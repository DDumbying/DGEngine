# DGEngine — Implementation Notes (This Batch)

## Bugs Fixed

### 1. Text invisible while typing in fields (`layout.c`)
**Root cause:** `ui_layout_field()` was calling `textinput_update()` when focused
(which processes keystrokes into the buffer) but was *not* calling `textinput_render()`
while focused — only when *unfocused*. So the text was being stored but not drawn
until you clicked away.

**Fix in `src/ui/layout.c`:**
```c
// OLD (broken):
if (focused) {
    enter = textinput_update(ti, ...);
} else {
    textinput_render(ti, ...);  // ← only rendered when NOT focused!
}

// NEW (fixed):
if (focused) {
    enter = textinput_update(ti, ...);
}
// Always render — focused or not. textinput_render draws buffer + blink caret.
textinput_render(ti, text_x, text_y, SCALE_BODY, 1.0f, 1.0f, 1.0f, 1.0f);
```

This was the same bug in ALL text fields across the engine (sprite names, object
names, settings fields, import path, etc.) because they all go through `ui_layout_field`.

---

## New Files

### `src/editor/play_mode.h`
**Real play/edit/pause state machine.**

```c
typedef enum {
    PLAY_MODE_EDIT = 0,   // editor is active
    PLAY_MODE_PLAY,       // game running, editor hidden
    PLAY_MODE_PAUSED,     // sim paused mid-play
} PlayModeState;
```

**How to integrate in `main.c`:**
```c
PlayMode play_mode;
play_mode_init(&play_mode);

// In the main loop:
if (toggle_play) {
    if (play_mode_is_editing(&play_mode))
        play_mode_enter_play(&play_mode);
    else
        play_mode_stop(&play_mode);
}
play_mode_update(&play_mode, delta_time);

// Gate editing:
if (play_mode_is_editing(&play_mode)) {
    editor_update(...);
    panel_update(...);
}

// Gate simulation:
if (play_mode_is_playing(&play_mode) && play_mode.state != PLAY_MODE_PAUSED) {
    simulation_update(...);
    ai_update(...);
}

// In render:
tabbar_render(&tb, vw, play_mode_is_playing(&play_mode));
if (play_mode_is_playing(&play_mode))
    tabbar_render_play_overlay(play_mode.overlay_timer,
                               play_mode.state == PLAY_MODE_PAUSED, vw, vh);
```

**The key insight:** play mode is NOT "hide the editor". It gates ALL editing input
(mouse painting, tile picking, object placement, panel clicks) while letting the
world render and simulation run normally. Camera pan/zoom can stay active in both
modes.

---

### `src/world/world_shape.h`
**Freeform world shapes — any tile can be excluded from the playable area.**

The `WorldShape` struct holds a bit array (1 bit per tile) marking which tiles
are "in the playable area". Disabled tiles are invisible, non-walkable, and
unpaitable.

**How to integrate in `World`:**
```c
// In world.h, add to World struct:
WorldShape shape;

// In world_create():
world_shape_init(&w->shape);

// In world_destroy():
world_shape_destroy(&w->shape);

// In world_render(), before drawing each tile:
if (!world_shape_enabled(&w->shape, gx, gy)) continue;

// In editor_update() for EDITOR_MODE_PAINT and PLACE:
if (!world_shape_enabled(&world->shape, hover_gx, hover_gy)) return;

// New EDITOR_MODE_SHAPE:
case EDITOR_MODE_SHAPE:
    if (ed->hover_valid) {
        if (input_mouse_button_down(SDL_BUTTON_LEFT))
            world_shape_set(&world->shape, hover_gx, hover_gy, true);
        if (input_mouse_button_down(SDL_BUTTON_RIGHT))
            world_shape_set(&world->shape, hover_gx, hover_gy, false);
    }
    break;
```

**Activation:** shape editing is off by default (all tiles enabled via the
`ws->active = false` fast path). When the user enables it in Settings, call:
```c
world_shape_activate(&world->shape, world->width, world->height);
```

**Save/load:** the bit array should be appended to the world file as an optional
chunk (detect its presence by file size). The format is just `(width*height+7)/8`
raw bytes.

---

## Changed Files

### `src/ui/sprites_tab.h` + `sprites_tab.c`
**Two-section layout: your sprites first, atlas placeholders second.**

The grid now has two clearly separated sections:

```
┌─────────────────────────────────────────────────────────┐
│ YOUR SPRITES  --  IMPORT YOUR OWN PNGs HERE              │  ← section header
│  [thumb] [thumb] [thumb]                                  │  ← imported assets
│                                                           │
│ > ATLAS SPRITES (PLACEHOLDER) -- CLICK TO EXPAND         │  ← collapsible
│   [cell0][cell1][cell2]...                                │  ← atlas grid
└─────────────────────────────────────────────────────────┘
```

Inspector panel now shows:
1. **IMPORT YOUR OWN SPRITE** at the top — image path + name + IMPORT button
2. **SELECTED SPRITE** below — shows info for whatever cell was last clicked
3. **PLACEHOLDER ATLAS PATH** at the very bottom — for the old reload-atlas flow

New header field: `bool show_atlas_section` (default `true`, toggled by clicking
the atlas section header).

### `src/ui/settings_tab.h` + `settings_tab.c`
**Real editor settings, not just project grid dimensions.**

New `EditorSettings` struct (meant to be owned by `main.c` and persisted):
```c
typedef struct {
    EditorTheme    theme;          // GREEN / BLUE / PURPLE / ORANGE accent
    WorldPanelMode panel_mode;     // DOCKED (fixed sidebar) or FLOATING
    bool show_fps;
    bool show_tile_coords;
    bool show_minimap;
    bool show_grid_overlay;
    bool world_shape_active;       // enables WorldShape masking
    float float_panel_x, float_panel_y;  // floating panel position
} EditorSettings;
```

`editor_settings_accent(s, &r, &g, &b)` returns the current accent color so
all buttons/borders can reference it and theme changes propagate immediately.

**How to integrate:**
```c
// In main.c, alongside World, Registry, etc.:
EditorSettings editor_settings;
editor_settings_init(&editor_settings);

// Pass to settings_tab_init:
settings_tab_init(&settings_tab, &project, &editor_settings);

// In render: pass accent to tabbar for colored active tabs (future)
```

### `src/ui/tabbar.h` + `tabbar.c`
**Play control zone redesigned.**

- **EDIT mode:** single green "PLAY" button
- **PLAY mode:** full-width red "STOP PLAY" button
- New `tabbar_render_play_overlay()` draws the fade-in "PLAY MODE" notification
  and a persistent colored bar below the tab row during play.

The old `PLAY_BTN_W` macro is kept as an alias for `PLAY_CTRL_W` for backward
compatibility.

---

## What's Still Needed (Integration in main.c)

1. **Wire `PlayMode` into main.c** — replace the existing `bool playing` toggle
   with `PlayMode play_mode` and use the state machine functions.

2. **Wire `EditorSettings` into main.c** — create one instance, pass to
   `settings_tab_init`, check `settings.show_minimap` before rendering minimap,
   check `settings.panel_mode` to decide floating vs docked panel.

3. **Wire `WorldShape` into `World`** — add `WorldShape shape` to the `World`
   struct, call `world_shape_set()` in the new `EDITOR_MODE_SHAPE`, skip disabled
   tiles in `world_render()`.

4. **Settings persistence** — save/load `EditorSettings` to a user config file
   (e.g. `~/.config/dgengine/editor.cfg`) separate from the project file, so
   theme/display preferences survive across projects.

5. **Floating panel** — when `settings.panel_mode == WORLD_PANEL_FLOATING`,
   render the panel as a draggable overlay window instead of the fixed left
   sidebar. The drag logic needs mouse capture during drag.

