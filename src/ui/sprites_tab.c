#include "sprites_tab.h"

#include <stdio.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "../platform/input.h"
#include "../renderer/renderer.h"
#include "../core/log.h"
#include "text.h"
#include "tabbar.h"
#include "layout.h"

/* -----------------------------------------------------------------------
   Layout constants (all pixel values, relative to the content area) */

#define INSPECTOR_W   240.0f      /* right inspector panel width */
#define CELL_DRAW_SZ   56.0f      /* each atlas cell drawn at this size */
#define CELL_GAP        4.0f
#define SCROLL_SPEED    3         /* cells per scroll tick */
#define PAD            12.0f
#define BTN_H          26.0f
#define SCALE_LBL      1.5f
#define SCALE_BODY     1.5f
#define SCALE_SMALL    1.3f

/* Content area top edge (below tab bar) */
#define CONTENT_Y  ((float)TABBAR_H)

/* -----------------------------------------------------------------------
   Internal helpers */

static bool hittest(float x, float y, float w, float h, int mx, int my) {
    return (float)mx >= x && (float)mx < x + w &&
           (float)my >= y && (float)my < y + h;
}

static void draw_box_border(float x, float y, float w, float h,
                             float br, float bg, float bb) {
    renderer_draw_quad(x,       y,       w,    1.0f, br, bg, bb, 1.0f);
    renderer_draw_quad(x,       y+h-1,   w,    1.0f, br, bg, bb, 1.0f);
    renderer_draw_quad(x,       y,       1.0f, h,    br, bg, bb, 1.0f);
    renderer_draw_quad(x+w-1,   y,       1.0f, h,    br, bg, bb, 1.0f);
}

/* -----------------------------------------------------------------------
   Meta I/O */

static int find_name_slot(SpritesTab *st, int id) {
    for (int i = 0; i < st->name_count; i++)
        if (st->names[i].id == id) return i;
    return -1;
}

void sprites_tab_load_meta(SpritesTab *st) {
    st->name_count = 0;
    FILE *f = fopen("assets/sprites.meta", "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        /* format: index=name */
        int id;
        char name[SPRITE_NAME_MAX];
        if (sscanf(line, "%d=%63s", &id, name) == 2 &&
            id >= 0 && id < SPRITES_META_MAX &&
            st->name_count < SPRITES_META_MAX) {
            st->names[st->name_count].id = id;
            strncpy(st->names[st->name_count].name, name, SPRITE_NAME_MAX - 1);
            st->name_count++;
        }
    }
    fclose(f);
    LOG_INFO("Sprites meta loaded: %d named sprites.", st->name_count);
}

void sprites_tab_save_meta(const SpritesTab *st) {
    FILE *f = fopen("assets/sprites.meta", "w");
    if (!f) { LOG_WARN("sprites_tab_save_meta: cannot write assets/sprites.meta"); return; }
    for (int i = 0; i < st->name_count; i++)
        fprintf(f, "%d=%s\n", st->names[i].id, st->names[i].name);
    fclose(f);
    LOG_INFO("Sprites meta saved (%d entries).", st->name_count);
}

const char *sprites_tab_get_name(const SpritesTab *st, int id) {
    for (int i = 0; i < st->name_count; i++)
        if (st->names[i].id == id) return st->names[i].name;
    return "";
}

int sprites_tab_find_id(const SpritesTab *st, const char *name) {
    if (st->assets) {
        int id = asset_library_find_id(st->assets, name);
        if (id >= 0) return id;
    }
    for (int i = 0; i < st->name_count; i++)
        if (strncmp(st->names[i].name, name, SPRITE_NAME_MAX) == 0)
            return st->names[i].id;
    return -1;
}

/* -----------------------------------------------------------------------
   Public API */

void sprites_tab_init(SpritesTab *st, SpriteAtlas *atlas, AssetLibrary *assets) {
    memset(st, 0, sizeof(*st));
    st->atlas       = atlas;
    st->assets      = assets;
    st->selected_id = -1;
    textinput_init(&st->name_field,  SPRITE_NAME_MAX - 1, false);
    textinput_init(&st->load_path,   255, false);
    textinput_set (&st->load_path,   "assets/sprites.png");
    textinput_init(&st->import_path, ASSET_PATH_MAX - 1, false);
    textinput_set (&st->import_path, "assets/");
    textinput_init(&st->import_name, ASSET_NAME_MAX - 1, false);
    sprites_tab_load_meta(st);
}

void sprites_tab_update(SpritesTab *st, int vw, int vh) {
    if (!st->atlas) return;

    int mx, my;
    input_mouse_pos(&mx, &my);

    float grid_w = (float)vw - INSPECTOR_W - PAD;
    float grid_x = 0.0f;
    float grid_y = CONTENT_Y + PAD;

    int cols = (int)((grid_w + CELL_GAP) / (CELL_DRAW_SZ + CELL_GAP));
    if (cols < 1) cols = 1;

    /* Scroll with mouse wheel when hovering the grid area */
    if (hittest(grid_x, grid_y, grid_w, (float)vh - grid_y, mx, my)) {
        int sdx, sdy;
        input_mouse_scroll(&sdx, &sdy);
        if (sdy) {
            st->scroll_cells -= sdy * SCROLL_SPEED;
            if (st->scroll_cells < 0) st->scroll_cells = 0;
        }
    }

    /* Click on a cell to select it */
    if (input_mouse_button_pressed(SDL_BUTTON_LEFT)) {
        float gy = grid_y;
        int total = st->atlas->sprite_count;
        int row0  = st->scroll_cells / cols;
        int start = row0 * cols;
        for (int i = start; i < total && gy + CELL_DRAW_SZ < (float)vh; ) {
            float gx = grid_x + PAD;
            for (int c = 0; c < cols && i < total; c++, i++) {
                if (hittest(gx, gy, CELL_DRAW_SZ, CELL_DRAW_SZ, mx, my)) {
                    /* Unfocus name field on the old selection */
                    if (st->name_focused) {
                        textinput_unfocus(&st->name_field);
                        st->name_focused = false;
                    }
                    st->selected_id = i;
                    /* Pre-fill name field with existing name */
                    textinput_set(&st->name_field, sprites_tab_get_name(st, i));
                    goto cell_clicked;
                }
                gx += CELL_DRAW_SZ + CELL_GAP;
            }
            gy += CELL_DRAW_SZ + CELL_GAP;
        }
    }
    cell_clicked:;

    /* Inspector interaction */
        /* UI handled in render */
    /* Import interaction handled in render */
    /* Load interaction handled in render */
    (void)vh;
}

void sprites_tab_render(const SpritesTab *st, int vw, int vh) {
    /* Background */
    renderer_draw_quad(0.0f, CONTENT_Y, (float)vw, (float)vh - CONTENT_Y,
                       0.08f, 0.08f, 0.10f, 1.0f);

    if (!st->atlas) {
        text_draw(PAD, CONTENT_Y + PAD, SCALE_BODY, 0.6f, 0.2f, 0.2f, 1.0f,
                  "NO ATLAS LOADED");
        return;
    }

    int mx, my;
    input_mouse_pos(&mx, &my);

    float grid_w = (float)vw - INSPECTOR_W - PAD;
    float grid_x = 0.0f;
    float grid_y = CONTENT_Y + PAD;

    int cols = (int)((grid_w + CELL_GAP) / (CELL_DRAW_SZ + CELL_GAP));
    if (cols < 1) cols = 1;

    int total = st->atlas->sprite_count;
    int row0  = st->scroll_cells / cols;
    int start = row0 * cols;

    /* Draw atlas grid */
    float gy = grid_y;
    for (int i = start; i < total && gy + CELL_DRAW_SZ < (float)vh; ) {
        float gx = grid_x + PAD;
        for (int c = 0; c < cols && i < total; c++, i++) {
            bool selected = (i == st->selected_id);
            bool hover    = hittest(gx, gy, CELL_DRAW_SZ, CELL_DRAW_SZ, mx, my);

            /* Cell background */
            float cbg = selected ? 0.20f : (hover ? 0.17f : 0.12f);
            renderer_draw_quad(gx, gy, CELL_DRAW_SZ, CELL_DRAW_SZ,
                               cbg, cbg, cbg + 0.03f, 1.0f);

            /* Draw the sprite cell from the atlas using a flat colored
               representation (we draw a colored quad per cell index because
               the atlas texture is drawn via the iso renderer path which
               requires a camera context — here we're in UI mode). */
            UVRect uv = atlas_get_uv(st->atlas, i);
            /* Color-coded preview using UV coords as a hue hint */
            float cr = 0.30f + uv.u0 * 0.50f;
            float cg = 0.45f - uv.v0 * 0.20f;
            float cb = 0.55f + uv.u1 * 0.20f;
            renderer_draw_quad(gx + 4.0f, gy + 4.0f,
                               CELL_DRAW_SZ - 8.0f, CELL_DRAW_SZ - 8.0f,
                               cr, cg, cb, 1.0f);

            /* Index number */
            char idx_buf[16];
            snprintf(idx_buf, sizeof idx_buf, "%d", i);
            text_draw(gx + 3.0f, gy + 3.0f, 1.2f,
                      0.20f, 0.20f, 0.22f, 1.0f, idx_buf);

            /* Name label under the cell */
            const char *name = sprites_tab_get_name(st, i);
            if (name[0]) {
                float nw = text_measure_width(name, 1.1f);
                float nx = gx + (CELL_DRAW_SZ - nw) * 0.5f;
                text_draw(nx, gy + CELL_DRAW_SZ - text_line_height(1.1f) - 1.0f,
                           1.1f, 0.80f, 0.90f, 0.70f, 1.0f, name);
            }

            /* Border */
            float border_r = selected ? 0.30f : (hover ? 0.40f : 0.22f);
            float border_g = selected ? 0.85f : (hover ? 0.60f : 0.22f);
            float border_b = selected ? 0.50f : (hover ? 0.40f : 0.26f);
            draw_box_border(gx, gy, CELL_DRAW_SZ, CELL_DRAW_SZ,
                            border_r, border_g, border_b);

            gx += CELL_DRAW_SZ + CELL_GAP;
        }
        gy += CELL_DRAW_SZ + CELL_GAP;
    }

    /* Vertical divider between grid and inspector */
    float insp_x = (float)vw - INSPECTOR_W;
    renderer_draw_quad(insp_x - 1.0f, CONTENT_Y, 1.0f, (float)vh - CONTENT_Y,
                       0.22f, 0.22f, 0.28f, 1.0f);

    /* Inspector panel */
    renderer_draw_quad(insp_x, CONTENT_Y, INSPECTOR_W, (float)vh - CONTENT_Y,
                       0.09f, 0.09f, 0.11f, 1.0f);

    UILayout l;
    ui_layout_begin(&l, insp_x + PAD, CONTENT_Y + PAD, INSPECTOR_W - PAD * 2.0f);

    if (st->selected_id < 0) {
        ui_layout_label(&l, "SELECT A CELL", false);
    } else {
        /* Selected cell info */
        char info[64];
        snprintf(info, sizeof info, "CELL %d", st->selected_id);
        ui_layout_label(&l, info, false);

        char dim[64];
        snprintf(dim, sizeof dim, "%dx%d PX", st->atlas->cell_w, st->atlas->cell_h);
        ui_layout_label(&l, dim, true);
        ui_layout_gap(&l, 10.0f);

        /* Name field */
        ui_layout_label(&l, "NAME", true);
        
        bool enter_pressed = false;
        if (ui_layout_field(&l, &((SpritesTab*)st)->name_field, 0, 24.0f, st->name_focused, mx, my, &enter_pressed)) {
            if (!st->name_focused) {
                ((SpritesTab*)st)->name_focused = true;
                textinput_focus(&((SpritesTab*)st)->name_field);
                if (st->import_path_focused) { textinput_unfocus(&((SpritesTab*)st)->import_path); ((SpritesTab*)st)->import_path_focused = false; }
                if (st->import_name_focused) { textinput_unfocus(&((SpritesTab*)st)->import_name); ((SpritesTab*)st)->import_name_focused = false; }
                if (st->load_path_focused)   { textinput_unfocus(&((SpritesTab*)st)->load_path);   ((SpritesTab*)st)->load_path_focused = false; }
            }
        } else if (input_mouse_button_pressed(SDL_BUTTON_LEFT) && !hittest(insp_x + PAD, l.cursor_y - 28.0f, INSPECTOR_W, 28.0f, mx, my)) {
            // Unfocus logic is handled implicitly or on click elsewhere
        }

        ui_layout_gap(&l, 4.0f);
        ui_layout_label(&l, "CLICK FIELD, TYPE, ENTER", true);
        ui_layout_gap(&l, 4.0f);

        /* Save button */
        if (ui_layout_button(&l, "SAVE", 80.0f, BTN_H, false, mx, my) || enter_pressed) {
            const char *new_name = textinput_get(&st->name_field);
            int slot = find_name_slot((SpritesTab*)st, st->selected_id);
            if (new_name[0]) {
                if (slot < 0 && st->name_count < SPRITES_META_MAX) {
                    slot = ((SpritesTab*)st)->name_count++;
                    ((SpritesTab*)st)->names[slot].id = st->selected_id;
                }
                if (slot >= 0)
                    strncpy(((SpritesTab*)st)->names[slot].name, new_name, SPRITE_NAME_MAX - 1);
            } else if (slot >= 0) {
                ((SpritesTab*)st)->names[slot] = st->names[--((SpritesTab*)st)->name_count];
            }
            sprites_tab_save_meta(st);
            if (enter_pressed) {
                textinput_unfocus(&((SpritesTab*)st)->name_field);
                ((SpritesTab*)st)->name_focused = false;
            }
            snprintf(((SpritesTab*)st)->status, sizeof st->status, "SAVED.");
        }
    }

    /* Status message */
    if (st->status[0]) {
        float sw = text_measure_width(st->status, SCALE_SMALL);
        text_draw(insp_x + (INSPECTOR_W - sw) * 0.5f,
                  (float)vh - PAD - text_line_height(SCALE_SMALL) - BTN_H - 40.0f,
                  SCALE_SMALL, 0.30f, 0.85f, 0.48f, 1.0f, st->status);
    }

    ui_layout_gap(&l, 20.0f);
    ui_layout_label(&l, "IMPORT IMAGE (ANY SIZE, ANY PNG)", true);

    ui_layout_label(&l, "IMAGE PATH", true);
    if (ui_layout_field(&l, &((SpritesTab*)st)->import_path, 0, 24.0f, st->import_path_focused, mx, my, NULL)) {
        if (!st->import_path_focused) {
            ((SpritesTab*)st)->import_path_focused = true;
            textinput_focus(&((SpritesTab*)st)->import_path);
            if (st->name_focused)        { textinput_unfocus(&((SpritesTab*)st)->name_field);  ((SpritesTab*)st)->name_focused = false; }
            if (st->import_name_focused) { textinput_unfocus(&((SpritesTab*)st)->import_name); ((SpritesTab*)st)->import_name_focused = false; }
            if (st->load_path_focused)   { textinput_unfocus(&((SpritesTab*)st)->load_path);   ((SpritesTab*)st)->load_path_focused = false; }
        }
    }
    
    ui_layout_label(&l, "SPRITE NAME", true);
    if (ui_layout_field(&l, &((SpritesTab*)st)->import_name, 0, 24.0f, st->import_name_focused, mx, my, NULL)) {
        if (!st->import_name_focused) {
            ((SpritesTab*)st)->import_name_focused = true;
            textinput_focus(&((SpritesTab*)st)->import_name);
            if (st->name_focused)        { textinput_unfocus(&((SpritesTab*)st)->name_field);  ((SpritesTab*)st)->name_focused = false; }
            if (st->import_path_focused) { textinput_unfocus(&((SpritesTab*)st)->import_path); ((SpritesTab*)st)->import_path_focused = false; }
            if (st->load_path_focused)   { textinput_unfocus(&((SpritesTab*)st)->load_path);   ((SpritesTab*)st)->load_path_focused = false; }
        }
    }

    if (ui_layout_button(&l, "IMPORT", 0, BTN_H, false, mx, my)) {
        const char *path = textinput_get(&st->import_path);
        const char *name = textinput_get(&st->import_name);
        if (!name[0]) {
            snprintf(((SpritesTab*)st)->status, sizeof st->status, "NAME REQUIRED.");
        } else if (st->assets && asset_library_import(st->assets, path, name) >= 0) {
            asset_library_save_meta(st->assets);
            snprintf(((SpritesTab*)st)->status, sizeof st->status, "IMPORTED '%s'.", name);
        } else {
            snprintf(((SpritesTab*)st)->status, sizeof st->status, "IMPORT FAILED -- CHECK PATH.");
        }
    }

    if (st->assets && st->assets->count > 0) {
        char buf[48];
        snprintf(buf, sizeof buf, "%d IMAGE(S) IMPORTED", st->assets->count);
        ui_layout_label(&l, buf, true);
    }

    ui_layout_gap(&l, 20.0f);
    ui_layout_label(&l, "ATLAS PATH", true);
    if (ui_layout_field(&l, &((SpritesTab*)st)->load_path, 0, 24.0f, st->load_path_focused, mx, my, NULL)) {
        if (!st->load_path_focused) {
            ((SpritesTab*)st)->load_path_focused = true;
            textinput_focus(&((SpritesTab*)st)->load_path);
            if (st->name_focused)        { textinput_unfocus(&((SpritesTab*)st)->name_field);  ((SpritesTab*)st)->name_focused = false; }
            if (st->import_name_focused) { textinput_unfocus(&((SpritesTab*)st)->import_name); ((SpritesTab*)st)->import_name_focused = false; }
            if (st->import_path_focused) { textinput_unfocus(&((SpritesTab*)st)->import_path); ((SpritesTab*)st)->import_path_focused = false; }
        }
    }

    if (ui_layout_button(&l, "RELOAD ATLAS", 0, BTN_H, false, mx, my)) {
        // Handled in main loop or panel
    }
}
