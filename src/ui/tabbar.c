#include "tabbar.h"

#include <SDL2/SDL.h>
#include <string.h>

#include "../platform/input.h"
#include "../renderer/renderer.h"
#include "text.h"

/* Labels shown on each tab button. */
static const char *TAB_LABELS[TAB_COUNT] = {
    "WORLD", "OBJECTS", "SPRITES", "SCRIPTS", "SETTINGS"
};

void tabbar_init(TabBar *tb) {
    memset(tb, 0, sizeof(*tb));
    tb->active = TAB_WORLD;
}

ActiveTab tabbar_update(TabBar *tb, int vw) {
    int mx, my;
    input_mouse_pos(&mx, &my);
    bool clicked = input_mouse_button_pressed(SDL_BUTTON_LEFT);

    float tab_w = (float)vw / (float)TAB_COUNT;
    if (clicked && my >= 0 && my < TABBAR_H) {
        int idx = (int)((float)mx / tab_w);
        if (idx >= 0 && idx < TAB_COUNT)
            tb->active = (ActiveTab)idx;
    }
    return tb->active;
}

void tabbar_render(const TabBar *tb, int vw) {
    float tab_w = (float)vw / (float)TAB_COUNT;
    int mx, my;
    input_mouse_pos(&mx, &my);
    bool hover_in_bar = (my >= 0 && my < TABBAR_H);

    /* Bar background */
    renderer_draw_quad(0.0f, 0.0f, (float)vw, (float)TABBAR_H,
                       0.09f, 0.09f, 0.11f, 1.0f);
    /* Bottom separator */
    renderer_draw_quad(0.0f, (float)TABBAR_H - 1.0f, (float)vw, 1.0f,
                       0.25f, 0.25f, 0.30f, 1.0f);

    float scale = 1.5f;
    float th = text_line_height(scale);

    for (int i = 0; i < TAB_COUNT; i++) {
        float tx = (float)i * tab_w;
        bool active = (tb->active == (ActiveTab)i);
        bool hover  = hover_in_bar &&
                      (float)mx >= tx && (float)mx < tx + tab_w;

        /* Tab background */
        float br, bg, bb;
        if (active) {
            br = 0.14f; bg = 0.45f; bb = 0.28f;   /* green accent */
        } else if (hover) {
            br = 0.15f; bg = 0.17f; bb = 0.18f;
        } else {
            br = 0.09f; bg = 0.09f; bb = 0.11f;
        }
        renderer_draw_quad(tx, 0.0f, tab_w - 1.0f, (float)TABBAR_H,
                           br, bg, bb, 1.0f);

        /* Active tab: bright bottom highlight line */
        if (active)
            renderer_draw_quad(tx, (float)TABBAR_H - 2.0f, tab_w - 1.0f, 2.0f,
                               0.30f, 0.85f, 0.50f, 1.0f);

        /* Vertical right-edge separator between tabs */
        renderer_draw_quad(tx + tab_w - 1.0f, 0.0f, 1.0f, (float)TABBAR_H,
                           0.20f, 0.20f, 0.24f, 1.0f);

        /* Label — centered */
        const char *lbl = TAB_LABELS[i];
        float tw = text_measure_width(lbl, scale);
        float lx = tx + (tab_w - tw) * 0.5f;
        float ly = ((float)TABBAR_H - th) * 0.5f;

        float fr, fg, fb;
        if (active) {
            fr = 1.0f; fg = 1.0f; fb = 1.0f;
        } else if (hover) {
            fr = 0.80f; fg = 0.80f; fb = 0.82f;
        } else {
            fr = 0.50f; fg = 0.50f; fb = 0.55f;
        }
        text_draw(lx, ly, scale, fr, fg, fb, 1.0f, lbl);
    }
}
