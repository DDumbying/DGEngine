#include <string.h>
#include "internal.h"

/* Hash cell coordinate, clamped so huge positions cannot overflow an int; clamping never moves two points
   further apart in cells, so neighbors still sit in adjacent cells. */
static int cell_coord(float x, float cell) {
    float f = floorf(x / cell);
    return f >= 1e9f ? 1000000000 : (f >= -1e9f ? (int)f : -1000000000);
}

/* Dense cell along one axis; strays outside the grid box land in its border cells, by the same argument. */
static int dense_coord(float x, float origin, float cell, int n) {
    float f = floorf((x - origin) / cell);
    return f >= (float)(n - 1) ? n - 1 : (f >= 0 ? (int)f : 0);
}

static int hash_cell(int x, int y, int z, int mask) {
    unsigned h = (unsigned)x * 92837111u ^ (unsigned)y * 689287499u ^ (unsigned)z * 283923481u;
    return (int)(h & (unsigned)mask);
}

/* The distinct hash buckets of the 27 cells around x. */
static int hash_buckets(const grid *g, sl_vec3 x, int *buckets) {
    int nb = 0, mask = g->table_size - 1;
    int cx = cell_coord(x.x, g->cell), cy = cell_coord(x.y, g->cell), cz = cell_coord(x.z, g->cell);
    for (int dz = -1; dz <= 1; dz++)
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                int b = hash_cell(cx + dx, cy + dy, cz + dz, mask), dup = 0;
                for (int k = 0; k < nb && !dup; k++) dup = buckets[k] == b;
                if (!dup) buckets[nb++] = b;
            }
    return nb;
}

static int cell_of(const grid *g, sl_vec3 x) {
    if (g->dense) {
        int ix = dense_coord(x.x, g->origin.x, g->cell, g->nx), iy = dense_coord(x.y, g->origin.y, g->cell, g->ny);
        return ix + g->nx * (iy + g->ny * dense_coord(x.z, g->origin.z, g->cell, g->nz));
    }
    return hash_cell(cell_coord(x.x, g->cell), cell_coord(x.y, g->cell), cell_coord(x.z, g->cell), g->table_size - 1);
}

/* Bounds and position sum of each chunk, kept in tmp until they are merged. */
static void chunk_bounds(sl_world *w, int begin, int end, int chunk, void *ctx) {
    (void)ctx;
    sl_vec3 lo = w->x[begin], hi = w->x[begin], sum = w->x[begin];
    for (int s = begin + 1; s < end; s++) {
        sl_vec3 x = w->x[s];
        lo = v3(sl_min(lo.x, x.x), sl_min(lo.y, x.y), sl_min(lo.z, x.z));
        hi = v3(sl_max(hi.x, x.x), sl_max(hi.y, x.y), sl_max(hi.z, x.z));
        sum = v3_add(sum, x);
    }
    w->tmp[3 * chunk] = lo;
    w->tmp[3 * chunk + 1] = hi;
    w->tmp[3 * chunk + 2] = sum;
}

static void count_outside(sl_world *w, int begin, int end, int chunk, void *ctx) {
    const sl_vec3 *box = ctx;
    int n = 0;
    for (int s = begin; s < end; s++) {
        sl_vec3 x = w->x[s];
        n += x.x < box[0].x || x.y < box[0].y || x.z < box[0].z || x.x > box[1].x || x.y > box[1].y || x.z > box[1].z;
    }
    w->chunk_buf[chunk] = n;
}

/* Cells per axis that fit the budget, shrinking the longest axes first. */
static void fit_cells(double n[3], double budget) {
    int order[3] = {0, 1, 2};
    for (int i = 0; i < 2; i++)
        for (int j = i + 1; j < 3; j++)
            if (n[order[j]] < n[order[i]]) { int t = order[i]; order[i] = order[j]; order[j] = t; }
    for (int k = 0; k < 3; k++) {
        double limit = sl_iroot(budget, 3 - k);
        if (n[order[k]] > limit) n[order[k]] = limit;
        budget /= n[order[k]];
    }
}

/* Dense grid when it stays small, which keeps neighbor cells close in memory. When a few strays stretch the
   bounds, the dense box covers the bulk around the centroid and the strays share its border cells; only a
   world that is truly spread out falls back to the hash table. */
static int setup_grid(sl_world *w) {
    grid *g = &w->g;
    int chunks = sl__chunks(w->count);
    if (!sl__grow(w, (void **)&w->chunk_buf, &w->chunk_cap, chunks + 1, sizeof(int))) return 0;
    sl__parallel(w, w->count, chunk_bounds, NULL);
    sl_vec3 lo = w->tmp[0], hi = w->tmp[1], sum = w->tmp[2];
    for (int c = 1; c < chunks; c++) {
        sl_vec3 a = w->tmp[3 * c], b = w->tmp[3 * c + 1];
        lo = v3(sl_min(lo.x, a.x), sl_min(lo.y, a.y), sl_min(lo.z, a.z));
        hi = v3(sl_max(hi.x, b.x), sl_max(hi.y, b.y), sl_max(hi.z, b.z));
        sum = v3_add(sum, w->tmp[3 * c + 2]);
    }
    /* Dense cells are half the search range and scanned 5 wide, which tests far fewer far-off particles. */
    float range = w->h + w->skin, cell = 0.5f * range;
    double n[3] = {floor((hi.x - lo.x) / cell) + 1, floor((hi.y - lo.y) / cell) + 1, floor((hi.z - lo.z) / cell) + 1};
    double budget = 16.0 * w->count + 4096;
    sl_vec3 origin = lo;
    int dense = n[0] * n[1] * n[2] <= budget;
    if (!dense && v3_finite(lo) && v3_finite(hi)) {
        fit_cells(n, budget);
        sl_vec3 mean = v3_scale(sum, 1.0f / (float)w->count);
        float *o = &origin.x;
        const float *m = &mean.x, *l = &lo.x, *h = &hi.x;
        for (int a = 0; a < 3; a++) {
            float width = (float)n[a] * cell;
            o[a] = sl_max(l[a], sl_min(m[a] - 0.5f * width, h[a] - width));
        }
        sl_vec3 box[2] = {origin, v3(origin.x + (float)n[0] * cell, origin.y + (float)n[1] * cell, origin.z + (float)n[2] * cell)};
        sl__parallel(w, w->count, count_outside, box);
        int outside = 0;
        for (int c = 0; c < chunks; c++) outside += w->chunk_buf[c];
        dense = outside <= w->count / 32;
    }
    g->dense = dense;
    g->cell = dense ? cell : range;
    g->span = dense ? 2 : 1;
    if (dense) {
        g->origin = origin;
        g->nx = (int)n[0]; g->ny = (int)n[1]; g->nz = (int)n[2];
        g->cells = g->nx * g->ny * g->nz;
    } else {
        /* Sized from the particle count, never from buffer capacities, which differ between machines that
           reached the same state by different routes; bucket order shapes neighbor order and so results. */
        int table = 1024;
        while (table < (1 << 30) && table < 2LL * w->count) table <<= 1;
        g->table_size = table;
        g->cells = table;
    }
    return sl__grow(w, (void **)&g->start, &g->start_cap, g->cells + 1, sizeof(int));
}

static void find_cells(sl_world *w, int begin, int end, int chunk, void *ctx) {
    (void)chunk; (void)ctx;
    for (int s = begin; s < end; s++) w->g.bucket[s] = cell_of(&w->g, w->x[s]);
}

static void sort_cells(sl_world *w) {
    grid *g = &w->g;
    int n = w->count;
    memset(g->start, 0, (size_t)(g->cells + 1) * sizeof(int));
    sl__parallel(w, n, find_cells, NULL);
    for (int s = 0; s < n; s++) g->start[g->bucket[s] + 1]++;
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
    PERMUTE(int, w->material, is);
    w->tmp = vs; w->lambda = fs; w->island = is;

    unsigned char *bytes = (unsigned char *)g->bucket;
    unsigned char *barr[] = {w->flags, w->calm, w->mat, w->wet};
    for (int a = 0; a < 4; a++) {
        for (int k = 0; k < n; k++) bytes[k] = barr[a][g->sorted[k]];
        memcpy(barr[a], bytes, (size_t)n);
    }
    /* push_id is SL_PUSH_SLOTS bytes per slot, which fits the int per slot in bucket. */
    for (int k = 0; k < n; k++) memcpy(bytes + SL_PUSH_SLOTS * k, w->push_id + SL_PUSH_SLOTS * g->sorted[k], SL_PUSH_SLOTS);
    memcpy(w->push_id, bytes, (size_t)n * SL_PUSH_SLOTS);
    for (int q = 0; q < SL_PUSH_SLOTS; q++) {
        for (int k = 0; k < n; k++) w->tmp[k] = w->push[SL_PUSH_SLOTS * g->sorted[k] + q];
        for (int k = 0; k < n; k++) w->push[SL_PUSH_SLOTS * k + q] = w->tmp[k];
    }
    for (int q = 0; w->use_aniso && q < 4; q++) {
        for (int k = 0; k < n; k++) w->tmp[k] = w->aniso[4 * g->sorted[k] + q];
        for (int k = 0; k < n; k++) w->aniso[4 * k + q] = w->tmp[k];
    }
    for (int k = 0; k < n; k++) { w->id_slot[id_index(w, w->id[k])] = k; g->bucket[g->sorted[k]] = k; }
    sl__objects_remap(w, g->bucket);
    for (int k = 0; k < n; k++) g->sorted[k] = k;
}

#define SL_FAR_MAX 256

/* Every particle within range of slot s, from the cells around it, written to out up to room; returns the full
   count either way. Positions are read from xs, a copy in cell order, so each row of cells is one sequential
   run. With far set, neighbors closer than near2 come first and the rest are collected in far and appended,
   so passes that skip far neighbors branch predictably; it returns -1 if far fills up. */
static int scan_cells_split(const sl_world *w, const sl_vec3 *xs, int s, int *out, int room, float range2, float near2, int *far) {
    const grid *g = &w->g;
    sl_vec3 x = w->x[s];
    int n = 0, nf = 0;
#define TRY(t) do { float d2_ = v3_len2(v3_sub(xs[t], x)); \
        if (d2_ < range2) { int c_ = g->sorted[t]; \
            if (c_ != s) { \
                if (!far || d2_ < near2) { if (n < room) out[n] = c_; n++; } \
                else if (nf < SL_FAR_MAX) far[nf++] = c_; \
                else return -1; } } } while (0)
    if (g->dense) {
        int ix = dense_coord(x.x, g->origin.x, g->cell, g->nx), iy = dense_coord(x.y, g->origin.y, g->cell, g->ny);
        int iz = dense_coord(x.z, g->origin.z, g->cell, g->nz);
        int k = g->span, x0 = ix > k ? ix - k : 0, x1 = ix + k < g->nx ? ix + k : g->nx - 1;
        for (int z = iz - k; z <= iz + k; z++)
            for (int y = iy - k; y <= iy + k; y++) {
                if (z < 0 || z >= g->nz || y < 0 || y >= g->ny) continue;
                int row = g->nx * (y + g->ny * z);
                for (int t = g->start[row + x0]; t < g->start[row + x1 + 1]; t++) TRY(t);
            }
    } else {
        int buckets[27], nb = hash_buckets(g, x, buckets);
        for (int b = 0; b < nb; b++)
            for (int t = g->start[buckets[b]]; t < g->start[buckets[b] + 1]; t++) TRY(t);
    }
#undef TRY
    for (int k = 0; k < nf; k++) { if (n < room) out[n] = far[k]; n++; }
    return n;
}

/* Near neighbors first, or in plain scan order for the rare particle with more far neighbors than fit. Either
   way the list depends only on the positions it was built from. */
static int scan_cells(const sl_world *w, const sl_vec3 *xs, int s, int *out, int room, float range2) {
    int far[SL_FAR_MAX];
    int n = scan_cells_split(w, xs, s, out, room, range2, w->h * w->h, far);
    return n >= 0 ? n : scan_cells_split(w, xs, s, out, room, range2, 0, NULL);
}

static void copy_sorted(sl_world *w, int begin, int end, int chunk, void *ctx) {
    (void)chunk; (void)ctx;
    for (int t = begin; t < end; t++) w->tmp[t] = w->x[w->g.sorted[t]];
}

typedef struct { float range2; int room; } list_ctx;

/* Each chunk of slots writes its lists into its own slice of the scratch buffer and keeps the counts. */
static void find_lists(sl_world *w, int begin, int end, int chunk, void *ctx) {
    list_ctx *c = ctx;
    int *out = w->knbr + (size_t)chunk * (size_t)c->room, n = 0;
    for (int s = begin; s < end; s++) {
        int k = scan_cells(w, w->tmp, s, out + (n < c->room ? n : c->room), c->room - n, c->range2);
        w->order[s] = k;
        n += k;
    }
    w->chunk_buf[chunk] = n;
}

/* Copies each chunk's slice to its final place and fills the offsets. */
static void place_lists(sl_world *w, int begin, int end, int chunk, void *ctx) {
    const int *slice = w->knbr + (size_t)chunk * (size_t)*(int *)ctx;
    int at = w->chunk_buf[chunk], count = w->chunk_buf[chunk + 1] - at;
    if (count) memcpy(w->nbr + at, slice, (size_t)count * sizeof(int));
    for (int s = begin; s < end; s++) { w->nbr_off[s] = at; at += w->order[s]; }
}

/* Per-particle neighbor lists in both directions, built in parallel straight from the grid. */
static int build_lists(sl_world *w, float range2) {
    int n = w->count, chunks = sl__chunks(n);
    if (w->chunk_room < SL_CHUNK * 16) w->chunk_room = SL_CHUNK * 48;
    if (!sl__grow(w, (void **)&w->chunk_buf, &w->chunk_cap, chunks + 1, sizeof(int))) return 0;
    sl__parallel(w, n, copy_sorted, NULL);
    for (;;) {
        if (!sl__grow(w, (void **)&w->knbr, &w->knbr_cap, chunks * w->chunk_room, sizeof(int))) return 0;
        list_ctx lc = {range2, w->chunk_room};
        sl__parallel(w, n, find_lists, &lc);
        int most = 0;
        for (int c = 0; c < chunks; c++) if (w->chunk_buf[c] > most) most = w->chunk_buf[c];
        if (most <= w->chunk_room) break;
        w->chunk_room = most + most / 4;
    }
    int total = 0;
    for (int c = 0; c < chunks; c++) { int k = w->chunk_buf[c]; w->chunk_buf[c] = total; total += k; }
    w->chunk_buf[chunks] = total;
    if (!sl__grow(w, (void **)&w->nbr, &w->nbr_cap, total, sizeof(int))
        || !sl__grow(w, (void **)&w->kdist, &w->kdist_cap, total, sizeof(float))) return 0;
    sl__parallel(w, n, place_lists, &w->chunk_room);
    w->nbr_off[n] = total;
    w->pair_count = total / 2;
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

typedef struct { float reach2, shock; sl_vec3 up; int fill; } contact_ctx;

/* Two passes over fixed chunks of slots, each pair once from its lower slot: count, then fill at the
   chunk's offset, so the order never depends on threads. */
static void contact_range(sl_world *w, int begin, int end, int chunk, void *ctx) {
    contact_ctx *c = ctx;
    contact *out = c->fill ? w->contact_tmp + w->chunk_buf[chunk] : NULL;   /* no buffer yet while counting */
    int count = 0;
    for (int i = begin; i < end; i++)
        for (int k = w->nbr_off[i]; k < w->nbr_off[i + 1]; k++) {
            int j = w->nbr[k];
            if (j < i || !needs_contact(w, i, j, c->reach2)) continue;
            if (!c->fill) { count++; continue; }
            /* Shock propagation: the upper particle acts lighter, so piles carry their weight down.
               Only between solids; fluid below a grain must not act as a floor. */
            float h = sl_min(sl_max(v3_dot(v3_sub(w->x[i], w->x[j]), c->up), -w->spacing), w->spacing);
            const sl_material_desc *mi = &w->materials[w->mat[i]], *mj = &w->materials[w->mat[j]];
            float mu = 0.5f * (mi->friction + mj->friction), dist = w->spacing;
            if (w->obj[i] >= 0 && w->obj[i] == w->obj[j]) {
                dist = w->objects[w->obj[i]].self_dist;
                if (w->objects[w->obj[i]].kind == OBJ_SOFT) mu = 0;
            }
            /* Fluid against solids: no friction, and a little closer, so water can flow between grains. */
            int wet = (w->flags[i] | w->flags[j]) & F_FLUID;
            if (wet) { mu = 0; dist = 0.8f * w->spacing; }
            *out++ = (contact){i, j, wet ? 1.0f : sl_exp2(h * c->shock), mu, dist, 0.5f * (mi->wet_cohesion + mj->wet_cohesion) / 255.0f};
        }
    if (!c->fill) w->chunk_buf[chunk] = count;
}

static int collect_contacts(sl_world *w) {
    float reach = w->spacing + w->skin, glen = v3_len(w->gravity);
    contact_ctx c = {reach * reach, 1.0f / w->spacing, glen > 0 ? v3_scale(w->gravity, -1.0f / glen) : v3(0, 0, 0), 0};
    int chunks = sl__chunks(w->count), n = 0;
    sl__parallel(w, w->count, contact_range, &c);
    for (int k = 0; k < chunks; k++) { int count = w->chunk_buf[k]; w->chunk_buf[k] = n; n += count; }
    if (!sl__grow(w, (void **)&w->contact_tmp, &w->contact_tmp_cap, n, sizeof(contact))
        || !sl__grow(w, (void **)&w->contacts, &w->contact_cap, n, sizeof(contact))) return 0;
    c.fill = 1;
    sl__parallel(w, w->count, contact_range, &c);
    w->contact_count = n;
    return 1;
}

/* Greedy coloring of index pairs read from ends with the given stride: no two pairs in one color share
   an index, so a color can run in parallel. Colors go to w->colors, color starts to offsets. */
int sl__color_graph(sl_world *w, const int *ends, int stride, int count, int *offsets) {
    unsigned long long *used = (unsigned long long *)(void *)w->tmp;
    int counts[SL_MAX_COLORS + 1] = {0};
    if (!sl__grow(w, (void **)&w->colors, &w->color_cap, count + 1, 1)) return 0;
    for (int s = 0; s < w->count; s++) used[s] = 0;
    for (int k = 0; k < count; k++) {
        int a = ends[(size_t)k * stride], b = ends[(size_t)k * stride + 1], color = SL_MAX_COLORS;
        unsigned long long free_bits = ~(used[a] | used[b]);
        if (free_bits) {
            color = 0;
            while (!(free_bits >> color & 1ull)) color++;
            used[a] |= 1ull << color;
            used[b] |= 1ull << color;
        }
        w->colors[k] = (unsigned char)color;
        counts[color]++;
    }
    int sum = 0;
    for (int c = 0; c <= SL_MAX_COLORS; c++) { offsets[c] = sum; sum += counts[c]; }
    offsets[SL_MAX_COLORS + 1] = sum;
    return 1;
}

static int color_contacts(sl_world *w) {
    int fill[SL_MAX_COLORS + 2];
    if (!sl__color_graph(w, (const int *)(void *)w->contact_tmp, (int)(sizeof(contact) / sizeof(int)), w->contact_count, w->color_off)) return 0;
    memcpy(fill, w->color_off, sizeof fill);
    for (int k = 0; k < w->contact_count; k++) w->contacts[fill[w->colors[k]]++] = w->contact_tmp[k];
    return 1;
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
    for (int i = 0; i < w->count; i++)
        for (int k = w->nbr_off[i]; k < w->nbr_off[i + 1]; k++) if (w->nbr[k] > i) unite(parent, i, w->nbr[k]);
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

static void all_awake(sl_world *w) {
    for (int s = 0; s < w->count; s++) { w->active[s] = s; w->flags[s] |= F_AWAKE; }
    w->active_count = w->count;
}

static void update_islands(sl_world *w) {
    if (!sl__grow(w, (void **)&w->island_calm, &w->island_cap, w->island_count + 1, sizeof(int))) { all_awake(w); return; }
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

/* Picks who sleeps, at the start of a step. Islands are only worked out when some particle has been
   calm long enough to sleep, so violent scenes never pay for them. */
void sl__settle_islands(sl_world *w) {
    int calm = 0;
    if (w->sleep_speed > 0) for (int s = 0; s < w->count && !calm; s++) calm = w->calm[s] >= SL_CALM_STEPS;
    if (!calm) { all_awake(w); return; }
    if (w->islands_stale) { build_islands(w); w->islands_stale = 0; }
    update_islands(w);
}

/* Out of memory partway: no contacts and one awake island, so the step stays safe until a rebuild works. */
static int rebuild_failed(sl_world *w) {
    w->contact_count = w->pair_count = 0;
    memset(w->color_off, 0, sizeof w->color_off);
    if (w->nbr_off) for (int s = 0; s <= w->count; s++) w->nbr_off[s] = 0;
    all_awake(w);
    w->islands_stale = 1;
    w->built = w->count;
    w->nbr_valid = 0;
    return 0;
}

int sl__grid_rebuild(sl_world *w, int step_start) {
    int n = w->count;
    w->nbr_valid = 0;
    if (n == 0) {
        w->active_count = w->contact_count = w->pair_count = w->island_count = w->built = 0;
        memset(w->color_off, 0, sizeof w->color_off);
        if (w->nbr_off) w->nbr_off[0] = 0;
        return 1;
    }
    int ok = 1;
    PROF(P_SORT, ok = setup_grid(w); if (ok) sort_cells(w));
    if (!ok) return rebuild_failed(w);
    if (step_start) PROF(P_REORDER, reorder(w));
    if (w->mem_dirty && sl__objects_membership(w)) w->mem_dirty = 0;
    for (int s = 0; s < n; s++) { w->p[s] = w->x[s]; w->x_build[s] = w->x[s]; }

    float range = w->h + w->skin;
    PROF(P_PAIRS, ok = build_lists(w, range * range));
    if (ok) PROF(P_CONTACTS, ok = collect_contacts(w));
    if (ok) PROF(P_COLOR, ok = color_contacts(w));
    if (!ok) return rebuild_failed(w);
    /* Mid-step rebuilds keep slots and the awake set; who sleeps is decided at the next step start. */
    w->islands_stale = 1;
    if (step_start) PROF(P_ISLANDS, sl__settle_islands(w));
    w->built = n;
    w->nbr_valid = 1;
    return 1;
}

/* Fluid particles within the kernel radius of x and their average velocity, from the last grid build. */
int sl__grid_fluid_near(sl_world *w, sl_vec3 x, sl_vec3 *avg_vel) {
    const grid *g = &w->g;
    float h2 = w->h * w->h;
    int n = 0;
    sl_vec3 v = v3(0, 0, 0);
    if (!g->start || w->count == 0) { *avg_vel = v; return 0; }
    if (g->dense) {
        int ix = dense_coord(x.x, g->origin.x, g->cell, g->nx), iy = dense_coord(x.y, g->origin.y, g->cell, g->ny);
        int iz = dense_coord(x.z, g->origin.z, g->cell, g->nz);
        int k = g->span;
        for (int z = iz - k; z <= iz + k; z++)
            for (int y = iy - k; y <= iy + k; y++) {
                if (z < 0 || z >= g->nz || y < 0 || y >= g->ny) continue;
                int x0 = ix - k < 0 ? 0 : ix - k, x1 = ix + k >= g->nx ? g->nx - 1 : ix + k;
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
        int buckets[27], nb = hash_buckets(g, x, buckets);
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
