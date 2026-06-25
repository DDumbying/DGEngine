#ifndef DGE_EDITOR_H
#define DGE_EDITOR_H

#include <stdbool.h>
#include "../ecs/registry.h"
#include "../world/world.h"
#include "../renderer/camera.h"
#include "../game/prefabs.h"
#include "../simulation/simulation.h"
#include "../core/object_def.h"

/*  Mode/brush/prefab switches still announce themselves via LOG_INFO
    (kept — useful when running without a window, e.g. piping logs).
    ui/ui.c now also renders the current mode/brush/prefab on screen
    each frame via editor_mode_name()/terrain_name() below, so the
    console log and the HUD always agree — one source of truth. */
typedef enum {
    EDITOR_MODE_PAINT = 0,  /* left-click+drag paints terrain          */
    EDITOR_MODE_PLACE,      /* left-click places a prefab, right deletes */
    EDITOR_MODE_SELECT,     /* left-click selects, prints entity info  */
    EDITOR_MODE_COUNT
} EditorMode;

typedef struct {
    EditorMode   mode;
    TerrainType  brush;     /* active terrain type in PAINT mode */
    PrefabKind   prefab;    /* active prefab in PLACE mode, when !placing_building */

    /* PLACE mode places either a free instant PrefabKind (tree/rock/
       worker — no cost, no construction time) or a BuildingKind
       blueprint (costs resources, needs a worker to build it over
       time). placing_building picks which of the two number-key rows
       is active; building selects which one when it's the latter. */
    bool         placing_building;
    BuildingKind building;

    /* PLACE mode places either a free instant PrefabKind (tree/rock/
       worker — no cost, no construction time) or a BuildingKind
       blueprint (costs resources, needs a worker to build it over
       time). placing_building picks which of the two number-key rows
       is active; building selects which one when it's the latter.

       A third option, placing_custom, places a user-defined ObjectDef
       (Phase L's Objects tab) the same instant way as a prefab — no
       cost/construction system involvement, since that machinery is
       still BuildingKind-specific (see simulation/construction.h).
       place_def_name/place_sprite_id are resolved once by panel.c when
       the object's button is clicked (panel.c already has the
       ObjectDefRegistry + SpritesTab needed to do that lookup), so
       editor.c itself never needs to know either of those types exist
       — it just carries the already-resolved name + sprite id, same
       spirit as already-resolved PrefabKind/BuildingKind enums. At
       most one of placing_building/placing_custom is true at a time. */
    bool         placing_custom;
    char         place_def_name[OBJDEF_NAME_MAX];
    int          place_sprite_id;

    EntityHandle selected;  /* ENTITY_HANDLE_NULL when nothing selected */

    /* Hovered tile this frame, kept around so editor_render() can draw
       the same highlight editor_update() used to decide what to act on. */
    bool hover_valid;
    int  hover_gx, hover_gy;
} Editor;

void editor_init(Editor *ed);

/* Human-readable names, exposed for the HUD (ui/ui.c) as well as the
   editor's own LOG_INFO lines — one source of truth for both. */
const char *editor_mode_name(EditorMode m);
const char *terrain_name(TerrainType t);

/* Reads keyboard/mouse state and mutates world/registry accordingly.
   Call once per frame, after input_begin_frame()/event polling and
   after the camera's viewport size is current for this frame.

   Takes ResourceStore now (Phase 5b/Construction) — every other
   resource-touching action (harvesting, via H) is deliberately
   orchestrated from main.c instead, specifically so editor.c didn't
   need to know ResourceStore existed. Building placement breaks that:
   it's the same mouse click that already places trees/rocks/workers
   in PLACE mode, just with a cost check first, so splitting "pick what
   to place" (editor.c) from "commit the placement" (main.c) for
   buildings only would scatter one mode's logic across two files for
   no real benefit. One extra pointer here was the smaller cost. */
/* ui_panel_width is PANEL_WIDTH from ui/panel.h (not included here to
   avoid a circular header — panel.h already includes this header for
   the Editor type it mutates). Any mouse position with x < ui_panel_width
   is treated as "no tile hovered", so a click on the sidebar can never
   also paint/place/select on whatever world tile happens to be behind
   it. Pass 0 to disable this (e.g. from a test harness with no panel).

   ui_top_margin is the same idea for the tab bar + status row above
   the World view (TABBAR_H + STATUS_BAR_H, see ui/tabbar.h and ui/ui.h)
   — any mouse position with y < ui_top_margin is likewise treated as
   "no tile hovered". Without this, clicking a tab (or anything else in
   that strip) also paints/places/selects on whatever world tile
   happens to be behind it. */
void editor_update(Editor *ed, Registry *reg, World *world, const Camera *cam,
                    ResourceStore *resources, int ui_panel_width, int ui_top_margin);

/* Draws ground-level highlights only (hover + selection markers).
   Call between world_render() and system_render_entities() — that
   ordering matters: highlights are flat tiles, so anything drawn after
   them (entities) renders on top, the way it should for a marker
   *under* an object rather than an overlay on top of it. Takes the
   registry (read-only) so the selection highlight is resolved through
   entity_handle_valid() each frame instead of trusting a cached
   position that could go stale once something other than the editor
   is able to destroy entities (Simulation/AI, later phases). */
void editor_render(const Editor *ed, Registry *reg);

#endif /* DGE_EDITOR_H */
