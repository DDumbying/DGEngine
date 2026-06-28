#ifndef DGE_ASSET_LIBRARY_H
#define DGE_ASSET_LIBRARY_H

#include <stdbool.h>
#include "texture.h"

/*  Asset import overhaul (the gap from the architecture discussion):
    SpriteAtlas (atlas.h) is a uniform grid — every cell the same
    pixel size, one shared texture. That's the right shape for hand-
    packed sprite sheets, but it can't be "drop in any PNG you drew,
    any size" — a 200x140 background doesn't fit a 32x32 grid cell.

    AssetLibrary is the sibling for that case: each imported image
    gets its own GL texture (no forced grid, no shared sheet) and a
    name. It deliberately does NOT replace SpriteAtlas — a project can
    use a packed grid sheet for small repeated sprites (trees, rocks)
    *and* import arbitrary one-off images (a custom character, a UI
    background) side by side. Sprites tab (ui/sprites_tab.h) is the
    one place that has to know both exist; everything downstream (an
    ObjectDef's `sprite` property, RenderableComponent.sprite_id,
    system_render_entities) just holds an int and doesn't care which
    table it came from.

    That "doesn't care which table" property is what ASSET_ID_BASE is
    for: SpriteAtlas ids are small (0..ATLAS_MAX_SPRITES-1, see
    atlas.h); AssetLibrary ids are ASSET_ID_BASE+index, a disjoint
    range with a huge gap between them. Any function that already
    takes a "sprite_id" (RenderableComponent, save/load, objdef
    resolution) keeps working completely unchanged — it was always
    "just an int", and the int's meaning was always "ask the renderer/
    atlas layer to resolve it", never something callers pattern-matched
    on themselves. The only two places that needed to learn the new
    range are asset_library_is_asset_id()'s callers: system_render_entities
    (ecs/systems.c) and sprites_tab.c's name resolution. */

#define ASSET_LIBRARY_MAX 256
#define ASSET_ID_BASE      100000
#define ASSET_NAME_MAX     64
#define ASSET_PATH_MAX     256

typedef struct {
    char    name[ASSET_NAME_MAX];
    char    path[ASSET_PATH_MAX]; /* relative to the project folder, for assets.meta */
    Texture tex;
    bool    loaded;
} ImportedAsset;

typedef struct {
    ImportedAsset assets[ASSET_LIBRARY_MAX];
    int           count;
} AssetLibrary;

void asset_library_init(AssetLibrary *lib);
void asset_library_destroy(AssetLibrary *lib);

/*  Loads the image at path and registers it under name (overwriting
    any existing asset with that name -- re-importing is how you
    update one). Returns the sprite_id to store wherever a sprite_id
    is expected (RenderableComponent.sprite_id, etc.), or -1 on failure
    (bad path, decode failure, or the library is full). */
int asset_library_import(AssetLibrary *lib, const char *path, const char *name);

/* Removes a named asset (frees its GPU texture). */
void asset_library_remove(AssetLibrary *lib, const char *name);

/* -1 if no asset has this name. */
int asset_library_find_id(const AssetLibrary *lib, const char *name);
const char *asset_library_get_name(const AssetLibrary *lib, int sprite_id);

bool asset_library_is_asset_id(int sprite_id);
/* NULL if sprite_id isn't a valid, loaded asset id. */
const Texture *asset_library_get_texture(const AssetLibrary *lib, int sprite_id);

/*  assets/imported.meta -- "name=path" lines, one per imported asset,
    plain text like sprites.meta. Saved after every import/remove so a
    reopened project re-imports automatically; load is safe to call on
    a project that has never imported anything (no file = no entries). */
void asset_library_save_meta(const AssetLibrary *lib);
void asset_library_load_meta(AssetLibrary *lib);

#endif /* DGE_ASSET_LIBRARY_H */
