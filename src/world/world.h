#ifndef DGE_WORLD_H
#define DGE_WORLD_H

#include <stdbool.h>
#include "tile.h"

/*  Flat tile-grid world.
    No chunking yet — Phase 7 introduces chunk streaming for large/infinite
    worlds. For now the whole map lives in one contiguous array, which is
    plenty for anything up to a few hundred tiles per side. */
typedef struct {
    int width;
    int height;
    Tile *tiles; /* width * height, row-major: index = y * width + x */
} World;

/* Allocates the tile array (zeroed -> all TERRAIN_GRASS, variant 0). */
bool world_create(World *w, int width, int height);
void world_destroy(World *w);

/* Bounds-checked accessors. get returns NULL outside the map. */
const Tile *world_get_tile(const World *w, int x, int y);
Tile       *world_get_tile_mut(World *w, int x, int y);
void        world_set_tile(World *w, int x, int y, TerrainType type);

/* Procedural terrain fill. Deterministic for a given seed — same seed
   always produces the same map, which matters once save files exist. */
void world_generate(World *w, unsigned int seed);

/* Draws every tile via renderer_draw_iso_tile, colored by terrain type
   with its per-tile variant shading applied. */
void world_render(const World *w);

/* Binary save/load. Returns false on I/O or format error (load leaves
   the world untouched on failure). */
bool world_save(const World *w, const char *path);
bool world_load(World *w, const char *path);

#endif /* DGE_WORLD_H */
