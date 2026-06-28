#ifndef DGE_TABBAR_H
#define DGE_TABBAR_H

/*  Phase J — Top tab bar.

    A fixed-height strip at the top of the editor (y=0, x=0..vw).
    Five tabs: World / Objects / Sprites / Scripts / Settings, plus a
    fixed-width Play/Stop control reserved on the right edge of the
    same row (added for the Edit/Play split — see core/playmode.h).
    Tabs occupy (vw - PLAY_BTN_W) split evenly five ways; the control
    isn't "tab content" so it living in tabbar.c (which already owns
    this row's layout) rather than panel.c or its own module keeps
    "who owns this row" answerable in one place.

    The tab bar does NOT own any content — it only manages which tab is
    active, and now also whether Play is on. Each tab's content is drawn
    by its own module (panel.c, objects_tab.c, sprites_tab.c, scripts_tab.c,
    settings_tab.c); what Play actually *does* is main.c's job (snapshot/
    restore via playmode.h, gating Edit-only input) — tabbar.c only
    reports the click.

    Height is TABBAR_H pixels; the content area below starts at y=TABBAR_H.
    main.c reserves that margin when passing viewport bounds to subsystems. */

#include <stdbool.h>

#define TABBAR_H 34   /* pixels reserved for the tab bar at the top */
#define PLAY_BTN_W 110 /* pixels reserved for the Play/Stop control */

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

/*  Call once per frame. Returns the new active tab (may be the same as
    before if no click occurred this frame). vw is the full viewport
    width. *out_toggle_play is set true if the Play/Stop control was
    clicked this frame (false otherwise) — main.c reads this and flips
    its own GameMode; tabbar.c has no opinion on what Play mode means,
    it just reports the click like it reports a tab click. */
ActiveTab tabbar_update(TabBar *tb, int vw, bool *out_toggle_play);

/*  Draw the bar. Call between renderer_begin_ui() and renderer_end().
    playing selects the control's "PLAY"/"STOP" label and color — tabbar.c
    doesn't store this itself since GameMode lives in main.c, not here. */
void tabbar_render(const TabBar *tb, int vw, bool playing);

#endif /* DGE_TABBAR_H */
