#ifndef DGE_WORLD_SHAPE_H
#define DGE_WORLD_SHAPE_H

/*  World Shape system — allows the playable area to be any shape,
    not just a filled rectangle.

    By default, world_shape_is_active() returns false and every tile in
    the grid is considered "in bounds" (the old rectangular behaviour).

    When shape editing is enabled, each tile has a 1-bit "enabled" flag.
    Disabled tiles are excluded from pathfinding, painting, entity
    placement, and rendering — they are effectively holes in the map.
    This lets the developer define islands, corridors, L-shaped levels,
    dungeon rooms, etc. without being forced into rectangular world bounds.

    The mask is stored as a flat bit array alongside the tile grid and
    is saved/loaded with the world file (a separate optional section).

    Shape editing is a new editor sub-mode (EDITOR_MODE_SHAPE):
      - LMB: enable tiles (add to playable area)
      - RMB: disable tiles (remove from playable area)
      - Hold Shift + LMB: fill-rect select
    The mask is initialized to all-enabled when first created so that
    existing projects aren't broken.                                     */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define WORLD_SHAPE_CHUNK 8   /* bits per byte */

typedef struct {
    unsigned char *mask;  /* bit array, width*height bits, row-major */
    int            width;
    int            height;
    bool           active; /* false = rectangular (all enabled), no mask alloc needed */
} WorldShape;

/* Initialize (inactive, no allocation) */
static inline void world_shape_init(WorldShape *ws) {
    ws->mask   = NULL;
    ws->width  = 0;
    ws->height = 0;
    ws->active = false;
}

/* Allocate/resize mask to (w x h), filling new bits with `default_enabled`.
   If the shape is inactive, this activates it. */
static inline bool world_shape_resize(WorldShape *ws, int w, int h, bool default_enabled) {
    int bytes = (w * h + 7) / 8;
    unsigned char *m = (unsigned char *)realloc(ws->mask, (size_t)bytes);
    if (!m) return false;
    /* Fill new bytes */
    int old_bytes = ws->mask ? (ws->width * ws->height + 7) / 8 : 0;
    if (bytes > old_bytes)
        memset(m + old_bytes, default_enabled ? 0xFF : 0x00, (size_t)(bytes - old_bytes));
    ws->mask   = m;
    ws->width  = w;
    ws->height = h;
    ws->active = true;
    return true;
}

static inline void world_shape_destroy(WorldShape *ws) {
    free(ws->mask);
    ws->mask   = NULL;
    ws->active = false;
}

/* Returns true if tile (x,y) is part of the playable area.
   When shape is inactive, every in-bounds tile returns true. */
static inline bool world_shape_enabled(const WorldShape *ws, int x, int y) {
    if (!ws->active || !ws->mask) return true;
    if (x < 0 || y < 0 || x >= ws->width || y >= ws->height) return false;
    int bit = y * ws->width + x;
    return (ws->mask[bit / 8] >> (bit % 8)) & 1;
}

static inline void world_shape_set(WorldShape *ws, int x, int y, bool enabled) {
    if (!ws->active || !ws->mask) return;
    if (x < 0 || y < 0 || x >= ws->width || y >= ws->height) return;
    int bit = y * ws->width + x;
    if (enabled)
        ws->mask[bit / 8] |=  (unsigned char)(1u << (bit % 8));
    else
        ws->mask[bit / 8] &= (unsigned char)~(1u << (bit % 8));
}

/* Fill a rectangle within the mask */
static inline void world_shape_fill_rect(WorldShape *ws, int x0, int y0,
                                          int x1, int y1, bool enabled) {
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            world_shape_set(ws, x, y, enabled);
}

/* Activate the shape with all tiles enabled (first-time activation) */
static inline bool world_shape_activate(WorldShape *ws, int w, int h) {
    return world_shape_resize(ws, w, h, true);
}

#endif /* DGE_WORLD_SHAPE_H */
