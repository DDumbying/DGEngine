#ifndef DGE_SETTINGS_TAB_H
#define DGE_SETTINGS_TAB_H

/*  Phase O — Settings tab.
    Project name rename, grid resize inputs, tile pixel dimensions. */

#include <stdbool.h>
#include "../core/project.h"
#include "textinput.h"

typedef enum {
    SETTINGS_FOCUS_NONE=0,
    SETTINGS_FOCUS_NAME,
    SETTINGS_FOCUS_GRID_W,
    SETTINGS_FOCUS_GRID_H,
    SETTINGS_FOCUS_TILE_W,
    SETTINGS_FOCUS_TILE_H,
} SettingsFocus;

typedef struct {
    TextInput     fi_name;
    TextInput     fi_grid_w, fi_grid_h;
    TextInput     fi_tile_w, fi_tile_h;
    SettingsFocus focus;
    char          status[128];
    /* Pending resize — applied to the world on Apply click */
    int pending_grid_w, pending_grid_h;
    int pending_tile_w, pending_tile_h;
    /* Apply-resize callback result flag for main.c to read */
    bool wants_resize;
    bool wants_tile_resize;
    /* Set when "CLOSE PROJECT" is clicked — main.c reads this once per
       frame and, if true, saves and returns to the Project Manager
       screen instead of staying in the editor. The only way back to
       the launcher today without this was quitting the whole app and
       relaunching, which is the gap this closes. */
    bool wants_close_project;
} SettingsTab;

void settings_tab_init(SettingsTab *st, const Project *proj);

/*  Per-frame update/render.  Returns true if project was saved. */
bool settings_tab_update(SettingsTab *st, Project *proj, int vw, int vh);
void settings_tab_render(const SettingsTab *st, const Project *proj, int vw, int vh);

#endif /* DGE_SETTINGS_TAB_H */
