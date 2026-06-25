#ifndef DGE_PANEL_H
#define DGE_PANEL_H

#include <stdbool.h>
#include "../editor/editor.h"
#include "../simulation/simulation.h"

/*  the first clickable UI. Before this, editor.c was driven
    entirely by keyboard shortcuts (TAB, 1-5, M, H...) with no visible
    affordance for what any of them did. panel.c doesn't replace that
    input path — every shortcut still works exactly as before — it adds
    a parallel screen-space sidebar that mutates the same Editor/World/
    Registry/ResourceStore state through the same setters, so there is
    no second source of truth for "what mode am I in".

    Fixed-width left sidebar, full window height. Mouse clicks inside
    PANEL_WIDTH px of the left edge are owned by the panel and must not
    fall through to world-space tile picking — editor_update() is told
    this via its ui_panel_width parameter (see editor.h). */

#define PANEL_WIDTH 210

typedef enum {
    PANEL_ACTION_NONE = 0,
    PANEL_ACTION_NEW,
    PANEL_ACTION_REGENERATE,
    PANEL_ACTION_SAVE,
    PANEL_ACTION_LOAD,
    PANEL_ACTION_RESIZE,
    PANEL_ACTION_WEATHER_TOGGLE,   /* toggle weather enabled/disabled */
    PANEL_ACTION_WEATHER_SET,      /* .weather_type is valid          */
} PanelActionType;

typedef struct {
    PanelActionType type;
    int weather_type;   /* used by PANEL_ACTION_WEATHER_SET */
} PanelAction;

typedef struct {
    /* Canvas-size fields being edited; only applied to the real World
       on PANEL_ACTION_RESIZE, so typo/overshoot clicks on +/- don't
       resize (and reallocate) the world on every click. */
    int pending_w;
    int pending_h;

    /* Whether the sidebar is currently drawn/clickable. A small tab
       stays visible at the screen edge either way so there's always a
       way back in — see panel_update()/panel_render(). Toggle with the
       backtick key (SDL_SCANCODE_GRAVE) or by clicking the tab. */
    bool visible;
} Panel;

void panel_init(Panel *p, int world_w, int world_h);

/*  How much of the left edge the panel currently owns for click/pick
    purposes — PANEL_WIDTH while visible, 0 while hidden (the small
    toggle tab itself is narrow enough that letting world-picking work
    underneath it the rare time someone clicks exactly on its sliver
    is an acceptable trade rather than plumbing a second exception
    through editor_update()). main.c passes this into editor_update()'s
    ui_panel_width parameter instead of a hardcoded PANEL_WIDTH. */
int panel_effective_width(const Panel *p);

/*  Reads mouse state directly (input_mouse_pos / input_mouse_button_*),
    the same way editor.c does — there's no reason for panel.c to take
    input as parameters when nothing else in this codebase does either.

    Call once per frame, BEFORE editor_update(), so:
      1. button clicks this frame already mutated ed/resources by the
         time editor_update() runs, and
      2. editor_update() can be told the click was already spent (via
         its ui_panel_width parameter) instead of also painting/placing
         under the cursor.

    Returns true if the pointer is currently over the sidebar at all
    (not just over a button) — main.c doesn't need this return value
    itself (editor_update()'s own panel_width check handles the actual
    gating), but it's exposed for symmetry and any future caller that
    wants to know without duplicating the rect test.

    out_action is set to PANEL_ACTION_NONE unless a button was clicked
    this frame; NEW/REGENERATE/SAVE/LOAD/RESIZE are reported back to
    main.c to execute, the same way F5/F9/R are handled today — panel.c
    deliberately doesn't reach into world_save/world_load/world_generate
    itself, so main.c stays the one place that sequences save-file I/O
    across world+entities+sim+weather. */
#include "../simulation/weather.h"
#include "../core/object_def.h"
#include "sprites_tab.h"

bool panel_update(Panel *p, Editor *ed, ResourceStore *resources,
                   WeatherSystem *weather,
                   ObjectDefRegistry *obj_registry, SpritesTab *sprites_tab,
                   int viewport_w, int viewport_h, PanelAction *out_action);

void panel_render(const Panel *p, const Editor *ed, const ResourceStore *resources,
                   const WeatherSystem *weather, const ObjectDefRegistry *obj_registry,
                   int viewport_w, int viewport_h);

#endif /* DGE_PANEL_H */
