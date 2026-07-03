#ifndef DGE_TILE_H
#define DGE_TILE_H

/*  A single grid cell.

    `type` used to be a TerrainType enum (grass/dirt/sand/water/stone,
    hardcoded into the engine). It's now a plain index into the active
    project's Tileset (see tileset.h) — the engine stores "which slot",
    the project's data decides what that slot means, looks like, and
    whether it's walkable.

    -1 is a valid value here (e.g. on a freshly created World whose
    Tileset is still empty) and means "undefined" — world_render() draws
    it as a missing-texture checker, tileset_is_walkable() treats it as
    not walkable. There's no engine-level default terrain anymore. */

#include <stdbool.h>

typedef struct {
    int   type;             /* Tileset slot index, or -1 = undefined */
    float height;           /* reserved for terrain layers / elevation */
    unsigned char variant;  /* 0-3, picks a shading jitter so flat color
                                fields don't look uniform once a sprite
                                or fallback color is applied */
} Tile;

typedef struct {
    float r, g, b;
} TileColor;

#endif /* DGE_TILE_H */
