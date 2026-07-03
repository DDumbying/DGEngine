#ifndef DGE_WORLD_H
#define DGE_WORLD_H

#include <stdbool.h>
#include "tile.h"
#include "tileset.h"
#include "world_shape.h"
#include "../renderer/atlas.h"

/*  Flat tile-grid world. */
typedef struct {
    int          width;
    int          height;
    Tile        *tiles;       /* width * height, row-major */
    Tileset      tileset;     /* project-defined terrain slots, see tileset.h */
    WorldShape   shape;       /* freeform playable-area mask; inactive = full rect */
} World;

/* Allocates the tile array. Every tile starts with type=-1 (undefined —
   see tile.h) until the project's Tileset has at least one slot and
   world_clear()/painting assigns it. There is no engine-level default
   terrain to fall back to. */
bool world_create(World *w, int width, int height);
void world_destroy(World *w);

/* Bounds-checked accessors. get returns NULL outside the map. */
const Tile *world_get_tile(const World *w, int x, int y);
Tile       *world_get_tile_mut(World *w, int x, int y);
void        world_set_tile(World *w, int x, int y, int tileset_slot);

/* True if (x,y) is in-bounds AND part of the playable area (world_shape
   inactive -> every in-bounds tile counts). Paint/place/pathfinding
   should all gate on this rather than world_get_tile() alone once
   shape editing is in play, so a "hole" tile behaves like it's not
   part of the map even though the underlying Tile storage still
   exists there. */
bool world_tile_playable(const World *w, int x, int y);

/* The only "meaning" the engine needs from a tile: can something walk
   on it. Looks the type up in w's own Tileset, so callers never need
   to know Tileset exists just to ask "can I path through here". */
bool world_tile_walkable(const World *w, const Tile *t);

/* Resets every tile to type=-1 (undefined), variant 0 — blank
   authoring canvas. Does NOT pick a Tileset slot on the project's
   behalf; if the project wants every tile to start as some particular
   slot, that's a PAINT-mode action, not something world_clear() should
   silently decide. */
void world_clear(World *w);

/* Resize to new_w x new_h, preserving tiles that fit in the new bounds,
   filling any new area with type=-1 (undefined), matching world_clear()'s
   "don't presume a default" reasoning. Existing tile pointer is freed
   and reallocated; returns false on allocation failure (world left
   untouched in that case). */
bool world_resize(World *w, int new_w, int new_h);

/* "Regenerate" button: scatters the project's own Tileset slots across
   the grid using blobby noise, so a project with more than one tile
   type has something to look at rather than a flat undefined canvas.
   Distributes proportionally across however many slots exist — the
   engine doesn't know what any slot "means", so it can't decide "high
   noise should be the 3rd slot", it just spreads all of them out in
   smooth patches. No-op (with a warning) if the Tileset is still
   empty — nothing to distribute yet. See world.c for the full
   reasoning (this replaced a version that hardcoded 5 named terrain
   bands). */
void world_generate(World *w, unsigned int seed);

/* Draws every tile: a real sprite if its Tileset slot has one assigned,
   the slot's project-chosen... actually no per-slot color exists
   anymore (see tileset.h) — an unassigned sprite, or an undefined tile
   (type == -1), draws as a missing-texture checker pattern instead of
   a guessed color. */
void world_render(const World *w, const SpriteAtlas *atlas);

/* Binary save/load. Returns false on I/O or format error (load leaves
   the world untouched on failure). */
bool world_save(const World *w, const char *path);
bool world_load(World *w, const char *path);

#endif /* DGE_WORLD_H */
