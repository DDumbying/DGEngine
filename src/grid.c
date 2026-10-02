#include <string.h>
#include "internal.h"

static int cell_coord(float x, float cell) { return (int)floorf(x / cell); }

static int hash_cell(int x, int y, int z, int mask) {
    unsigned h = (unsigned)x * 92837111u ^ (unsigned)y * 689287499u ^ (unsigned)z * 283923481u;
    return (int)(h & (unsigned)mask);
}

int grid_init(sl_world *w) {
    grid *g = &w->g;
    g->table_size = 1024;
    while (g->table_size < 2 * w->max_particles) g->table_size <<= 1;
    g->cell = w->h + 0.1f * w->spacing;
    g->start = sl__alloc(w, (size_t)(g->table_size + 1) * sizeof(int));
    g->sorted = sl__alloc(w, (size_t)w->max_particles * sizeof(int));
    g->bucket = sl__alloc(w, (size_t)w->max_particles * sizeof(int));
    g->sorted_p = sl__alloc(w, (size_t)w->max_particles * sizeof(sl_vec3));
    return g->start && g->sorted && g->bucket && g->sorted_p;
}

void grid_free(sl_world *w) {
    sl__free(w, w->g.start);
    sl__free(w, w->g.sorted);
    sl__free(w, w->g.bucket);
    sl__free(w, w->g.sorted_p);
}

void grid_build(sl_world *w) {
    grid *g = &w->g;
    int mask = g->table_size - 1;
    memset(g->start, 0, (size_t)(g->table_size + 1) * sizeof(int));
    for (int i = 0; i < w->count; i++) {
        sl_vec3 p = w->p[i];
        int b = hash_cell(cell_coord(p.x, g->cell), cell_coord(p.y, g->cell), cell_coord(p.z, g->cell), mask);
        g->bucket[i] = b;
        g->start[b]++;
    }
    int sum = 0;
    for (int b = 0; b < g->table_size; b++) { sum += g->start[b]; g->start[b] = sum; }
    g->start[g->table_size] = sum;
    for (int i = w->count - 1; i >= 0; i--) g->sorted[--g->start[g->bucket[i]]] = i;
    for (int s = 0; s < w->count; s++) g->sorted_p[s] = w->p[g->sorted[s]];
}

/* Buckets of the 27 cells around (cx, cy, cz), with hash collisions removed so no pair is found twice. */
static int gather_buckets(const grid *g, int cx, int cy, int cz, int out[27]) {
    int mask = g->table_size - 1, n = 0;
    for (int dz = -1; dz <= 1; dz++)
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                int b = hash_cell(cx + dx, cy + dy, cz + dz, mask), dup = 0;
                for (int k = 0; k < n && !dup; k++) dup = out[k] == b;
                if (!dup) out[n++] = b;
            }
    return n;
}

/* Pairs within the kernel radius plus a margin, found once per step and reused by every substep.
   Particles are visited in bucket order so neighbors of a cell are gathered once for all of them. */
void grid_pairs(sl_world *w) {
    grid *g = &w->g;
    float range = w->h + 0.1f * w->spacing, h2 = range * range, reach = 2.0f * w->spacing;
    float glen = v3_len(w->gravity), shock = 0.6931f / w->spacing;
    sl_vec3 up = glen > 0 ? v3_scale(w->gravity, -1.0f / glen) : v3(0, 0, 0);
    int buckets[27], nb = 0, np = 0, nc = 0;
    int lx = 0, ly = 0, lz = 0, have = 0;
    for (int a = 0; a < w->count; a++) {
        int i = g->sorted[a];
        sl_vec3 pi = g->sorted_p[a];
        int cx = cell_coord(pi.x, g->cell), cy = cell_coord(pi.y, g->cell), cz = cell_coord(pi.z, g->cell);
        if (!have || cx != lx || cy != ly || cz != lz) {
            nb = gather_buckets(g, cx, cy, cz, buckets);
            lx = cx; ly = cy; lz = cz; have = 1;
        }
        for (int b = 0; b < nb; b++) {
            for (int s = g->start[buckets[b]]; s < g->start[buckets[b] + 1]; s++) {
                int j = g->sorted[s];
                if (j <= i) continue;
                sl_vec3 d = v3_sub(pi, g->sorted_p[s]);
                float r2 = v3_len2(d);
                if (r2 >= h2 || np == w->pair_cap) continue;
                float r = sqrtf(r2);
                w->pairs[np++] = (pair){i, j, r};
                /* Contacts keep a shock propagation factor: the upper particle acts lighter, so piles carry their weight. */
                if (r < reach && !(w->fluid[i] && w->fluid[j]))
                    w->contacts[nc++] = (pair){i, j, expf(shock * fminf(v3_dot(d, up), w->spacing))};
            }
        }
    }
    w->pair_count = np;
    w->contact_count = nc;
}
