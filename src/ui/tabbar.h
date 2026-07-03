#ifndef DGE_TABBAR_H
#define DGE_TABBAR_H

/*  Top tab bar.

    A fixed-height strip at the top of the editor (y=0, x=0..vw).
    Five tabs: World / Objects / Sprites / Scripts / Settings, plus a
    fixed-width Play control zone at the right edge.

    Play control zone (PLAY_CTRL_W pixels):
      - When EDITING: shows a single green "PLAY" button.
      - When PLAYING:  shows a full-width red "STOP PLAY" button.

    tabbar.c only reports clicks — main.c / play_mode.h owns state.

    Height is TABBAR_H pixels; content area below starts at y=TABBAR_H. */

#include <stdbool.h>

#define TABBAR_H      34
#define PLAY_CTRL_W  120   /* pixels for the play control zone */

/* Legacy alias so existing code that uses PLAY_BTN_W still compiles */
#define PLAY_BTN_W PLAY_CTRL_W

typedef enum {
    TAB_WORLD    = 0,
    TAB_OBJECTS  = 1,
    TAB_SPRITES  = 2,
    TAB_SCRIPTS  = 3,
    TAB_SETTINGS = 4,
    TAB_COUNT    = 5,
} ActiveTab;

typedef struct {
    ActiveTab active;
} TabBar;

void tabbar_init(TabBar *tb);

/*  Call once per frame. Returns the new active tab.
    *out_toggle_play is set true if the play control was clicked. */
ActiveTab tabbar_update(TabBar *tb, int vw, bool *out_toggle_play);

/*  Draw the bar. `playing` selects PLAY vs STOP styling. */
void tabbar_render(const TabBar *tb, int vw, bool playing);

/*  Draw the play-mode overlay (notification banner + status bar).
    overlay_timer counts down from 2.0 to 0 (the fade-in duration).
    Call after tabbar_render when in play mode. */
void tabbar_render_play_overlay(float overlay_timer, bool paused, int vw, int vh);

#endif /* DGE_TABBAR_H */
