#include "sprites_tab.h"

#include <stdio.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "../platform/input.h"
#include "../renderer/renderer.h"
#include "../core/log.h"
#include "text.h"
#include "tabbar.h"

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

static bool draw_button(float x, float y, float w, float h,
                         const char *label, int mx, int my) {
    bool hover = hittest(x, y, w, h, mx, my);
    bool click = hover && input_mouse_button_pressed(SDL_BUTTON_LEFT);
    float bg = hover ? 0.22f : 0.14f;
    renderer_draw_quad(x, y, w, h, bg, bg, bg + 0.03f, 1.0f);
    draw_box_border(x, y, w, h, 0.30f, 0.30f, 0.36f);
    float tw = text_measure_width(label, SCALE_BODY);
    float th = text_line_height(SCALE_BODY);
    text_draw(x + (w - tw) * 0.5f, y + (h - th) * 0.5f,
              SCALE_BODY, 0.90f, 0.90f, 0.90f, 1.0f, label);
    return click;
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
    for (int i = 0; i < st->name_count; i++)
        if (strncmp(st->names[i].name, name, SPRITE_NAME_MAX) == 0)
            return st->names[i].id;
    return -1;
}

/* -----------------------------------------------------------------------
   Public API */

void sprites_tab_init(SpritesTab *st, SpriteAtlas *atlas) {
    memset(st, 0, sizeof(*st));
    st->atlas       = atlas;
    st->selected_id = -1;
    textinput_init(&st->name_field,  SPRITE_NAME_MAX - 1, false);
    textinput_init(&st->load_path,   255, false);
    textinput_set (&st->load_path,   "assets/sprites.png");
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
    float insp_x = (float)vw - INSPECTOR_W;
    float iy = grid_y;

    if (st->selected_id >= 0) {
        /* Name field */
        float field_y = iy + text_line_height(SCALE_LBL) + 4.0f + text_line_height(SCALE_SMALL) + 4.0f;
        float field_w = INSPECTOR_W - PAD * 2.0f;
        bool field_hover = hittest(insp_x + PAD, field_y, field_w, 24.0f, mx, my);

        if (field_hover && input_mouse_button_pressed(SDL_BUTTON_LEFT)) {
            if (!st->name_focused) {
                st->name_focused = true;
                textinput_focus(&st->name_field);
            }
        } else if (!field_hover && input_mouse_button_pressed(SDL_BUTTON_LEFT)) {
            if (st->name_focused) {
                textinput_unfocus(&st->name_field);
                st->name_focused = false;
            }
        }

        if (st->name_focused) {
            bool enter = textinput_update(&st->name_field, insp_x + PAD + 4.0f,
                                          field_y + 4.0f, field_w - 8.0f, SCALE_BODY);
            if (enter) {
                /* Commit the name */
                const char *new_name = textinput_get(&st->name_field);
                int slot = find_name_slot(st, st->selected_id);
                if (new_name[0]) {
                    if (slot < 0 && st->name_count < SPRITES_META_MAX) {
                        slot = st->name_count++;
                        st->names[slot].id = st->selected_id;
                    }
                    if (slot >= 0)
                        strncpy(st->names[slot].name, new_name, SPRITE_NAME_MAX - 1);
                } else {
                    /* Empty name → remove the entry */
                    if (slot >= 0) {
                        st->names[slot] = st->names[--st->name_count];
                    }
                }
                sprites_tab_save_meta(st);
                textinput_unfocus(&st->name_field);
                st->name_focused = false;
                snprintf(st->status, sizeof st->status, "SAVED.");
            }
        }

        /* Save button */
        float btn_y = field_y + 28.0f + 8.0f;
        if (draw_button(insp_x + PAD, btn_y, 80.0f, BTN_H, "SAVE", mx, my)) {
            const char *new_name = textinput_get(&st->name_field);
            int slot = find_name_slot(st, st->selected_id);
            if (new_name[0]) {
                if (slot < 0 && st->name_count < SPRITES_META_MAX) {
                    slot = st->name_count++;
                    st->names[slot].id = st->selected_id;
                }
                if (slot >= 0)
                    strncpy(st->names[slot].name, new_name, SPRITE_NAME_MAX - 1);
            } else if (slot >= 0) {
                st->names[slot] = st->names[--st->name_count];
            }
            sprites_tab_save_meta(st);
            snprintf(st->status, sizeof st->status, "SAVED.");
        }
    }

    /* Load path field — at the bottom of the inspector */
    float load_y = (float)vh - PAD - BTN_H - 8.0f - 24.0f - PAD;
    bool lp_hover = hittest(insp_x + PAD, load_y, INSPECTOR_W - PAD * 2.0f, 24.0f, mx, my);
    if (lp_hover && input_mouse_button_pressed(SDL_BUTTON_LEFT)) {
        if (!st->load_path_focused) {
            if (st->name_focused) { textinput_unfocus(&st->name_field); st->name_focused = false; }
            st->load_path_focused = true;
            textinput_focus(&st->load_path);
        }
    } else if (!lp_hover && input_mouse_button_pressed(SDL_BUTTON_LEFT)) {
        if (st->load_path_focused) {
            textinput_unfocus(&st->load_path);
            st->load_path_focused = false;
        }
    }
    if (st->load_path_focused)
        textinput_update(&st->load_path, insp_x + PAD + 4.0f, load_y + 4.0f,
                          INSPECTOR_W - PAD * 2.0f - 8.0f, SCALE_BODY);

    float reload_y = (float)vh - PAD - BTN_H;
    (void)reload_y;
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

    float iy = CONTENT_Y + PAD;

    if (st->selected_id < 0) {
        text_draw(insp_x + PAD, iy, SCALE_BODY, 0.40f, 0.40f, 0.45f, 1.0f,
                  "SELECT A CELL");
    } else {
        /* Selected cell info */
        char info[64];
        snprintf(info, sizeof info, "CELL %d", st->selected_id);
        text_draw(insp_x + PAD, iy, SCALE_LBL, 0.85f, 0.85f, 0.90f, 1.0f, info);
        iy += text_line_height(SCALE_LBL) + 4.0f;

        char dim[64];
        snprintf(dim, sizeof dim, "%dx%d PX", st->atlas->cell_w, st->atlas->cell_h);
        text_draw(insp_x + PAD, iy, SCALE_SMALL, 0.50f, 0.50f, 0.55f, 1.0f, dim);
        iy += text_line_height(SCALE_SMALL) + 4.0f;

        /* Name field */
        text_draw(insp_x + PAD, iy, SCALE_SMALL, 0.52f, 0.52f, 0.55f, 1.0f, "NAME");
        iy += text_line_height(SCALE_SMALL) + 2.0f;

        float field_w = INSPECTOR_W - PAD * 2.0f;
        float fbg = st->name_focused ? 0.17f : 0.11f;
        renderer_draw_quad(insp_x + PAD, iy, field_w, 24.0f,
                           fbg, fbg, fbg, 1.0f);
        draw_box_border(insp_x + PAD, iy, field_w, 24.0f,
                        st->name_focused ? 0.30f : 0.22f,
                        st->name_focused ? 0.75f : 0.30f,
                        st->name_focused ? 0.45f : 0.22f);
        float th = text_line_height(SCALE_BODY);
        textinput_render(&st->name_field,
                          insp_x + PAD + 4.0f, iy + (24.0f - th) * 0.5f,
                          SCALE_BODY, 1.0f, 1.0f, 1.0f, 1.0f);
        iy += 28.0f;

        /* Hint */
        text_draw(insp_x + PAD, iy + 2.0f, 1.1f, 0.35f, 0.35f, 0.40f, 1.0f,
                  "CLICK FIELD, TYPE, ENTER");
        iy += text_line_height(1.1f) + 4.0f;

        /* Save button */
        draw_button(insp_x + PAD, iy, 80.0f, BTN_H, "SAVE", mx, my);
    }

    /* Status message */
    if (st->status[0]) {
        float sw = text_measure_width(st->status, SCALE_SMALL);
        text_draw(insp_x + (INSPECTOR_W - sw) * 0.5f,
                  (float)vh - PAD - text_line_height(SCALE_SMALL) - BTN_H - 40.0f,
                  SCALE_SMALL, 0.30f, 0.85f, 0.48f, 1.0f, st->status);
    }

    /* Load atlas section at bottom of inspector */
    float load_bottom_y = (float)vh - PAD - BTN_H;
    float load_label_y  = load_bottom_y - 24.0f - 4.0f - text_line_height(SCALE_SMALL) - 4.0f;
    text_draw(insp_x + PAD, load_label_y, SCALE_SMALL,
              0.52f, 0.52f, 0.55f, 1.0f, "ATLAS PATH");
    float load_field_y = load_label_y + text_line_height(SCALE_SMALL) + 4.0f;
    float field_w = INSPECTOR_W - PAD * 2.0f;
    float fbg2 = st->load_path_focused ? 0.17f : 0.11f;
    renderer_draw_quad(insp_x + PAD, load_field_y, field_w, 24.0f,
                       fbg2, fbg2, fbg2, 1.0f);
    draw_box_border(insp_x + PAD, load_field_y, field_w, 24.0f,
                    st->load_path_focused ? 0.30f : 0.22f,
                    st->load_path_focused ? 0.75f : 0.30f,
                    st->load_path_focused ? 0.45f : 0.22f);
    float th2 = text_line_height(SCALE_BODY);
    textinput_render(&st->load_path,
                      insp_x + PAD + 4.0f, load_field_y + (24.0f - th2) * 0.5f,
                      SCALE_BODY, 1.0f, 1.0f, 1.0f, 1.0f);

    draw_button(insp_x + PAD, load_bottom_y,
                INSPECTOR_W - PAD * 2.0f, BTN_H, "RELOAD ATLAS", mx, my);
}
