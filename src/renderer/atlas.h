#ifndef DGE_ATLAS_H
#define DGE_ATLAS_H

/*  SpriteAtlas — one GPU texture that packs many named sprites in a
    uniform grid. All sprites are the same pixel dimensions (cell_w x
    cell_h). Lookup is by SpriteId (an integer index into the grid),
    returning a UVRect the renderer uses to sample the right region.

    Phase E ships with a programmatic fallback atlas (atlas_create_fallback)
    so the system works without any image files on disk. When real art
    exists, atlas_load() replaces it transparently — callers only hold
    SpriteIds and never touch raw UVs directly.

    Grid layout (left-to-right, top-to-bottom):
      0: TREE    1: ROCK    2: WORKER
      3: CAMPFIRE_BLUEPRINT   4: CAMPFIRE_COMPLETE
      5: TILE_GRASS (overlay not needed but reserved)
    More slots added as Phase F's data-driven objects land.           */

#include <stdbool.h>
#include "texture.h"

#define ATLAS_MAX_SPRITES 64

typedef int SpriteId;

/* Named sprite ids — gives callers a stable name instead of a magic int.
   Kept here (not in prefabs.h) because the atlas is renderer-layer;
   game-layer code that already has a PrefabKind/BuildingKind goes
   through sprite_id_for_prefab() / sprite_id_for_building() below.  */
typedef enum {
    SPRITE_TREE               = 0,
    SPRITE_ROCK               = 1,
    SPRITE_WORKER             = 2,
    SPRITE_CAMPFIRE_BLUEPRINT = 3,
    SPRITE_CAMPFIRE_COMPLETE  = 4,
    SPRITE_NONE               = -1,
} SpriteIdEnum;

typedef struct {
    float u0, v0;   /* top-left  UV (0..1) */
    float u1, v1;   /* bottom-right UV     */
} UVRect;

typedef struct {
    Texture  texture;
    UVRect   sprites[ATLAS_MAX_SPRITES];
    int      sprite_count;
    int      cell_w, cell_h;   /* pixels per sprite cell */
    int      cols;             /* how many cells across  */
} SpriteAtlas;

/* Build a purely programmatic atlas — solid-color cells that represent
   each sprite type. No file I/O. Runs even without an assets/ folder.
   Returns false only on GL allocation failure.                        */
bool atlas_create_fallback(SpriteAtlas *a);

/* Load a real PNG sprite sheet from disk. cell_w x cell_h defines how
   to slice it; cols is how many cells fit horizontally.
   Falls back to atlas_create_fallback on load failure so callers
   never need to special-case a missing file.                          */
bool atlas_load(SpriteAtlas *a, const char *path, int cell_w, int cell_h, int cols);

void atlas_destroy(SpriteAtlas *a);

/* Look up a sprite's UV rect by id. Returns a 0,0,1,1 full-texture
   rect if the id is out of range (safe degradation, not a crash).    */
UVRect atlas_get_uv(const SpriteAtlas *a, SpriteId id);

/* Convenience: translate game-layer kinds to sprite ids.
   Returns SPRITE_NONE if no sprite is registered for that kind.     */
#include "../game/prefabs.h"
#include "../simulation/construction.h"
SpriteId sprite_id_for_prefab(PrefabKind k);
SpriteId sprite_id_for_building(BuildingKind k, bool complete);

#endif /* DGE_ATLAS_H */
