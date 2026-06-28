#ifndef DGE_SPATIAL_GRID_H
#define DGE_SPATIAL_GRID_H

/*  Phase 4: Spatial Grid
    ---------------------
    Maps every tile (gx, gy) to the entity currently occupying it.
    At most one entity can physically stand on a tile at a time, so this
    is a flat Entity array rather than a bucket list:

        O(1) insert / remove / lookup — no hashing, no linked list, no
        allocation.  Memory cost: 4 bytes * W * H (same order as the
        Tile array itself).

    The grid is a separate allocation from the World so it can be
    created/destroyed independently and so world.h does not need to
    include entity.h (tile.h is already enough there).

    Lifecycle
    ---------
        sgrid_create()   — call after world_create() with the same dims.
        sgrid_destroy()  — call before world_destroy().
        sgrid_rebuild()  — O(N) full rebuild from the Registry; call after
                           a world resize or a registry_load().
        sgrid_insert()   — called by entity_add_transform (or placement
                           code) to register a new occupant.
        sgrid_remove()   — called by entity_destroy to vacate a slot.
        sgrid_move()     — called by system_update_agents when an entity
                           commits to a new tile (progress reaches 1.0).
        sgrid_at()       — O(1) lookup; returns ENTITY_NULL if empty.

    Invariants maintained by callers
    ---------------------------------
    •  A slot holds ENTITY_NULL or one valid, alive entity.
    •  insert/move write src→ENTITY_NULL and dst→e atomically.
    •  The grid is rebuilt from scratch on any load, so stale data from
       a previous session never survives.                                */

#include <stdbool.h>
#include "../ecs/entity.h"

typedef struct {
    Entity *cells;   /* width * height, row-major; ENTITY_NULL == empty */
    int     width;
    int     height;
} SpatialGrid;

/* Allocate cells for a w×h grid.  All cells are set to ENTITY_NULL.
   Returns false on allocation failure. */
bool sgrid_create(SpatialGrid *g, int width, int height);

/* Free cells.  Safe to call on a zero-initialized or already-destroyed grid. */
void sgrid_destroy(SpatialGrid *g);

/* Register entity e at (gx, gy).  Out-of-bounds coords are silently
   ignored.  Does NOT check for an existing occupant — callers that care
   should sgrid_remove() the old occupant first. */
void sgrid_insert(SpatialGrid *g, Entity e, int gx, int gy);

/* Remove whatever is at (gx, gy), setting the cell to ENTITY_NULL.
   Safe to call when the cell is already empty. */
void sgrid_remove(SpatialGrid *g, int gx, int gy);

/* Atomic move: clear (from_x, from_y) and set (to_x, to_y) to e.
   Both coords are bounds-checked; no-op on out-of-range. */
void sgrid_move(SpatialGrid *g, Entity e, int from_x, int from_y,
                int to_x, int to_y);

/* O(1) lookup.  Returns ENTITY_NULL for empty cells or out-of-bounds. */
Entity sgrid_at(const SpatialGrid *g, int gx, int gy);

/* O(N) full rebuild from the registry's transform components.
   Call after world_resize() or registry_load() to bring the grid back
   in sync.  Accepts a parallel arrays signature so spatial_grid.c does
   not need to include the full Registry header. */
void sgrid_rebuild(SpatialGrid *g,
                   int new_width, int new_height,
                   const bool *alive,
                   const bool *has_transform,
                   const float *tx, const float *ty,
                   int entity_count);

#endif /* DGE_SPATIAL_GRID_H */
