#ifndef DGE_SETTINGS_TAB_H
#define DGE_SETTINGS_TAB_H

/*  Settings Tab — a left-rail list of section titles + a right-side detail
    pane, same pattern as System Preferences / VS Code settings, instead of
    one long scrolling column. Clicking a section swaps which detail render
    function runs; nothing scrolls past sections you're not looking at.

    Sections (left rail, top to bottom):
      1. GENERAL     — project name, grid size, tile size (the default,
                        first-shown section)
      2. APPEARANCE  — theme accent, world editor panel docked/floating
      3. DISPLAY     — show FPS, tile coords, minimap, grid overlay
      4. WORLD SHAPE — activate freeform tile masking
      5. PROJECT FILE — save / close project

    Each section's content used to live concatenated top-to-bottom in one
    function; now each has its own small render_section_X() in
    settings_tab.c, all using the same x/y/width as if they were the only
    thing on screen, since only one is ever visible at a time.            */

#include <stdbool.h>
#include <string.h>
#include "../core/project.h"
#include "textinput.h"

/* Editor theme accent colors (affects button highlights, borders, etc.) */
typedef enum {
    EDITOR_THEME_GREEN = 0,   /* default: green accent */
    EDITOR_THEME_BLUE,
    EDITOR_THEME_PURPLE,
    EDITOR_THEME_ORANGE,
    EDITOR_THEME_COUNT
} EditorTheme;

/* Where the world editor panel lives */
typedef enum {
    WORLD_PANEL_DOCKED = 0,   /* left sidebar, fixed */
    WORLD_PANEL_FLOATING,     /* detached, draggable window */
} WorldPanelMode;

typedef struct {
    /* Editor appearance */
    EditorTheme   theme;            /* deprecated — kept so old config files
                                        still parse; theme_path is authoritative
                                        now (see ui/theme.h) */
    char          theme_path[256];  /* path to the active .theme file */
    WorldPanelMode panel_mode;

    /* Display toggles */
    bool show_fps;
    bool show_tile_coords;
    bool show_minimap;
    bool show_grid_overlay;

    /* World shape */
    bool world_shape_active;

    /* Floating panel drag state (only when panel_mode == FLOATING) */
    float float_panel_x;
    float float_panel_y;
    bool  dragging_panel;
    float drag_off_x, drag_off_y;
} EditorSettings;

/* Left-rail section list. GENERAL is first/default per the "first window
   should be general" requirement — also SETTINGS_SEC_FIRST below so the
   rail-drawing loop and settings_tab_init() don't hardcode the value 0
   in two places that could drift if a section is ever inserted earlier. */
typedef enum {
    SETTINGS_SEC_GENERAL = 0,
    SETTINGS_SEC_APPEARANCE,
    SETTINGS_SEC_DISPLAY,
    SETTINGS_SEC_WORLD_SHAPE,
    SETTINGS_SEC_PROJECT_FILE,
    SETTINGS_SEC_COUNT
} SettingsSection;
#define SETTINGS_SEC_FIRST SETTINGS_SEC_GENERAL

/* Settings tab UI state */
typedef enum {
    SETTINGS_FOCUS_NONE = 0,
    SETTINGS_FOCUS_NAME,
    SETTINGS_FOCUS_GRID_W,
    SETTINGS_FOCUS_GRID_H,
    SETTINGS_FOCUS_TILE_W,
    SETTINGS_FOCUS_TILE_H,
} SettingsFocus;

typedef struct {
    TextInput   fi_name;
    TextInput   fi_grid_w;
    TextInput   fi_grid_h;
    TextInput   fi_tile_w;
    TextInput   fi_tile_h;
    SettingsFocus focus;

    /* Which left-rail section is currently shown in the detail pane. */
    SettingsSection active_section;

    /* Pending resize triggers */
    bool wants_resize;
    bool wants_tile_resize;
    int  pending_grid_w, pending_grid_h;
    int  pending_tile_w, pending_tile_h;

    bool wants_close_project;

    char status[128];

    /* Pointer to shared editor settings (not owned, set in init) */
    EditorSettings *settings;
} SettingsTab;

/* Default settings */
static inline void editor_settings_init(EditorSettings *s) {
    s->theme           = EDITOR_THEME_GREEN;
    s->theme_path[0]   = '\0';
    strncpy(s->theme_path, "themes/default.theme", sizeof s->theme_path - 1);
    s->panel_mode      = WORLD_PANEL_DOCKED;
    s->show_fps        = true;
    s->show_tile_coords = true;
    s->show_minimap    = true;
    s->show_grid_overlay = false;
    s->world_shape_active = false;
    s->float_panel_x   = 20.0f;
    s->float_panel_y   = 60.0f;
    s->dragging_panel  = false;
    s->drag_off_x      = 0.0f;
    s->drag_off_y      = 0.0f;
}

/* Get accent color for current theme (r,g,b out params) */
static inline void editor_settings_accent(const EditorSettings *s,
                                           float *r, float *g, float *b) {
    switch (s->theme) {
        case EDITOR_THEME_BLUE:   *r=0.22f; *g=0.55f; *b=0.90f; return;
        case EDITOR_THEME_PURPLE: *r=0.55f; *g=0.22f; *b=0.90f; return;
        case EDITOR_THEME_ORANGE: *r=0.90f; *g=0.52f; *b=0.14f; return;
        default:                  *r=0.30f; *g=0.78f; *b=0.48f; return;
    }
}

void settings_tab_init(SettingsTab *st, const Project *proj, EditorSettings *settings);
bool settings_tab_update(SettingsTab *st, Project *proj, int vw, int vh);
void settings_tab_render(const SettingsTab *st, const Project *proj, int vw, int vh);

/* Persistence — independent of any open project, so it lives at
   ~/.dgengine_editor.cfg (sibling to the existing ~/.dgengine recent-
   projects file in core/project.c) rather than inside a project
   folder. Theme/panel-mode/display toggles survive between sessions;
   load() leaves *s untouched (caller should editor_settings_init()
   first) if the file doesn't exist yet, e.g. first run. */
bool editor_settings_save(const EditorSettings *s);
bool editor_settings_load(EditorSettings *s);

#endif /* DGE_SETTINGS_TAB_H */
