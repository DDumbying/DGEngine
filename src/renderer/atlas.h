#ifndef DGE_ATLAS_H
#define DGE_ATLAS_H

/*  SpriteAtlas — one GPU texture that packs many named sprites in a
    uniform grid. All sprites are the same pixel dimensions (cell_w x
    cell_h). Lookup is by SpriteId (an integer index into the grid),
    returning a UVRect the renderer uses to sample the right region.

    Ships with a programmatic fallback atlas (atlas_create_fallback)
    so the system works without any image files on disk. When real art
    exists, atlas_load() replaces it transparently — callers only hold
    SpriteIds and never touch raw UVs directly.

    Grid layout (left-to-right, top-to-bottom) of the fallback atlas:
      0: TREE    1: ROCK    2: WORKER
      3: CAMPFIRE_BLUEPRINT   4: CAMPFIRE_COMPLETE
      5: TILE_GRASS (overlay not needed but reserved)
    These five names are just the fallback atlas's own slot labels now
    — see the SPRITE_TREE etc. enum below — not a claim that the
    engine treats slot 0 as meaning "tree". A project's own ObjectDefs
    reference whichever slot their sprite actually lives in; nothing
    in the engine special-cases these five indices anymore (see the
    note further down on sprite_id_for_prefab()'s retirement). */

#include <stdbool.h>
#include "texture.h"

#define ATLAS_MAX_SPRITES 64

typedef int SpriteId;

/* Named sprite ids for the fallback atlas's own five slots — a stable
   name for those five specific indices, nothing more. Not read by any
   engine-layer logic; a project referencing its own imported
   spritesheet has its own slot numbering entirely and never touches
   this enum. */
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

/* Phase 2 (ObjectDef consolidation): sprite_id_for_prefab()/
   sprite_id_for_building() are retired along with PrefabKind/
   BuildingKind. Their retirement also fixes a real layering violation
   this file had: renderer/atlas.h — a renderer-layer header — used to
   #include game/prefabs.h and simulation/construction.h just to spell
   out those two functions' parameter types, meaning the renderer
   reached UP into game-layer types. An engine layer should never need
   to know a game-layer type exists (see ENGINE_DESIGN.md §1's first
   principle) — a project's own ObjectDef already carries its own
   sprite reference (ObjectDef.sprite, resolved via
   sprites_tab_find_id()), so no atlas-side convenience lookup by game
   type was ever structurally necessary; these two were dead code
   (never called outside this file) even before being retired. */

#endif /* DGE_ATLAS_H */
