#include "menubar.h"

#include <SDL2/SDL.h>
#include <string.h>

#include "../platform/input.h"
#include "../renderer/renderer.h"
#include "text.h"
#include "theme.h"

static const char *TAB_LABELS[TAB_COUNT] = {
    "WORLD", "OBJECTS", "SPRITES", "SCRIPTS", "SETTINGS"
};
static const float TAB_W = 100.0f;

static const char *MENU_LABELS[] = { "File", "Project", "View", "Help" };
static const int MENU_COUNT = 4;
static const float MENU_W[] = { 60.0f, 80.0f, 60.0f, 60.0f };

#define PLAY_CTRL_W 120.0f

void menubar_init(MenuBar *mb) {
    memset(mb, 0, sizeof(*mb));
    mb->active = TAB_WORLD;
    mb->active_dropdown = DROPDOWN_NONE;
}

ActiveTab menubar_update(MenuBar *mb, int vw, int vh, bool *out_toggle_play, MenuAction *out_action) {
    (void)vh;
    *out_toggle_play = false;
    *out_action = MENU_ACTION_NONE;

    int mx, my;
    input_mouse_pos(&mx, &my);
    bool clicked = input_mouse_button_pressed(SDL_BUTTON_LEFT);

    /* Handle open dropdown clicks */
    if (mb->active_dropdown != DROPDOWN_NONE) {
        if (clicked) {
            float start_x = 0;
            for (int i = 0; i < (int)mb->active_dropdown - 1; i++) {
                start_x += MENU_W[i];
            }
            float drop_w = 160.0f;
            float drop_item_h = 30.0f;

            if ((float)mx >= start_x && (float)mx < start_x + drop_w && (float)my >= (float)TOP_BAR_H) {
                int item_idx = (int)(((float)my - (float)TOP_BAR_H) / drop_item_h);
                
                if (mb->active_dropdown == DROPDOWN_FILE) {
                    if (item_idx == 0) *out_action = MENU_ACTION_SAVE;
                    else if (item_idx == 1) *out_action = MENU_ACTION_LOAD;
                    else if (item_idx == 2) *out_action = MENU_ACTION_QUIT;
                } else if (mb->active_dropdown == DROPDOWN_PROJECT) {
                    if (item_idx == 0) *out_action = MENU_ACTION_LEVEL_NEXT;
                    else if (item_idx == 1) *out_action = MENU_ACTION_LEVEL_PREV;
                    else if (item_idx == 2) *out_action = MENU_ACTION_LEVEL_ADD;
                }
            }
            /* Clicking anywhere (inside or outside dropdown) closes it right now */
            mb->active_dropdown = DROPDOWN_NONE;
        }
        return mb->active;
    }

    /* Standard menu bar click detection */
    if (!clicked || my < 0 || my >= TOP_BAR_H) return mb->active;

    /* Play zone on right */
    if ((float)mx >= (float)vw - PLAY_CTRL_W) {
        *out_toggle_play = true;
        return mb->active;
    }

    /* Left application menus */
    float current_x = 0;
    for (int i = 0; i < MENU_COUNT; i++) {
        if ((float)mx >= current_x && (float)mx < current_x + MENU_W[i]) {
            mb->active_dropdown = (ActiveDropdown)(i + 1);
            return mb->active;
        }
        current_x += MENU_W[i];
    }

    /* Centered workspace tabs */
    float total_tabs_w = TAB_W * (float)TAB_COUNT;
    float tabs_start_x = ((float)vw - total_tabs_w) * 0.5f;
    if ((float)mx >= tabs_start_x && (float)mx < tabs_start_x + total_tabs_w) {
        int idx = (int)(((float)mx - tabs_start_x) / TAB_W);
        if (idx >= 0 && idx < TAB_COUNT) {
            mb->active = (ActiveTab)idx;
        }
    }

    return mb->active;
}

void menubar_render(const MenuBar *mb, int vw, bool playing) {
    const Theme *th_ = theme_current();
    int mx, my;
    input_mouse_pos(&mx, &my);
    bool hover_in_bar = (my >= 0 && my < TOP_BAR_H);
    float scale = 1.5f;
    float th = text_line_height(scale);

    /* Bar background */
    renderer_draw_quad(0.0f, 0.0f, (float)vw, (float)TOP_BAR_H,
                       th_->panel_bg_r, th_->panel_bg_g, th_->panel_bg_b, 1.0f);
    
    /* Bottom separator */
    renderer_draw_quad(0.0f, (float)TOP_BAR_H - 1.0f, (float)vw, 1.0f,
                       0.05f, 0.05f, 0.05f, 1.0f);

    /* 1. Render Left App Menus */
    float current_x = 0;
    for (int i = 0; i < MENU_COUNT; i++) {
        bool active = (mb->active_dropdown == (ActiveDropdown)(i + 1));
        bool hover = hover_in_bar && (float)mx >= current_x && (float)mx < current_x + MENU_W[i];

        if (active) {
            renderer_draw_quad(current_x, 0.0f, MENU_W[i], (float)TOP_BAR_H,
                               th_->panel_bg_r + 0.1f, th_->panel_bg_g + 0.1f, th_->panel_bg_b + 0.1f, 1.0f);
        } else if (hover) {
            renderer_draw_quad(current_x, 0.0f, MENU_W[i], (float)TOP_BAR_H,
                               th_->panel_bg_r + 0.05f, th_->panel_bg_g + 0.05f, th_->panel_bg_b + 0.05f, 1.0f);
        }

        const char *lbl = MENU_LABELS[i];
        float tw = text_measure_width(lbl, scale);
        text_draw(current_x + (MENU_W[i] - tw) * 0.5f,
                  ((float)TOP_BAR_H - th) * 0.5f,
                  scale, th_->text_r, th_->text_g, th_->text_b, 1.0f, lbl);

        current_x += MENU_W[i];
    }

    /* 2. Render Centered Workspace Tabs */
    float total_tabs_w = TAB_W * (float)TAB_COUNT;
    float tabs_start_x = ((float)vw - total_tabs_w) * 0.5f;

    for (int i = 0; i < TAB_COUNT; i++) {
        float tx = tabs_start_x + (float)i * TAB_W;
        bool active = (mb->active == (ActiveTab)i);
        bool hover = hover_in_bar && (float)mx >= tx && (float)mx < tx + TAB_W;

        if (active) {
            /* Pill shape background for active tab */
            renderer_draw_quad(tx + 4.0f, 4.0f, TAB_W - 8.0f, (float)TOP_BAR_H - 8.0f,
                               th_->accent_r * 0.6f, th_->accent_g * 0.6f, th_->accent_b * 0.6f, 1.0f);
        } else if (hover) {
            renderer_draw_quad(tx + 4.0f, 4.0f, TAB_W - 8.0f, (float)TOP_BAR_H - 8.0f,
                               th_->panel_bg_r + 0.05f, th_->panel_bg_g + 0.05f, th_->panel_bg_b + 0.05f, 1.0f);
        }

        const char *lbl = TAB_LABELS[i];
        float tw = text_measure_width(lbl, scale);
        
        float fr = th_->text_dim_r, fg = th_->text_dim_g, fb = th_->text_dim_b;
        if (active || hover) {
            fr = th_->text_r; fg = th_->text_g; fb = th_->text_b;
        }

        text_draw(tx + (TAB_W - tw) * 0.5f,
                  ((float)TOP_BAR_H - th) * 0.5f,
                  scale, fr, fg, fb, 1.0f, lbl);
    }

    /* 3. Render Right Play Controls */
    float pz = (float)vw - PLAY_CTRL_W;
    
    if (!playing) {
        bool ph = hover_in_bar && (float)mx >= pz;
        float pr = ph ? th_->accent_r * 0.65f : th_->accent_r * 0.45f;
        float pg = ph ? th_->accent_g * 0.75f : th_->accent_g * 0.55f;
        float pb = ph ? th_->accent_b * 0.65f : th_->accent_b * 0.45f;
        renderer_draw_quad(pz, 0.0f, PLAY_CTRL_W, (float)TOP_BAR_H, pr, pg, pb, 1.0f);
        
        const char *lbl = "PLAY";
        float tw = text_measure_width(lbl, scale);
        text_draw(pz + (PLAY_CTRL_W - tw) * 0.5f,
                  ((float)TOP_BAR_H - th) * 0.5f,
                  scale, 1.0f, 1.0f, 1.0f, 1.0f, lbl);
    } else {
        bool sh = hover_in_bar && (float)mx >= pz;
        float sr = sh ? th_->error_r * 0.80f : th_->error_r * 0.60f;
        float sg = sh ? th_->error_g * 0.65f : th_->error_g * 0.45f;
        float sb = sh ? th_->error_b * 0.65f : th_->error_b * 0.45f;
        renderer_draw_quad(pz, 0.0f, PLAY_CTRL_W, (float)TOP_BAR_H, sr, sg, sb, 1.0f);
        
        const char *lbl = "STOP PLAY";
        float tw = text_measure_width(lbl, scale);
        text_draw(pz + (PLAY_CTRL_W - tw) * 0.5f,
                  ((float)TOP_BAR_H - th) * 0.5f,
                  scale, 1.0f, 0.85f, 0.85f, 1.0f, lbl);
    }
}

void menubar_render_dropdowns(const MenuBar *mb, int vw, int vh) {
    (void)vw; (void)vh;
    if (mb->active_dropdown == DROPDOWN_NONE) return;

    const Theme *th_ = theme_current();
    float start_x = 0;
    for (int i = 0; i < (int)mb->active_dropdown - 1; i++) {
        start_x += MENU_W[i];
    }
    float drop_w = 160.0f;
    float drop_item_h = 30.0f;
    
    int mx, my;
    input_mouse_pos(&mx, &my);

    int num_items = 0;
    const char *labels[5] = {0};

    if (mb->active_dropdown == DROPDOWN_FILE) {
        num_items = 3;
        labels[0] = "Save World";
        labels[1] = "Load World";
        labels[2] = "Quit Editor";
    } else if (mb->active_dropdown == DROPDOWN_PROJECT) {
        num_items = 3;
        labels[0] = "Next Level";
        labels[1] = "Prev Level";
        labels[2] = "Add Level";
    } else {
        num_items = 1;
        labels[0] = "(Empty)";
    }

    float drop_h = drop_item_h * (float)num_items;
    
    /* Dropdown BG and shadow/border */
    renderer_draw_quad(start_x, (float)TOP_BAR_H, drop_w, drop_h,
                       th_->panel_bg_r + 0.05f, th_->panel_bg_g + 0.05f, th_->panel_bg_b + 0.05f, 1.0f);
    renderer_draw_quad(start_x, (float)TOP_BAR_H + drop_h, drop_w, 1.0f,
                       th_->border_r, th_->border_g, th_->border_b, 1.0f);
    renderer_draw_quad(start_x + drop_w, (float)TOP_BAR_H, 1.0f, drop_h + 1.0f,
                       th_->border_r, th_->border_g, th_->border_b, 1.0f);
    renderer_draw_quad(start_x - 1.0f, (float)TOP_BAR_H, 1.0f, drop_h + 1.0f,
                       th_->border_r, th_->border_g, th_->border_b, 1.0f);

    float scale = 1.5f;
    float th = text_line_height(scale);

    for (int i = 0; i < num_items; i++) {
        float item_y = (float)TOP_BAR_H + (float)i * drop_item_h;
        bool hover = ((float)mx >= start_x && (float)mx < start_x + drop_w &&
                      (float)my >= item_y && (float)my < item_y + drop_item_h);
        
        if (hover && labels[0][0] != '(') { /* Don't highlight (Empty) */
            renderer_draw_quad(start_x, item_y, drop_w, drop_item_h,
                               th_->accent_r * 0.4f, th_->accent_g * 0.4f, th_->accent_b * 0.4f, 1.0f);
        }

        text_draw(start_x + 12.0f, item_y + (drop_item_h - th) * 0.5f,
                  scale, th_->text_r, th_->text_g, th_->text_b, 1.0f, labels[i]);
    }
}

void menubar_render_play_overlay(float overlay_timer, bool paused, int vw, int vh) {
    (void)vh;
    /* Persistent status bar at bottom of top bar during play */
    float bar_y = (float)TOP_BAR_H - 3.0f;
    float pr = paused ? 0.80f : 0.22f;
    float pg = paused ? 0.55f : 0.80f;
    float pb = paused ? 0.18f : 0.22f;
    renderer_draw_quad(0.0f, bar_y, (float)vw, 3.0f, pr, pg, pb, 0.85f);

    /* Fade-in notification */
    if (overlay_timer > 0.0f) {
        float alpha = overlay_timer > 1.0f ? 1.0f : overlay_timer;
        float scale = 2.5f;
        const char *msg = paused ? "PAUSED" : "PLAY MODE";
        float tw = text_measure_width(msg, scale);
        float th = text_line_height(scale);
        float ox = ((float)vw - tw) * 0.5f;
        float oy = (float)TOP_BAR_H + 20.0f;

        renderer_draw_quad(ox - 12.0f, oy - 6.0f, tw + 24.0f, th + 12.0f,
                           0.0f, 0.0f, 0.0f, alpha * 0.60f);

        text_draw(ox, oy, scale, pr, pg, pb, alpha, msg);
    }
}
