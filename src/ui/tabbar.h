#ifndef DGE_TABBAR_H
#define DGE_TABBAR_H

/*  Top tab bar.

    A fixed-height strip at the top of the editor (y=0, x=0..vw).
    Five tabs: World / Objects / Sprites / Scripts / Settings.

    The tab bar does NOT own any content — it only manages which tab is
    active. Each tab's content is drawn by its own module (panel.c,
    objects_tab.c, sprites_tab.c, scripts_tab.c, settings_tab.c).

    Height is TABBAR_H pixels; the content area below starts at y=TABBAR_H.
    main.c reserves that margin when passing viewport bounds to subsystems. */

#include <stdbool.h>

#define TABBAR_H 34   /* pixels reserved for the tab bar at the top */

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

/*  Call once per frame.  Returns the new active tab (may be the same as
    before if no click occurred this frame).  vw is the full viewport width. */
ActiveTab tabbar_update(TabBar *tb, int vw);

/*  Draw the bar.  Call between renderer_begin_ui() and renderer_end(). */
void tabbar_render(const TabBar *tb, int vw);

#endif /* DGE_TABBAR_H */
