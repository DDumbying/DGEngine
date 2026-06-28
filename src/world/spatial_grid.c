#include "spatial_grid.h"
#include "../core/log.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ------------------------------------------------------------------ */
/*  Helpers                                                             */
/* ------------------------------------------------------------------ */

static inline bool in_bounds(const SpatialGrid *g, int x, int y) {
    return x >= 0 && y >= 0 && x < g->width && y < g->height;
}

static inline int idx(const SpatialGrid *g, int x, int y) {
    return y * g->width + x;
}

/* ------------------------------------------------------------------ */
/*  Lifecycle                                                            */
/* ------------------------------------------------------------------ */

bool sgrid_create(SpatialGrid *g, int width, int height) {
    g->width  = width;
    g->height = height;
    g->cells  = malloc((size_t)(width * height) * sizeof(Entity));
    if (!g->cells) {
        LOG_ERROR("sgrid_create: out of memory (%d x %d)", width, height);
        return false;
    }
    /* ENTITY_NULL == 0xFFFFFFFF — fill with 0xFF to set all cells empty */
    memset(g->cells, 0xFF, (size_t)(width * height) * sizeof(Entity));
    return true;
}

void sgrid_destroy(SpatialGrid *g) {
    if (!g) return;
    free(g->cells);
    g->cells  = NULL;
    g->width  = 0;
    g->height = 0;
}

/* ------------------------------------------------------------------ */
/*  Mutation                                                             */
/* ------------------------------------------------------------------ */

void sgrid_insert(SpatialGrid *g, Entity e, int gx, int gy) {
    if (!g->cells || !in_bounds(g, gx, gy)) return;
    g->cells[idx(g, gx, gy)] = e;
}

void sgrid_remove(SpatialGrid *g, int gx, int gy) {
    if (!g->cells || !in_bounds(g, gx, gy)) return;
    g->cells[idx(g, gx, gy)] = ENTITY_NULL;
}

void sgrid_move(SpatialGrid *g, Entity e,
                int from_x, int from_y,
                int to_x, int to_y) {
    if (!g->cells) return;
    if (in_bounds(g, from_x, from_y))
        g->cells[idx(g, from_x, from_y)] = ENTITY_NULL;
    if (in_bounds(g, to_x, to_y))
        g->cells[idx(g, to_x, to_y)] = e;
}

/* ------------------------------------------------------------------ */
/*  Query                                                                */
/* ------------------------------------------------------------------ */

Entity sgrid_at(const SpatialGrid *g, int gx, int gy) {
    if (!g->cells || !in_bounds(g, gx, gy)) return ENTITY_NULL;
    return g->cells[idx(g, gx, gy)];
}

/* ------------------------------------------------------------------ */
/*  Rebuild                                                              */
/* ------------------------------------------------------------------ */

void sgrid_rebuild(SpatialGrid *g,
                   int new_width, int new_height,
                   const bool  *alive,
                   const bool  *has_transform,
                   const float *tx, const float *ty,
                   int entity_count) {
    /* Resize the allocation if dimensions changed */
    if (new_width != g->width || new_height != g->height) {
        free(g->cells);
        g->width  = new_width;
        g->height = new_height;
        g->cells  = malloc((size_t)(new_width * new_height) * sizeof(Entity));
        if (!g->cells) {
            LOG_ERROR("sgrid_rebuild: out of memory (%d x %d)", new_width, new_height);
            return;
        }
    }
    /* Clear */
    memset(g->cells, 0xFF, (size_t)(g->width * g->height) * sizeof(Entity));

    /* Re-insert every alive entity that has a position */
    for (int e = 0; e < entity_count; e++) {
        if (!alive[e] || !has_transform[e]) continue;
        int gx = (int)floorf(tx[e] + 0.5f);
        int gy = (int)floorf(ty[e] + 0.5f);
        if (in_bounds(g, gx, gy))
            g->cells[idx(g, gx, gy)] = (Entity)e;
    }
}
