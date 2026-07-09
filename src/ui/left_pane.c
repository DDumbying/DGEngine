#include "left_pane.h"
#include <stdio.h>
#include <SDL2/SDL.h>
#include "../platform/input.h"
#include "../renderer/renderer.h"
#include "text.h"
#include "theme.h"

#define ROW_H       26
#define HEADER_H    36
#define TAB_H       24
#define SCROLL_SPEED 3
#define LIST_TOP    (34 + HEADER_H + TAB_H)

static const char *LEFT_TAB_LABELS[LEFT_TAB_COUNT] = {
    "Scene", "Files", "Palette"
};

void left_pane_init(LeftPane *lp) {
    lp->is_collapsed = false;
    lp->active_tab = LEFT_TAB_SCENE;
    lp->scene_scroll_y = 0;
    lp->selected_entity = ENTITY_NULL;
    lp->files_scroll_y = 0;
    lp->palette_scroll_y = 0;
}

bool left_pane_update(LeftPane *lp, Editor *ed, Registry *reg, int viewport_h) {
    int px, py;
    input_mouse_pos(&px, &py);

    int pane_w = lp->is_collapsed ? 24 : LEFT_PANE_WIDTH;
    bool hovering = (px >= 0 && px < pane_w && py >= 34 && py < viewport_h);
    bool clicked = input_mouse_button_pressed(SDL_BUTTON_LEFT);

    /* Collapse toggle button */
    if (clicked && px >= 0 && px < 24 && py >= 34 && py < 34 + HEADER_H) {
        lp->is_collapsed = !lp->is_collapsed;
        return hovering;
    }

    if (lp->is_collapsed) return hovering;

    /* Tabs */
    if (clicked && py >= 34 + HEADER_H && py < 34 + HEADER_H + TAB_H) {
        float tab_w = (float)LEFT_PANE_WIDTH / LEFT_TAB_COUNT;
        int idx = (int)(px / tab_w);
        if (idx >= 0 && idx < LEFT_TAB_COUNT) {
            lp->active_tab = (LeftTab)idx;
        }
        return hovering;
    }

    if (lp->active_tab == LEFT_TAB_SCENE) {
        /* Scroll wheel */
        if (hovering && px < LEFT_PANE_WIDTH) {
            int sx, sy;
            input_mouse_scroll(&sx, &sy);
            (void)sx;
            if (sy) {
                lp->scene_scroll_y -= sy * SCROLL_SPEED;
                if (lp->scene_scroll_y < 0) lp->scene_scroll_y = 0;
            }
        }

        /* Click to select */
        if (hovering && clicked && py >= LIST_TOP) {
            int row_y = py - LIST_TOP + lp->scene_scroll_y;
            int row_idx = row_y / ROW_H;

            int visible_idx = 0;
            for (Entity e = 0; e < (Entity)MAX_ENTITIES; e++) {
                if (!reg->alive[e]) continue;
                if (visible_idx == row_idx) {
                    lp->selected_entity = e;
                    /* Sync editor selection */
                    ed->selected = entity_to_handle(reg, e);
                    break;
                }
                visible_idx++;
            }
        }

        /* Sync from editor */
        if (entity_handle_valid(reg, ed->selected)) {
            Entity sel = ed->selected.index;
            if (sel != lp->selected_entity)
                lp->selected_entity = sel;
        }
    } else if (lp->active_tab == LEFT_TAB_FILES) {
        if (hovering) {
            int sx, sy; input_mouse_scroll(&sx, &sy); (void)sx;
            if (sy) { lp->files_scroll_y -= sy * SCROLL_SPEED; if (lp->files_scroll_y < 0) lp->files_scroll_y = 0; }
        }
    } else if (lp->active_tab == LEFT_TAB_PALETTE) {
        if (hovering) {
            int sx, sy; input_mouse_scroll(&sx, &sy); (void)sx;
            if (sy) { lp->palette_scroll_y -= sy * SCROLL_SPEED; if (lp->palette_scroll_y < 0) lp->palette_scroll_y = 0; }
        }
    }

    return hovering;
}

void left_pane_render(const LeftPane *lp, const Editor *ed, const Registry *reg, int viewport_h) {
    (void)ed;
    const Theme *th = theme_current();

    int pane_y = 34;
    int pane_h = viewport_h - pane_y;
    int pane_w = lp->is_collapsed ? 24 : LEFT_PANE_WIDTH;

    /* Background */
    renderer_draw_quad(0, pane_y, pane_w, pane_h,
                       th->panel_bg_r, th->panel_bg_g, th->panel_bg_b, 1.0f);

    /* Right border */
    renderer_draw_quad(pane_w - 1, pane_y, 1, pane_h,
                       th->border_r, th->border_g, th->border_b, 1.0f);

    /* Header strip */
    renderer_draw_quad(0, pane_y, pane_w, HEADER_H,
                       th->bg_r, th->bg_g, th->bg_b, 1.0f);
    
    /* Toggle icon */
    text_draw(4, pane_y + 10, 1.2f, th->text_r, th->text_g, th->text_b, 1.0f, lp->is_collapsed ? ">>" : "<<");

    if (lp->is_collapsed) return;

    /* Tabs */
    float tab_w = (float)LEFT_PANE_WIDTH / LEFT_TAB_COUNT;
    for (int i = 0; i < LEFT_TAB_COUNT; i++) {
        float tx = i * tab_w;
        if (lp->active_tab == i) {
            renderer_draw_quad(tx, pane_y + HEADER_H, tab_w, TAB_H,
                               th->accent_r * 0.4f, th->accent_g * 0.4f, th->accent_b * 0.4f, 1.0f);
        } else {
            renderer_draw_quad(tx, pane_y + HEADER_H, tab_w, TAB_H,
                               th->bg_r * 0.8f, th->bg_g * 0.8f, th->bg_b * 0.8f, 1.0f);
        }
        
        float tw = text_measure_width(LEFT_TAB_LABELS[i], 1.0f);
        text_draw(tx + (tab_w - tw)*0.5f, pane_y + HEADER_H + 4, 1.0f, th->text_r, th->text_g, th->text_b, 1.0f, LEFT_TAB_LABELS[i]);
    }

    renderer_draw_quad(0, pane_y + HEADER_H + TAB_H, LEFT_PANE_WIDTH, 1, th->border_r, th->border_g, th->border_b, 1.0f);

    if (lp->active_tab == LEFT_TAB_SCENE) {
        float y = (float)LIST_TOP - (float)lp->scene_scroll_y;
        int count = 0;

        for (Entity e = 0; e < (Entity)MAX_ENTITIES; e++) {
            if (!reg->alive[e]) continue;
            count++;

            float row_top = y;
            float row_bot = y + ROW_H;

            if (row_bot < LIST_TOP || row_top > viewport_h) {
                y += ROW_H;
                continue;
            }

            if (lp->selected_entity == e) {
                renderer_draw_quad(2, (int)row_top, LEFT_PANE_WIDTH - 4, ROW_H,
                                   th->accent_r * 0.25f, th->accent_g * 0.25f, th->accent_b * 0.25f, 1.0f);
                renderer_draw_quad(0, (int)row_top, 3, ROW_H,
                                   th->accent_r, th->accent_g, th->accent_b, 1.0f);
            }

            char name[64];
            if (reg->has_definition[e]) {
                snprintf(name, sizeof(name), "%s", reg->definition[e].def_name);
            } else {
                snprintf(name, sizeof(name), "Entity %u", e);
            }

            float r = th->text_r, g = th->text_g, b = th->text_b;
            if (lp->selected_entity == e) {
                r = th->accent_r; g = th->accent_g; b = th->accent_b;
            }

            text_draw(14, row_top + 5, 1.0f, r, g, b, 1.0f, name);
            y += ROW_H;
        }

        if (count == 0) {
            text_draw(10, LIST_TOP + 12, 1.0f, th->text_dim_r, th->text_dim_g, th->text_dim_b, 1.0f, "No entities in scene.");
        }
    } else if (lp->active_tab == LEFT_TAB_FILES) {
        text_draw(10, LIST_TOP + 12, 1.0f, th->text_dim_r, th->text_dim_g, th->text_dim_b, 1.0f, "File system browser coming soon.");
    } else if (lp->active_tab == LEFT_TAB_PALETTE) {
        text_draw(10, LIST_TOP + 12, 1.0f, th->text_dim_r, th->text_dim_g, th->text_dim_b, 1.0f, "Palette coming soon.");
    }
}
