#ifndef DGE_MENUBAR_H
#define DGE_MENUBAR_H

/*  Godot-style top menu bar replacing the old TabBar.
    Provides standard application menus (File, Project, etc.), 
    workspace tabs centered, and play controls on the right.
*/

#include <stdbool.h>

#define TOP_BAR_H 36

typedef enum {
    MENU_ACTION_NONE = 0,
    MENU_ACTION_SAVE,
    MENU_ACTION_LOAD,
    MENU_ACTION_LEVEL_NEXT,
    MENU_ACTION_LEVEL_PREV,
    MENU_ACTION_LEVEL_ADD,
    MENU_ACTION_QUIT
} MenuAction;

typedef enum {
    TAB_WORLD    = 0,
    TAB_OBJECTS  = 1,
    TAB_SPRITES  = 2,
    TAB_SCRIPTS  = 3,
    TAB_SETTINGS = 4,
    TAB_COUNT    = 5,
} ActiveTab;

typedef enum {
    DROPDOWN_NONE = 0,
    DROPDOWN_FILE,
    DROPDOWN_PROJECT,
    DROPDOWN_VIEW,
    DROPDOWN_HELP
} ActiveDropdown;

typedef struct {
    ActiveTab active;
    ActiveDropdown active_dropdown;
} MenuBar;

void menubar_init(MenuBar *mb);

/*  Call once per frame. Returns the new active tab.
    *out_toggle_play is set true if the play control was clicked.
    *out_action is set to the selected menu action, or NONE. */
ActiveTab menubar_update(MenuBar *mb, int vw, int vh, bool *out_toggle_play, MenuAction *out_action);

/*  Draw the top menu bar. `playing` selects PLAY vs STOP styling. */
void menubar_render(const MenuBar *mb, int vw, bool playing);

/*  Draw any open dropdown menus overlaying the main viewport. */
void menubar_render_dropdowns(const MenuBar *mb, int vw, int vh);

/*  Draw the play-mode overlay (notification banner + status bar). */
void menubar_render_play_overlay(float overlay_timer, bool paused, int vw, int vh);

#endif /* DGE_MENUBAR_H */
