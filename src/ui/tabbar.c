#include "tabbar.h"

#include <SDL2/SDL.h>
#include <string.h>

#include "../platform/input.h"
#include "../renderer/renderer.h"
#include "text.h"
#include "theme.h"

static const char *TAB_LABELS[TAB_COUNT] = {
    "WORLD", "OBJECTS", "SPRITES", "SCRIPTS", "SETTINGS"
};

void tabbar_init(TabBar *tb) {
    memset(tb, 0, sizeof(*tb));
    tb->active = TAB_WORLD;
}

ActiveTab tabbar_update(TabBar *tb, int vw, bool *out_toggle_play) {
    *out_toggle_play = false;

    int mx, my;
    input_mouse_pos(&mx, &my);
    bool clicked = input_mouse_button_pressed(SDL_BUTTON_LEFT);
    if (!clicked || my < 0 || my >= TABBAR_H) return tb->active;

    float tabs_w = (float)(vw - PLAY_CTRL_W);
    float tab_w  = tabs_w / (float)TAB_COUNT;

    if ((float)mx >= tabs_w) {
        /* Clicked in the play control zone:
           First third = PLAY, second third = PAUSE, last = STOP
           (only shown when playing — in edit mode it's all PLAY) */
        *out_toggle_play = true;
        return tb->active;
    }

    int idx = (int)((float)mx / tab_w);
    if (idx >= 0 && idx < TAB_COUNT)
        tb->active = (ActiveTab)idx;
    return tb->active;
}

void tabbar_render(const TabBar *tb, int vw, bool playing) {
    const Theme *th_ = theme_current();
    float tabs_w = (float)(vw - PLAY_CTRL_W);
    float tab_w  = tabs_w / (float)TAB_COUNT;
    int mx, my;
    input_mouse_pos(&mx, &my);
    bool hover_in_bar = (my >= 0 && my < TABBAR_H);

    /* Bar background */
    renderer_draw_quad(0.0f, 0.0f, (float)vw, (float)TABBAR_H,
                       th_->panel_bg_r, th_->panel_bg_g, th_->panel_bg_b, 1.0f);
    /* Bottom separator */
    renderer_draw_quad(0.0f, (float)TABBAR_H - 1.0f, (float)vw, 1.0f,
                       th_->border_r, th_->border_g, th_->border_b, 1.0f);

    float scale = 1.5f;
    float th = text_line_height(scale);

    for (int i = 0; i < TAB_COUNT; i++) {
        float tx = (float)i * tab_w;
        bool active = (tb->active == (ActiveTab)i);
        bool hover  = hover_in_bar &&
                      (float)mx >= tx && (float)mx < tx + tab_w;

        float br, bg, bb;
        if (playing) {
            br = 0.10f; bg = 0.10f; bb = 0.11f;
        } else if (active) {
            /* Active tab fill tints toward the theme accent rather than
               a hardcoded green, so a non-green theme's active tab
               actually looks like it belongs to that theme. */
            br = th_->panel_bg_r + th_->accent_r * 0.18f;
            bg = th_->panel_bg_g + th_->accent_g * 0.18f;
            bb = th_->panel_bg_b + th_->accent_b * 0.18f;
        } else if (hover) {
            br = 0.14f; bg = 0.16f; bb = 0.17f;
        } else {
            br = th_->panel_bg_r; bg = th_->panel_bg_g; bb = th_->panel_bg_b;
        }
        renderer_draw_quad(tx, 0.0f, tab_w - 1.0f, (float)TABBAR_H,
                           br, bg, bb, 1.0f);

        if (active && !playing)
            renderer_draw_quad(tx, (float)TABBAR_H - 2.0f, tab_w - 1.0f, 2.0f,
                               th_->accent_r, th_->accent_g, th_->accent_b, 1.0f);

        renderer_draw_quad(tx + tab_w - 1.0f, 0.0f, 1.0f, (float)TABBAR_H,
                           th_->border_r, th_->border_g, th_->border_b, 1.0f);

        const char *lbl = TAB_LABELS[i];
        float tw = text_measure_width(lbl, scale);
        float lx = tx + (tab_w - tw) * 0.5f;
        float ly = ((float)TABBAR_H - th) * 0.5f;

        float fr, fg, fb;
        if (playing) {
            fr = 0.30f; fg = 0.30f; fb = 0.33f;
        } else if (active) {
            fr = th_->text_r;  fg = th_->text_g;  fb = th_->text_b;
        } else if (hover) {
            fr = 0.80f; fg = 0.80f; fb = 0.82f;
        } else {
            fr = th_->text_dim_r; fg = th_->text_dim_g; fb = th_->text_dim_b;
        }
        text_draw(lx, ly, scale, fr, fg, fb, 1.0f, lbl);
    }

    /* Play control zone — three-button layout when playing, one when idle.
       PLAY  [  PLAY  ]         when editing
       PLAY  [  ||    ][  []  ] when playing (pause left, stop right)  */
    float pz = tabs_w;
    renderer_draw_quad(pz, 0.0f, 1.0f, (float)TABBAR_H, th_->border_r, th_->border_g, th_->border_b, 1.0f);

    if (!playing) {
        /* Single PLAY button */
        bool ph = hover_in_bar && (float)mx >= pz;
        float pr = ph ? th_->accent_r * 0.65f : th_->accent_r * 0.45f;
        float pg = ph ? th_->accent_g * 0.75f : th_->accent_g * 0.55f;
        float pb = ph ? th_->accent_b * 0.65f : th_->accent_b * 0.45f;
        renderer_draw_quad(pz, 0.0f, (float)PLAY_CTRL_W, (float)TABBAR_H, pr, pg, pb, 1.0f);
        renderer_draw_quad(pz, (float)TABBAR_H - 2.0f, (float)PLAY_CTRL_W, 2.0f,
                           th_->accent_r, th_->accent_g, th_->accent_b, 1.0f);
        const char *lbl = "PLAY";
        float tw = text_measure_width(lbl, scale);
        text_draw(pz + ((float)PLAY_CTRL_W - tw) * 0.5f,
                  ((float)TABBAR_H - th) * 0.5f,
                  scale, 1.0f, 1.0f, 1.0f, 1.0f, lbl);
    } else {
        /* STOP button (full zone, red — status color, not theme accent,
           since "stop" should read as a warning regardless of theme) */
        bool sh = hover_in_bar && (float)mx >= pz;
        float sr = sh ? th_->error_r * 0.80f : th_->error_r * 0.60f;
        float sg = sh ? th_->error_g * 0.65f : th_->error_g * 0.45f;
        float sb = sh ? th_->error_b * 0.65f : th_->error_b * 0.45f;
        renderer_draw_quad(pz, 0.0f, (float)PLAY_CTRL_W, (float)TABBAR_H, sr, sg, sb, 1.0f);
        /* Bottom red line while playing */
        renderer_draw_quad(pz, (float)TABBAR_H - 2.0f, (float)PLAY_CTRL_W, 2.0f,
                           th_->error_r, th_->error_g, th_->error_b, 1.0f);
        const char *lbl = "STOP PLAY";
        float tw = text_measure_width(lbl, scale);
        text_draw(pz + ((float)PLAY_CTRL_W - tw) * 0.5f,
                  ((float)TABBAR_H - th) * 0.5f,
                  scale, 1.0f, 0.85f, 0.85f, 1.0f, lbl);
    }
}

/* Draw the PLAY MODE overlay — a brief notification strip at the top
   of the game view when play starts, and a persistent thin bar during play. */
void tabbar_render_play_overlay(float overlay_timer, bool paused, int vw, int vh) {
    (void)vh;

    /* Persistent status bar at bottom of tab bar during play */
    float bar_y = (float)TABBAR_H - 3.0f;
    float pr = paused ? 0.80f : 0.22f;
    float pg = paused ? 0.55f : 0.80f;
    float pb = paused ? 0.18f : 0.22f;
    renderer_draw_quad(0.0f, bar_y, (float)vw, 3.0f, pr, pg, pb, 0.85f);

    /* Fade-in "PLAY MODE" notification (2 seconds) */
    if (overlay_timer > 0.0f) {
        float alpha = overlay_timer > 1.0f ? 1.0f : overlay_timer;
        float scale = 2.5f;
        const char *msg = paused ? "PAUSED" : "PLAY MODE";
        float tw = text_measure_width(msg, scale);
        float th = text_line_height(scale);
        float ox = ((float)vw - tw) * 0.5f;
        float oy = (float)TABBAR_H + 20.0f;

        /* Shadow */
        renderer_draw_quad(ox - 12.0f, oy - 6.0f, tw + 24.0f, th + 12.0f,
                           0.0f, 0.0f, 0.0f, alpha * 0.60f);

        text_draw(ox, oy, scale, pr, pg, pb, alpha, msg);
    }
}
