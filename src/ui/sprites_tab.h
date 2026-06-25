#ifndef DGE_SPRITES_TAB_H
#define DGE_SPRITES_TAB_H

/*  Sprite Manager tab.

    Displays the loaded sprite atlas as a clickable grid of cells.
    Clicking a cell selects it; the inspector on the right lets you
    type a name for that cell.  Names are saved to assets/sprites.meta
    (plain text, one "index=name" per line) and loaded back on open.

    The Object Editor (Phase L) references sprites by name, not index,
    so renaming sprites here won't break object definitions as long as
    the meta file stays in sync.

    Layout (within the content area below TABBAR_H):
      LEFT  — atlas grid (scrollable if large)
      RIGHT — inspector: index, pixel size, name TextInput, Load/Reload btns

    sprites_tab_load_meta() / sprites_tab_save_meta() handle the .meta
    file; sprites_tab_get_name() lets the Object Editor look up names. */

#include <stdbool.h>
#include "../renderer/atlas.h"
#include "textinput.h"

#define SPRITES_META_MAX 256          /* max named sprite slots */
#define SPRITE_NAME_MAX   64

/*  One named entry in sprites.meta. */
typedef struct {
    int  id;
    char name[SPRITE_NAME_MAX];
} SpriteName;

typedef struct {
    /* Current atlas reference (not owned — owned by main.c) */
    SpriteAtlas *atlas;

    /* Selected cell */
    int selected_id;        /* -1 = nothing selected */

    /* Name editing */
    TextInput name_field;
    bool      name_focused;

    /* Name table — parallel to atlas sprite indices */
    SpriteName names[SPRITES_META_MAX];
    int        name_count;   /* valid entries in names[] */

    /* Grid scroll — vertical offset in cells */
    int scroll_cells;

    /* Path field for "Load atlas" */
    TextInput load_path;
    bool      load_path_focused;

    /* One-line status/error message */
    char status[128];
} SpritesTab;

void sprites_tab_init(SpritesTab *st, SpriteAtlas *atlas);

/*  Load  assets/sprites.meta (relative to cwd = project folder).
    Safe to call even if the file doesn't exist yet. */
void sprites_tab_load_meta(SpritesTab *st);

/*  Write assets/sprites.meta.  Called on name change + on Save. */
void sprites_tab_save_meta(const SpritesTab *st);

/*  Look up a sprite name by id.  Returns "" if not named. */
const char *sprites_tab_get_name(const SpritesTab *st, int id);

/*  Look up a sprite id by name.  Returns -1 if not found. */
int sprites_tab_find_id(const SpritesTab *st, const char *name);

/*  Per-frame update and render.  vw/vh are full viewport dimensions. */
void sprites_tab_update(SpritesTab *st, int vw, int vh);
void sprites_tab_render(const SpritesTab *st, int vw, int vh);

#endif /* DGE_SPRITES_TAB_H */
