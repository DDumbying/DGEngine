#ifndef DGE_SPRITES_TAB_H
#define DGE_SPRITES_TAB_H

/*  Sprite Manager tab.

    Layout (within the content area below TABBAR_H):
      LEFT  — Two sections:
               1. "YOUR SPRITES" — imported assets (always visible, always on top)
               2. "ATLAS SPRITES (PLACEHOLDER)" — collapsible atlas grid
      RIGHT — Inspector: selected info, name field, import section

    The separation is intentional: imported assets are the primary workflow
    (user's own art); the placeholder atlas is a fallback during development. */

#include <stdbool.h>
#include "../renderer/atlas.h"
#include "../renderer/asset_library.h"
#include "textinput.h"

#define SPRITES_META_MAX 256
#define SPRITE_NAME_MAX   64

typedef struct {
    int  id;
    char name[SPRITE_NAME_MAX];
} SpriteName;

typedef struct {
    /* Atlas reference (not owned) */
    SpriteAtlas *atlas;

    /* Imported asset library (not owned) */
    AssetLibrary *assets;

    /* Selected cell — can be atlas index (0..N-1) or ASSET_ID_BASE+i for imported */
    int selected_id;

    /* Name editing */
    TextInput name_field;
    bool      name_focused;

    /* Name table for atlas cells */
    SpriteName names[SPRITES_META_MAX];
    int        name_count;

    /* Grid scroll for atlas section */
    int scroll_cells;

    /* Imported assets scroll (separate list) */
    int import_scroll;

    /* Atlas section collapsed/expanded */
    bool show_atlas_section;

    /* Path fields */
    TextInput load_path;
    bool      load_path_focused;

    TextInput import_path;
    bool      import_path_focused;
    TextInput import_name;
    bool      import_name_focused;

    /* Status/error message */
    char status[128];
} SpritesTab;

void sprites_tab_init(SpritesTab *st, SpriteAtlas *atlas, AssetLibrary *assets);

void sprites_tab_load_meta(SpritesTab *st);
void sprites_tab_save_meta(const SpritesTab *st);

const char *sprites_tab_get_name(const SpritesTab *st, int id);
int sprites_tab_find_id(const SpritesTab *st, const char *name);

void sprites_tab_update(SpritesTab *st, int vw, int vh);
void sprites_tab_render(const SpritesTab *st, int vw, int vh);

#endif /* DGE_SPRITES_TAB_H */
