#ifndef DGE_PANEL_H
#define DGE_PANEL_H

#include <stdbool.h>
#include "../editor/editor.h"
#include "../simulation/simulation.h"

/*  The left sidebar. Before this, editor.c was driven entirely by
    keyboard shortcuts (TAB, 1-9, M, H...) with no visible affordance
    for what any of them did. panel.c doesn't replace that input path —
    every shortcut still works exactly as before — it adds a parallel
    screen-space sidebar that mutates the same Editor/World/Registry/
    ResourceStore state through the same setters, so there is no second
    source of truth for "what mode am I in".

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

#include "textinput.h"

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

    /* PLACE mode's stamp picker. Phase 2 (ObjectDef consolidation)
       changed this from an atlas-wide sprite thumbnail grid into a
       row list of the project's ObjectDefs (same shape as PAINT mode's
       Tileset palette) — sprite_scroll is now a row index into that
       list, not a thumbnail-row index. Name kept as-is since the
       field's *purpose* (how far scrolled in PLACE mode's picker)
       didn't change, only what's being scrolled through. */
    int  sprite_scroll;   /* scroll offset, row index into the ObjectDef list */
    int  hovered_sprite;  /* -1 = none; for hover highlight in render    */

    /* PAINT mode's Tileset palette — this is what used to be five fixed
       terrain swatches. Now it's a live, editable list of the active
       project's own Tileset slots (see world/tileset.h): click a row
       to paint with it, right-click a row to rename it, click a row's
       swatch to (re)assign its sprite, or use the list's own
       "+ ADD TILE" row to define a new one. There is no longer a
       cross-mode RMB-from-PLACE-mode gesture — a slot's sprite is
       assigned right here, in the same place you're already thinking
       about tiles, not borrowed from whatever happened to be selected
       in a different mode. */
    int  tileset_scroll;         /* scroll offset in tileset rows */
    int  renaming_slot;          /* -1 = none; else the slot index whose
                                     name is being edited inline right now
                                     (also used for a freshly-created slot,
                                     so "+ ADD TILE" flows straight into
                                     typing its name) */
    TextInput rename_field;
    int  assigning_sprite_slot;  /* -1 = none; else the slot index whose
                                     sprite picker is currently open,
                                     replacing the tile list temporarily */
    int  assign_sprite_scroll;   /* scroll offset within that picker,
                                     kept separate from sprite_scroll so
                                     opening it doesn't disturb PLACE
                                     mode's own scroll position */
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
#include "../renderer/atlas.h"
#include "../world/world.h"
#include "../core/project.h"
#include "sprites_tab.h"

bool panel_update(Panel *p, Editor *ed, ResourceStore *resources,
                   WeatherSystem *weather,
                   ObjectDefRegistry *obj_registry, SpritesTab *sprites_tab,
                   const SpriteAtlas *atlas, World *world, GenreProfile genre,
                   int viewport_w, int viewport_h, PanelAction *out_action);

void panel_render(const Panel *p, const Editor *ed, const ResourceStore *resources,
                   const WeatherSystem *weather, const ObjectDefRegistry *obj_registry,
                   const SpriteAtlas *atlas, const SpritesTab *sprites_tab,
                   const World *world, GenreProfile genre,
                   int viewport_w, int viewport_h);

#endif /* DGE_PANEL_H */
