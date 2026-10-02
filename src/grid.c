#include <string.h>
#include "internal.h"

static int cell_coord(float x, float cell) { return (int)floorf(x / cell); }

static int hash_cell(int x, int y, int z, int mask) {
    unsigned h = (unsigned)x * 92837111u ^ (unsigned)y * 689287499u ^ (unsigned)z * 283923481u;
    return (int)(h & (unsigned)mask);
}

static int cell_of(const grid *g, sl_vec3 x) {
    if (g->dense) {
        int ix = (int)((x.x - g->origin.x) / g->cell), iy = (int)((x.y - g->origin.y) / g->cell), iz = (int)((x.z - g->origin.z) / g->cell);
        return ix + g->nx * (iy + g->ny * iz);
    }
    return hash_cell(cell_coord(x.x, g->cell), cell_coord(x.y, g->cell), cell_coord(x.z, g->cell), g->table_size - 1);
}

/* Dense grid over the particle bounds when it stays small, which keeps neighbor cells close in memory. */
static int setup_grid(sl_world *w) {
    grid *g = &w->g;
    sl_vec3 lo = w->x[0], hi = w->x[0];
    for (int s = 1; s < w->count; s++) {
        sl_vec3 x = w->x[s];
        lo = v3(fminf(lo.x, x.x), fminf(lo.y, x.y), fminf(lo.z, x.z));
        hi = v3(fmaxf(hi.x, x.x), fmaxf(hi.y, x.y), fmaxf(hi.z, x.z));
    }
    double nx = floor((hi.x - lo.x) / g->cell) + 1, ny = floor((hi.y - lo.y) / g->cell) + 1, nz = floor((hi.z - lo.z) / g->cell) + 1;
    double cells = nx * ny * nz;
    g->dense = cells <= 8.0 * w->count + 4096;
    if (g->dense) {
        g->origin = lo;
        g->nx = (int)nx; g->ny = (int)ny; g->nz = (int)nz;
        g->cells = (int)cells;
    } else {
        g->cells = g->table_size;
    }
    return sl__grow(w, (void **)&g->start, &g->start_cap, g->cells + 1, sizeof(int));
}

static void sort_cells(sl_world *w) {
    grid *g = &w->g;
    int n = w->count;
    memset(g->start, 0, (size_t)(g->cells + 1) * sizeof(int));
    for (int s = 0; s < n; s++) { int b = cell_of(g, w->x[s]); g->bucket[s] = b; g->start[b + 1]++; }
    for (int b = 0; b < g->cells; b++) g->start[b + 1] += g->start[b];
    for (int s = 0; s < n; s++) g->sorted[g->start[g->bucket[s]]++] = s;
    for (int b = g->cells; b > 0; b--) g->start[b] = g->start[b - 1];
    g->start[0] = 0;
}

#define PERMUTE(type, arr, scratch) do { type *src_ = (type *)(arr), *dst_ = (type *)(scratch); \
    for (int k = 0; k < n; k++) dst_[k] = src_[g->sorted[k]]; \
    (scratch) = (void *)src_; (arr) = (void *)dst_; } while (0)

/* Moves particles in memory into cell order so neighbors sit close together. */
static void reorder(sl_world *w) {
    grid *g = &w->g;
    int n = w->count;
    void *vs = w->tmp, *fs = w->lambda, *is = w->island;
    PERMUTE(sl_vec3, w->x, vs);
    PERMUTE(sl_vec3, w->p, vs);
    PERMUTE(sl_vec3, w->v, vs);
    PERMUTE(sl_vec3, w->x_step, vs);
    PERMUTE(float, w->inv_mass, fs);
    PERMUTE(float, w->mass, fs);
    PERMUTE(int, w->id, is);
    PERMUTE(int, w->obj, is);
    w->tmp = vs; w->lambda = fs; w->island = is;

    unsigned char *bytes = (unsigned char *)g->bucket;
    unsigned char *barr[] = {w->flags, w->calm, w->mat, w->wet};
    for (int a = 0; a < 4; a++) {
        for (int k = 0; k < n; k++) bytes[k] = barr[a][g->sorted[k]];
        memcpy(barr[a], bytes, (size_t)n);
    }
    unsigned short *pairs16 = (unsigned short *)(void *)g->bucket, *ids16 = (unsigned short *)(void *)w->push_id;
    for (int k = 0; k < n; k++) pairs16[k] = ids16[g->sorted[k]];
    memcpy(ids16, pairs16, (size_t)n * 2);
    for (int half = 0; half < 2; half++) {
        for (int k = 0; k < n; k++) w->tmp[k] = w->push[2 * g->sorted[k] + half];
        for (int k = 0; k < n; k++) w->push[2 * k + half] = w->tmp[k];
    }
    for (int k = 0; k < n; k++) { w->id_slot[w->id[k]] = k; g->bucket[g->sorted[k]] = k; }
    objects_remap(w, g->bucket);
    for (int k = 0; k < n; k++) g->sorted[k] = k;
}

/* Pairs found from rank a looking only forward, so every close pair turns up exactly once.
   Writes at most room pairs to out but always returns the full count. */
static int scan_forward(sl_world *w, int a, int *out, int room, float range2) {
    const grid *g = &w->g;
    int s = g->sorted[a], n = 0;
    sl_vec3 x = w->x[s];
#define TRY(t) do { int c_ = g->sorted[t]; if (v3_len2(v3_sub(w->x[c_], x)) < range2) { if (n < room) { out[2 * n] = s; out[2 * n + 1] = c_; } n++; } } while (0)
    if (g->dense) {
        int ix = (int)((x.x - g->origin.x) / g->cell), iy = (int)((x.y - g->origin.y) / g->cell), iz = (int)((x.z - g->origin.z) / g->cell);
        int x0 = ix > 0 ? ix - 1 : 0, x1 = ix + 1 < g->nx ? ix + 1 : g->nx - 1;
        int own = g->nx * (iy + g->ny * iz);
        for (int t = a + 1; t < g->start[own + x1 + 1]; t++) TRY(t);
        int rows[4][2] = {{iy + 1, iz}, {iy - 1, iz + 1}, {iy, iz + 1}, {iy + 1, iz + 1}};
        for (int r = 0; r < 4; r++) {
            int y = rows[r][0], z = rows[r][1];
            if (y < 0 || y >= g->ny || z >= g->nz) continue;
            int row = g->nx * (y + g->ny * z);
            for (int t = g->start[row + x0]; t < g->start[row + x1 + 1]; t++) TRY(t);
        }
        return n;
    }
    int buckets[27], nb = 0, mask = g->table_size - 1;
    int cx = cell_coord(x.x, g->cell), cy = cell_coord(x.y, g->cell), cz = cell_coord(x.z, g->cell);
    for (int dz = -1; dz <= 1; dz++)
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                int b = hash_cell(cx + dx, cy + dy, cz + dz, mask), dup = 0;
                for (int k = 0; k < nb && !dup; k++) dup = buckets[k] == b;
                if (!dup) buckets[nb++] = b;
            }
    for (int b = 0; b < nb; b++)
        for (int t = g->start[buckets[b]]; t < g->start[buckets[b] + 1]; t++)
            if (t > a) TRY(t);
#undef TRY
    return n;
}

typedef struct { float range2; int room; int *found; } pair_ctx1;

/* One pass: each chunk writes its pairs into its own slice, and notes how many it found. */
static void find_pairs(sl_world *w, int begin, int end, int chunk, void *ctx) {
    pair_ctx1 *c = ctx;
    int *out = w->pairs + (size_t)2 * (size_t)chunk * (size_t)c->room, n = 0;
    for (int a = begin; a < end; a++) n += scan_forward(w, a, out + 2 * (n < c->room ? n : c->room), c->room - n, c->range2);
    c->found[chunk] = n;
}

static int gather_pairs(sl_world *w, float range2) {
    int n = w->count, chunks = sl__chunks(n), *found = w->order;
    if (w->chunk_pairs < SL_CHUNK * 8) w->chunk_pairs = SL_CHUNK * 24;
    for (;;) {
        if (!sl__grow(w, (void **)&w->nbr_r, &w->nbr_r_cap, 2 * chunks * w->chunk_pairs, sizeof(float))) return 0;
        w->pairs = (int *)(void *)w->nbr_r;
        pair_ctx1 pc = {range2, w->chunk_pairs, found};
        sl__parallel(w, n, find_pairs, &pc);
        int most = 0;
        for (int c = 0; c < chunks; c++) if (found[c] > most) most = found[c];
        if (most <= w->chunk_pairs) break;
        w->chunk_pairs = most + most / 4;
    }
    /* Close the gaps between chunk slices; moving front to back never overwrites unread data. */
    int total = 0;
    for (int c = 0; c < chunks; c++) {
        if (total != c * w->chunk_pairs)
            memmove(w->pairs + 2 * total, w->pairs + (size_t)2 * c * w->chunk_pairs, (size_t)found[c] * 2 * sizeof(int));
        total += found[c];
    }
    w->pair_count = total;
    return 1;
}

/* Per-particle neighbor lists in both directions, from the pair list. */
static int build_neighbors(sl_world *w) {
    int n = w->count, *fill = w->order;
    if (!sl__grow(w, (void **)&w->nbr, &w->nbr_cap, 2 * w->pair_count, sizeof(int))) return 0;
    for (int s = 0; s <= n; s++) w->nbr_off[s] = 0;
    for (int k = 0; k < 2 * w->pair_count; k++) w->nbr_off[w->pairs[k] + 1]++;
    for (int s = 0; s < n; s++) { w->nbr_off[s + 1] += w->nbr_off[s]; fill[s] = w->nbr_off[s]; }
    for (int k = 0; k < w->pair_count; k++) {
        int i = w->pairs[2 * k], j = w->pairs[2 * k + 1];
        w->nbr[fill[i]++] = j;
        w->nbr[fill[j]++] = i;
    }
    return 1;
}

/* Fluid meets grains through pressure alone, which lets grains sink and water flow through a pile;
   cloth and soft bodies also get hard contacts so they stay watertight. */
static int needs_contact(const sl_world *w, int i, int j, float reach2) {
    int fi = w->flags[i] & F_FLUID, fj = w->flags[j] & F_FLUID;
    if (fi && fj) return 0;
    if ((fi && w->materials[w->mat[j]].kind == SL_GRANULAR) || (fj && w->materials[w->mat[i]].kind == SL_GRANULAR)) return 0;
    return v3_len2(v3_sub(w->x[i], w->x[j])) < reach2;
}

static int collect_contacts(sl_world *w) {
    float reach = w->spacing + w->skin, reach2 = reach * reach;
    float glen = v3_len(w->gravity), shock = 0.6931f / w->spacing;
    sl_vec3 up = glen > 0 ? v3_scale(w->gravity, -1.0f / glen) : v3(0, 0, 0);
    int n = 0;
    for (int k = 0; k < w->pair_count; k++) n += needs_contact(w, w->pairs[2 * k], w->pairs[2 * k + 1], reach2);
    if (!sl__grow(w, (void **)&w->contact_tmp, &w->contact_tmp_cap, n, sizeof(contact))
        || !sl__grow(w, (void **)&w->contacts, &w->contact_cap, n, sizeof(contact))) return 0;
    n = 0;
    for (int k = 0; k < w->pair_count; k++) {
        int i = w->pairs[2 * k], j = w->pairs[2 * k + 1];
        if (!needs_contact(w, i, j, reach2)) continue;
        /* Shock propagation: the upper particle acts lighter, so piles carry their weight down.
           Only between solids; fluid below a grain must not act as a floor. */
        float h = fminf(fmaxf(v3_dot(v3_sub(w->x[i], w->x[j]), up), -w->spacing), w->spacing);
        int wet = (w->flags[i] | w->flags[j]) & F_FLUID;
        w->contact_tmp[n++] = (contact){i, j, wet ? 1.0f : expf(shock * h), 0};
    }
    w->contact_count = n;
    return 1;
}

/* Greedy coloring: no two contacts in one color share a particle, so a color can run in parallel. */
static void color_contacts(sl_world *w) {
    unsigned long long *used = (unsigned long long *)(void *)w->tmp;
    int counts[SL_MAX_COLORS + 1] = {0}, fill[SL_MAX_COLORS + 1];
    for (int s = 0; s < w->count; s++) used[s] = 0;
    for (int k = 0; k < w->contact_count; k++) {
        contact *c = &w->contact_tmp[k];
        unsigned long long free_bits = ~(used[c->i] | used[c->j]);
        int color = SL_MAX_COLORS;
        if (free_bits) {
            color = 0;
            while (!(free_bits >> color & 1ull)) color++;
            used[c->i] |= 1ull << color;
            used[c->j] |= 1ull << color;
        }
        c->color = color;
        counts[color]++;
    }
    int sum = 0;
    for (int c = 0; c <= SL_MAX_COLORS; c++) { w->color_off[c] = fill[c] = sum; sum += counts[c]; }
    w->color_off[SL_MAX_COLORS + 1] = sum;
    for (int k = 0; k < w->contact_count; k++) w->contacts[fill[w->contact_tmp[k].color]++] = w->contact_tmp[k];
}

static int find(int *parent, int a) {
    while (parent[a] != a) { parent[a] = parent[parent[a]]; a = parent[a]; }
    return a;
}

static void unite(int *parent, int a, int b) {
    a = find(parent, a);
    b = find(parent, b);
    if (a < b) parent[b] = a;
    else if (b < a) parent[a] = b;
}

/* Islands are groups of particles that can affect each other; a calm island sleeps as a whole. */
static void build_islands(sl_world *w) {
    int *parent = w->island, *root = (int *)(void *)w->tmp, *label = w->order;
    for (int s = 0; s < w->count; s++) parent[s] = s;
    for (int k = 0; k < w->pair_count; k++) unite(parent, w->pairs[2 * k], w->pairs[2 * k + 1]);
    for (int c = 0; c < w->dist_count; c++) unite(parent, w->dist[c].a, w->dist[c].b);
    for (int c = 0; c < w->cluster_count; c++)
        for (int m = 1; m < w->clusters[c].count; m++)
            unite(parent, w->members[w->clusters[c].first].slot, w->members[w->clusters[c].first + m].slot);
    int n = 0;
    for (int s = 0; s < w->count; s++) { root[s] = find(parent, s); label[s] = -1; }
    for (int s = 0; s < w->count; s++) if (label[root[s]] < 0) label[root[s]] = n++;
    for (int s = 0; s < w->count; s++) parent[s] = label[root[s]];
    w->island_count = n;
}

void update_islands(sl_world *w) {
    if (!sl__grow(w, (void **)&w->island_calm, &w->island_cap, w->island_count + 1, sizeof(int))) {
        w->active_count = w->count;
        for (int s = 0; s < w->count; s++) { w->active[s] = s; w->flags[s] |= F_AWAKE; }
        return;
    }
    for (int k = 0; k < w->island_count; k++) w->island_calm[k] = 255;
    for (int s = 0; s < w->count; s++) {
        int k = w->island[s];
        if (w->calm[s] < w->island_calm[k]) w->island_calm[k] = w->calm[s];
    }
    int n = 0;
    for (int s = 0; s < w->count; s++) {
        if (w->sleep_speed <= 0 || w->island_calm[w->island[s]] < SL_CALM_STEPS) {
            w->active[n++] = s;
            w->flags[s] |= F_AWAKE;
        } else {
            w->v[s] = v3(0, 0, 0);
            w->x_step[s] = w->x[s];
            w->flags[s] &= (unsigned char)~F_AWAKE;
        }
    }
    w->active_count = n;
}

int grid_rebuild(sl_world *w, int move) {
    int n = w->count;
    if (n == 0) { w->active_count = 0; w->contact_count = 0; w->island_count = 0; if (w->nbr_off) w->nbr_off[0] = 0; return 1; }
    if (!setup_grid(w)) return 0;
    sort_cells(w);
    if (move) reorder(w);
    if (w->mem_dirty && objects_membership(w)) w->mem_dirty = 0;
    for (int s = 0; s < n; s++) { w->p[s] = w->x[s]; w->x_build[s] = w->x[s]; }

    float range = w->h + w->skin;
    if (!gather_pairs(w, range * range)) return 0;
    if (!build_neighbors(w) || !collect_contacts(w)) return 0;
    color_contacts(w);

    build_islands(w);
    update_islands(w);
    w->built = n;
    return 1;
}

/* Fluid particles within the kernel radius of x and their average velocity, from the last grid build. */
int grid_fluid_near(sl_world *w, sl_vec3 x, sl_vec3 *avg_vel) {
    const grid *g = &w->g;
    float h2 = w->h * w->h;
    int n = 0;
    sl_vec3 v = v3(0, 0, 0);
    if (!g->start || w->count == 0) { *avg_vel = v; return 0; }
    if (g->dense) {
        int ix = (int)floorf((x.x - g->origin.x) / g->cell), iy = (int)floorf((x.y - g->origin.y) / g->cell), iz = (int)floorf((x.z - g->origin.z) / g->cell);
        for (int z = iz - 1; z <= iz + 1; z++)
            for (int y = iy - 1; y <= iy + 1; y++) {
                if (z < 0 || z >= g->nz || y < 0 || y >= g->ny) continue;
                int x0 = ix - 1 < 0 ? 0 : ix - 1, x1 = ix + 1 >= g->nx ? g->nx - 1 : ix + 1;
                if (x0 > x1) continue;
                int row = g->nx * (y + g->ny * z);
                for (int t = g->start[row + x0]; t < g->start[row + x1 + 1]; t++) {
                    int s = g->sorted[t];
                    if (s >= w->count || !(w->flags[s] & F_FLUID) || v3_len2(v3_sub(w->x[s], x)) >= h2) continue;
                    v = v3_add(v, w->v[s]);
                    n++;
                }
            }
    } else {
        int buckets[27], nb = 0, mask = g->table_size - 1;
        int cx = cell_coord(x.x, g->cell), cy = cell_coord(x.y, g->cell), cz = cell_coord(x.z, g->cell);
        for (int dz = -1; dz <= 1; dz++)
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int b = hash_cell(cx + dx, cy + dy, cz + dz, mask), dup = 0;
                    for (int k = 0; k < nb && !dup; k++) dup = buckets[k] == b;
                    if (!dup) buckets[nb++] = b;
                }
        for (int b = 0; b < nb; b++)
            for (int t = g->start[buckets[b]]; t < g->start[buckets[b] + 1]; t++) {
                int s = g->sorted[t];
                if (s >= w->count || !(w->flags[s] & F_FLUID) || v3_len2(v3_sub(w->x[s], x)) >= h2) continue;
                v = v3_add(v, w->v[s]);
                n++;
            }
    }
    *avg_vel = n ? v3_scale(v, 1.0f / (float)n) : v;
    return n;
}
