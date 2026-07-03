#include "sprites_tab.h"
#include "../platform/filepicker.h"

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
   Layout constants */

#define INSPECTOR_W   260.0f
#define CELL_DRAW_SZ   56.0f
#define CELL_GAP        4.0f
#define SCROLL_SPEED    3
#define PAD            12.0f
#define BTN_H          26.0f
#define SCALE_LBL      1.5f
#define SCALE_BODY     1.5f
#define SCALE_SMALL    1.3f
#define SECTION_H      22.0f   /* section header bar height */

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

/* Draw a section header bar (dark band with label) */
static void draw_section_header(float x, float y, float w, const char *label) {
    renderer_draw_quad(x, y, w, SECTION_H, 0.12f, 0.14f, 0.16f, 1.0f);
    renderer_draw_quad(x, y + SECTION_H - 1.0f, w, 1.0f, 0.28f, 0.55f, 0.38f, 1.0f);
    float th = text_line_height(SCALE_SMALL);
    text_draw(x + 8.0f, y + (SECTION_H - th) * 0.5f,
              SCALE_SMALL, 0.30f, 0.78f, 0.48f, 1.0f, label);
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
        int id;
        char name[SPRITE_NAME_MAX];
        if (sscanf(line, "%d=%63s", &id, name) == 2 &&
            id >= 0 && id < SPRITES_META_MAX &&
            st->name_count < SPRITES_META_MAX) {
            st->names[st->name_count].id = id;
            snprintf(st->names[st->name_count].name, SPRITE_NAME_MAX, "%s", name);
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
    st->show_atlas_section = true;   /* collapsed by default, user can expand */
    textinput_init(&st->name_field,  SPRITE_NAME_MAX - 1, false);
    textinput_init(&st->load_path,   255, false);
    textinput_set (&st->load_path,   "assets/sprites.png");
    textinput_init(&st->import_path, ASSET_PATH_MAX - 1, false);
    textinput_set (&st->import_path, "assets/");
    textinput_init(&st->import_name, ASSET_NAME_MAX - 1, false);
    sprites_tab_load_meta(st);
}

/* -----------------------------------------------------------------------
   Grid area: renders a set of sprite cells starting at (gx, gy),
   returning how many pixels of height were consumed. */

static float render_imported_assets_grid(const SpritesTab *st,
                                          float grid_x, float grid_y,
                                          float grid_w, float avail_h,
                                          int cols, int mx, int my,
                                          int scroll_offset) {
    (void)grid_w;  /* width available but not needed — cells wrap by cols count */
    if (!st->assets || st->assets->count == 0) {
        float th = text_line_height(SCALE_SMALL);
        text_draw(grid_x + PAD, grid_y + PAD, SCALE_SMALL,
                  0.40f, 0.40f, 0.42f, 1.0f,
                  "NO IMPORTED SPRITES YET -- USE IMPORT IN INSPECTOR");
        return PAD + th + PAD;
    }

    int total = st->assets->count;
    int row0  = scroll_offset / cols;
    int start = row0 * cols;

    /* First pass: backgrounds + sprites */
    float gy = grid_y;
    for (int i = start; i < total && gy + CELL_DRAW_SZ < grid_y + avail_h; ) {
        float gx = grid_x + PAD;
        for (int c = 0; c < cols && i < total; c++, i++) {
            int sprite_id = ASSET_ID_BASE + i;
            bool selected = (st->selected_id == sprite_id);
            bool hover    = hittest(gx, gy, CELL_DRAW_SZ, CELL_DRAW_SZ, mx, my);

            float cbg = selected ? 0.22f : (hover ? 0.17f : 0.12f);
            renderer_draw_quad(gx, gy, CELL_DRAW_SZ, CELL_DRAW_SZ,
                               cbg, cbg, cbg + 0.04f, 1.0f);

            /* Draw the actual imported texture */
            const Texture *tex = asset_library_get_texture(st->assets, sprite_id);
            if (tex && tex->id) {
                renderer_bind_texture(tex->id);
                float pad = 4.0f;
                renderer_draw_quad_uv(gx + pad, gy + pad,
                                      CELL_DRAW_SZ - pad * 2.0f, CELL_DRAW_SZ - pad * 2.0f,
                                      1.0f, 1.0f, 1.0f, 1.0f,
                                      0.0f, 0.0f, 1.0f, 1.0f);
                renderer_flush_texture();
            }

            gx += CELL_DRAW_SZ + CELL_GAP;
        }
        gy += CELL_DRAW_SZ + CELL_GAP;
    }

    /* Second pass: borders and name labels */
    gy = grid_y;
    for (int i = start; i < total && gy + CELL_DRAW_SZ < grid_y + avail_h; ) {
        float gx = grid_x + PAD;
        for (int c = 0; c < cols && i < total; c++, i++) {
            int sprite_id = ASSET_ID_BASE + i;
            bool selected = (st->selected_id == sprite_id);
            bool hover    = hittest(gx, gy, CELL_DRAW_SZ, CELL_DRAW_SZ, mx, my);

            const char *name = st->assets->assets[i].name;
            if (name[0]) {
                float nw = text_measure_width(name, 1.1f);
                if (nw > CELL_DRAW_SZ - 4.0f) nw = CELL_DRAW_SZ - 4.0f;
                float nx = gx + (CELL_DRAW_SZ - nw) * 0.5f;
                text_draw(nx, gy + CELL_DRAW_SZ - text_line_height(1.1f) - 1.0f,
                          1.1f, 0.95f, 1.0f, 0.80f, 1.0f, name);
            }

            float border_r = selected ? 0.30f : (hover ? 0.40f : 0.22f);
            float border_g = selected ? 0.85f : (hover ? 0.60f : 0.22f);
            float border_b = selected ? 0.50f : (hover ? 0.40f : 0.26f);
            draw_box_border(gx, gy, CELL_DRAW_SZ, CELL_DRAW_SZ,
                            border_r, border_g, border_b);

            gx += CELL_DRAW_SZ + CELL_GAP;
        }
        gy += CELL_DRAW_SZ + CELL_GAP;
    }

    return gy - grid_y;
}

void sprites_tab_update(SpritesTab *st, int vw, int vh) {
    int mx, my;
    input_mouse_pos(&mx, &my);

    /* Update focused text field */
    if (st->name_focused)
        textinput_update(&st->name_field,   0, 0, 200.0f, 1.4f);
    if (st->import_path_focused)
        textinput_update(&st->import_path,  0, 0, 200.0f, 1.4f);
    if (st->import_name_focused)
        textinput_update(&st->import_name,  0, 0, 200.0f, 1.4f);
    if (st->load_path_focused)
        textinput_update(&st->load_path,    0, 0, 200.0f, 1.4f);

    float grid_w = (float)vw - INSPECTOR_W - PAD;
    float grid_x = 0.0f;

    int cols = (int)((grid_w + CELL_GAP) / (CELL_DRAW_SZ + CELL_GAP));
    if (cols < 1) cols = 1;

    /* --- Section header clicks --- */
    float sy = CONTENT_Y;

    /* "YOUR SPRITES" header — always visible, non-collapsible */
    float imported_hdr_y = sy;
    sy += SECTION_H;
    float imported_grid_start = sy;

    /* Estimate imported grid height to know where atlas section starts */
    int imp_count = (st->assets ? st->assets->count : 0);
    float imp_rows = imp_count > 0 ? (float)((imp_count + cols - 1) / cols) : 1.0f;
    float imp_grid_h = imp_rows * (CELL_DRAW_SZ + CELL_GAP) + PAD * 2.0f;
    if (imp_count == 0) imp_grid_h = PAD + text_line_height(SCALE_SMALL) + PAD;

    sy += imp_grid_h;

    /* "ATLAS SPRITES (PLACEHOLDER)" collapsible header */
    float atlas_hdr_y = sy;
    if (hittest(grid_x, atlas_hdr_y, grid_w, SECTION_H, mx, my) &&
        input_mouse_button_pressed(SDL_BUTTON_LEFT)) {
        st->show_atlas_section = !st->show_atlas_section;
    }
    sy += SECTION_H;

    /* Scroll with mouse wheel over grid */
    if (hittest(grid_x, CONTENT_Y, grid_w, (float)vh - CONTENT_Y, mx, my)) {
        int sdx, sdy;
        input_mouse_scroll(&sdx, &sdy);
        if (sdy) {
            st->scroll_cells -= sdy * SCROLL_SPEED;
            if (st->scroll_cells < 0) st->scroll_cells = 0;
        }
    }

    /* Click on an IMPORTED asset cell */
    if (input_mouse_button_pressed(SDL_BUTTON_LEFT) && st->assets) {
        float gy = imported_grid_start + PAD;
        int total = st->assets->count;
        int row0  = st->import_scroll / cols;
        int start = row0 * cols;
        for (int i = start; i < total && gy + CELL_DRAW_SZ < imported_grid_start + imp_grid_h; ) {
            float gx = grid_x + PAD;
            for (int c = 0; c < cols && i < total; c++, i++) {
                if (hittest(gx, gy, CELL_DRAW_SZ, CELL_DRAW_SZ, mx, my)) {
                    if (st->name_focused) {
                        textinput_unfocus(&st->name_field);
                        st->name_focused = false;
                    }
                    st->selected_id = ASSET_ID_BASE + i;
                    const char *n = st->assets->assets[i].name;
                    textinput_set(&st->name_field, n);
                    goto cell_clicked;
                }
                gx += CELL_DRAW_SZ + CELL_GAP;
            }
            gy += CELL_DRAW_SZ + CELL_GAP;
        }
    }

    /* Click on an ATLAS cell (when expanded) */
    if (st->show_atlas_section && st->atlas &&
        input_mouse_button_pressed(SDL_BUTTON_LEFT)) {
        float gy = sy + PAD;
        int total = st->atlas->sprite_count;
        int row0  = st->scroll_cells / cols;
        int start = row0 * cols;
        for (int i = start; i < total && gy + CELL_DRAW_SZ < (float)vh; ) {
            float gx = grid_x + PAD;
            for (int c = 0; c < cols && i < total; c++, i++) {
                if (hittest(gx, gy, CELL_DRAW_SZ, CELL_DRAW_SZ, mx, my)) {
                    if (st->name_focused) {
                        textinput_unfocus(&st->name_field);
                        st->name_focused = false;
                    }
                    st->selected_id = i;
                    textinput_set(&st->name_field, sprites_tab_get_name(st, i));
                    goto cell_clicked;
                }
                gx += CELL_DRAW_SZ + CELL_GAP;
            }
            gy += CELL_DRAW_SZ + CELL_GAP;
        }
    }
    (void)imported_hdr_y;
    cell_clicked:;
    (void)atlas_hdr_y;
    (void)vh;
}

void sprites_tab_render(const SpritesTab *st, int vw, int vh) {
    /* Background */
    renderer_draw_quad(0.0f, CONTENT_Y, (float)vw, (float)vh - CONTENT_Y,
                       0.08f, 0.08f, 0.10f, 1.0f);

    int mx, my;
    input_mouse_pos(&mx, &my);

    float grid_w = (float)vw - INSPECTOR_W - PAD;
    float grid_x = 0.0f;

    int cols = (int)((grid_w + CELL_GAP) / (CELL_DRAW_SZ + CELL_GAP));
    if (cols < 1) cols = 1;

    /* --- YOUR SPRITES section (imported assets, always visible) --- */
    float sy = CONTENT_Y;
    draw_section_header(grid_x, sy, grid_w,
                        "YOUR SPRITES  --  IMPORT YOUR OWN PNGs HERE");
    sy += SECTION_H;

    float imported_grid_start = sy;
    int imp_count = (st->assets ? st->assets->count : 0);

    float imp_grid_h = render_imported_assets_grid(
        st, grid_x, sy, grid_w, (float)vh * 0.5f,
        cols, mx, my, st->import_scroll);
    if (imp_count == 0)
        imp_grid_h = PAD + text_line_height(SCALE_SMALL) + PAD;

    sy += imp_grid_h;

    /* --- ATLAS (PLACEHOLDER) collapsible section --- */
    {
        /* Section header with toggle arrow */
        renderer_draw_quad(grid_x, sy, grid_w, SECTION_H, 0.10f, 0.11f, 0.13f, 1.0f);
        renderer_draw_quad(grid_x, sy + SECTION_H - 1.0f, grid_w, 1.0f, 0.25f, 0.28f, 0.32f, 1.0f);
        float th = text_line_height(SCALE_SMALL);
        const char *arrow = st->show_atlas_section ? "v" : ">";
        text_draw(grid_x + 8.0f, sy + (SECTION_H - th) * 0.5f,
                  SCALE_SMALL, 0.55f, 0.55f, 0.58f, 1.0f, arrow);
        text_draw(grid_x + 24.0f, sy + (SECTION_H - th) * 0.5f,
                  SCALE_SMALL, 0.50f, 0.50f, 0.52f, 1.0f,
                  "ATLAS SPRITES  (PLACEHOLDER SPRITESHEET -- CLICK TO EXPAND/COLLAPSE)");
        sy += SECTION_H;
    }

    if (st->show_atlas_section && st->atlas) {
        int total = st->atlas->sprite_count;
        int row0  = st->scroll_cells / cols;
        int start = row0 * cols;

        /* First pass: backgrounds + atlas sprites */
        renderer_bind_texture(st->atlas->texture.id);
        float gy = sy;
        for (int i = start; i < total && gy + CELL_DRAW_SZ < (float)vh; ) {
            float gx = grid_x + PAD;
            for (int c = 0; c < cols && i < total; c++, i++) {
                bool selected = (i == st->selected_id);
                bool hover    = hittest(gx, gy, CELL_DRAW_SZ, CELL_DRAW_SZ, mx, my);

                float cbg = selected ? 0.22f : (hover ? 0.17f : 0.12f);
                renderer_flush_texture();
                renderer_draw_quad(gx, gy, CELL_DRAW_SZ, CELL_DRAW_SZ,
                                   cbg, cbg, cbg + 0.04f, 1.0f);
                renderer_bind_texture(st->atlas->texture.id);

                UVRect uv = atlas_get_uv(st->atlas, i);
                float pad = 4.0f;
                renderer_draw_quad_uv(gx + pad, gy + pad,
                                      CELL_DRAW_SZ - pad * 2.0f, CELL_DRAW_SZ - pad * 2.0f,
                                      1.0f, 1.0f, 1.0f, 1.0f,
                                      uv.u0, uv.v0, uv.u1, uv.v1);

                gx += CELL_DRAW_SZ + CELL_GAP;
            }
            gy += CELL_DRAW_SZ + CELL_GAP;
        }
        renderer_flush_texture();

        /* Second pass: borders, index numbers, names */
        gy = sy;
        for (int i = start; i < total && gy + CELL_DRAW_SZ < (float)vh; ) {
            float gx = grid_x + PAD;
            for (int c = 0; c < cols && i < total; c++, i++) {
                bool selected = (i == st->selected_id);
                bool hover    = hittest(gx, gy, CELL_DRAW_SZ, CELL_DRAW_SZ, mx, my);

                char idx_buf[16];
                snprintf(idx_buf, sizeof idx_buf, "%d", i);
                text_draw(gx + 3.0f, gy + 3.0f, 1.2f,
                          0.85f, 0.85f, 0.85f, 0.80f, idx_buf);

                const char *name = sprites_tab_get_name(st, i);
                if (name[0]) {
                    float nw = text_measure_width(name, 1.1f);
                    float nx = gx + (CELL_DRAW_SZ - nw) * 0.5f;
                    text_draw(nx, gy + CELL_DRAW_SZ - text_line_height(1.1f) - 1.0f,
                              1.1f, 0.95f, 1.0f, 0.80f, 1.0f, name);
                }

                float border_r = selected ? 0.30f : (hover ? 0.40f : 0.22f);
                float border_g = selected ? 0.85f : (hover ? 0.60f : 0.22f);
                float border_b = selected ? 0.50f : (hover ? 0.40f : 0.26f);
                draw_box_border(gx, gy, CELL_DRAW_SZ, CELL_DRAW_SZ,
                                border_r, border_g, border_b);

                gx += CELL_DRAW_SZ + CELL_GAP;
            }
            gy += CELL_DRAW_SZ + CELL_GAP;
        }
    } else if (st->show_atlas_section && !st->atlas) {
        text_draw(PAD, sy + PAD, SCALE_SMALL, 0.45f, 0.20f, 0.20f, 1.0f,
                  "NO ATLAS LOADED -- USE ATLAS PATH + RELOAD ATLAS BELOW");
    }

    /* Vertical divider */
    float insp_x = (float)vw - INSPECTOR_W;
    renderer_draw_quad(insp_x - 1.0f, CONTENT_Y, 1.0f, (float)vh - CONTENT_Y,
                       0.22f, 0.22f, 0.28f, 1.0f);

    /* Inspector panel */
    renderer_draw_quad(insp_x, CONTENT_Y, INSPECTOR_W, (float)vh - CONTENT_Y,
                       0.09f, 0.09f, 0.11f, 1.0f);

    UILayout l;
    ui_layout_begin(&l, insp_x + PAD, CONTENT_Y + PAD, INSPECTOR_W - PAD * 2.0f);

    /* --- IMPORT YOUR SPRITES section (top of inspector) --- */
    ui_layout_label(&l, "IMPORT YOUR OWN SPRITE", false);
    ui_layout_gap(&l, 2.0f);

    ui_layout_label(&l, "IMAGE PATH", true);

    if (ui_layout_button(&l, "BROWSE FOR IMAGE...", 0, BTN_H, false, mx, my)) {
        char picked[1024];
        if (filepicker_open_image(picked, (int)sizeof picked)) {
            textinput_set(&((SpritesTab*)st)->import_path, picked);

            /* Only auto-fill the name if it's still empty -- never
               stomp something the user already typed. */
            if (!textinput_get(&st->import_name)[0]) {
                const char *slash = strrchr(picked, '/');
#ifdef _WIN32
                const char *bslash = strrchr(picked, '\\');
                if (bslash && (!slash || bslash > slash)) slash = bslash;
#endif
                const char *base = slash ? slash + 1 : picked;
                char name[ASSET_NAME_MAX];
                snprintf(name, sizeof name, "%.*s", (int)sizeof(name) - 1, base);
                char *dot = strrchr(name, '.');
                if (dot) *dot = '\0';
                textinput_set(&((SpritesTab*)st)->import_name, name);
            }
            snprintf(((SpritesTab*)st)->status, sizeof st->status, "PICKED '%.*s'.",
                     (int)sizeof(st->status) - 12, picked);
        } else {
            snprintf(((SpritesTab*)st)->status, sizeof st->status,
                     "NO FILE PICKER AVAILABLE -- TYPE THE PATH BELOW.");
        }
    }

    bool enter_dummy = false;
    if (ui_layout_field(&l, &((SpritesTab*)st)->import_path, 0, 24.0f,
                        st->import_path_focused, mx, my, &enter_dummy)) {
        if (!st->import_path_focused) {
            ((SpritesTab*)st)->import_path_focused = true;
            textinput_focus(&((SpritesTab*)st)->import_path);
            if (st->name_focused)        { textinput_unfocus(&((SpritesTab*)st)->name_field);  ((SpritesTab*)st)->name_focused = false; }
            if (st->import_name_focused) { textinput_unfocus(&((SpritesTab*)st)->import_name); ((SpritesTab*)st)->import_name_focused = false; }
            if (st->load_path_focused)   { textinput_unfocus(&((SpritesTab*)st)->load_path);   ((SpritesTab*)st)->load_path_focused = false; }
        }
    }

    ui_layout_label(&l, "SPRITE NAME", true);
    if (ui_layout_field(&l, &((SpritesTab*)st)->import_name, 0, 24.0f,
                        st->import_name_focused, mx, my, &enter_dummy)) {
        if (!st->import_name_focused) {
            ((SpritesTab*)st)->import_name_focused = true;
            textinput_focus(&((SpritesTab*)st)->import_name);
            if (st->name_focused)        { textinput_unfocus(&((SpritesTab*)st)->name_field);  ((SpritesTab*)st)->name_focused = false; }
            if (st->import_path_focused) { textinput_unfocus(&((SpritesTab*)st)->import_path); ((SpritesTab*)st)->import_path_focused = false; }
            if (st->load_path_focused)   { textinput_unfocus(&((SpritesTab*)st)->load_path);   ((SpritesTab*)st)->load_path_focused = false; }
        }
    }

    if (ui_layout_button(&l, "IMPORT SPRITE", 0, BTN_H, false, mx, my)) {
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
        snprintf(buf, sizeof buf, "%d SPRITE(S) IMPORTED", st->assets->count);
        ui_layout_label(&l, buf, true);
    }

    ui_layout_gap(&l, 14.0f);

    /* --- SELECTED SPRITE inspector --- */
    ui_layout_label(&l, "SELECTED SPRITE", false);

    if (st->selected_id < 0) {
        ui_layout_label(&l, "CLICK A SPRITE TO SELECT", true);
    } else {
        char info[64];
        bool is_asset = asset_library_is_asset_id(st->selected_id);
        if (is_asset) {
            int idx = st->selected_id - ASSET_ID_BASE;
            snprintf(info, sizeof info, "IMPORTED #%d", idx);
        } else {
            snprintf(info, sizeof info, "ATLAS CELL %d", st->selected_id);
        }
        ui_layout_label(&l, info, false);

        if (!is_asset) {
            char dim[64];
            snprintf(dim, sizeof dim, "%dx%d PX",
                     st->atlas ? st->atlas->cell_w : 0,
                     st->atlas ? st->atlas->cell_h : 0);
            ui_layout_label(&l, dim, true);
        }
        ui_layout_gap(&l, 6.0f);

        ui_layout_label(&l, "NAME", true);

        bool enter_pressed = false;
        if (ui_layout_field(&l, &((SpritesTab*)st)->name_field, 0, 24.0f,
                            st->name_focused, mx, my, &enter_pressed)) {
            if (!st->name_focused) {
                ((SpritesTab*)st)->name_focused = true;
                textinput_focus(&((SpritesTab*)st)->name_field);
                if (st->import_path_focused) { textinput_unfocus(&((SpritesTab*)st)->import_path); ((SpritesTab*)st)->import_path_focused = false; }
                if (st->import_name_focused) { textinput_unfocus(&((SpritesTab*)st)->import_name); ((SpritesTab*)st)->import_name_focused = false; }
                if (st->load_path_focused)   { textinput_unfocus(&((SpritesTab*)st)->load_path);   ((SpritesTab*)st)->load_path_focused = false; }
            }
        }

        ui_layout_gap(&l, 2.0f);
        if (ui_layout_button(&l, "SAVE NAME", 0, BTN_H, false, mx, my) || enter_pressed) {
            const char *new_name = textinput_get(&st->name_field);
            if (!is_asset) {
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
            }
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

    /* --- ATLAS PATH section (bottom of inspector, for placeholder atlas) --- */
    ui_layout_label(&l, "PLACEHOLDER ATLAS PATH", true);
    if (ui_layout_field(&l, &((SpritesTab*)st)->load_path, 0, 24.0f,
                        st->load_path_focused, mx, my, &enter_dummy)) {
        if (!st->load_path_focused) {
            ((SpritesTab*)st)->load_path_focused = true;
            textinput_focus(&((SpritesTab*)st)->load_path);
            if (st->name_focused)        { textinput_unfocus(&((SpritesTab*)st)->name_field);  ((SpritesTab*)st)->name_focused = false; }
            if (st->import_name_focused) { textinput_unfocus(&((SpritesTab*)st)->import_name); ((SpritesTab*)st)->import_name_focused = false; }
            if (st->import_path_focused) { textinput_unfocus(&((SpritesTab*)st)->import_path); ((SpritesTab*)st)->import_path_focused = false; }
        }
    }

    if (ui_layout_button(&l, "RELOAD ATLAS", 0, BTN_H, false, mx, my)) {
        /* Handled in main loop */
    }

    (void)imported_grid_start;
}
