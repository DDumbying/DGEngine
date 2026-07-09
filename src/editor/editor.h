#ifndef DGE_EDITOR_H
#define DGE_EDITOR_H

#include <stdbool.h>
#include "../ecs/registry.h"
#include "../world/world.h"
#include "../world/spatial_grid.h"
#include "../renderer/camera.h"
#include "../game/prefabs.h"
#include "../simulation/simulation.h"
#include "../core/object_def.h"

/*  Mode/brush switches still announce themselves via LOG_INFO (kept —
    useful when running without a window, e.g. piping logs). ui/ui.c
    now also renders the current mode/brush on screen each frame via
    editor_mode_name()/tileset_name_for() below, so the console log and
    the HUD always agree — one source of truth. */
typedef enum {
    EDITOR_MODE_PAINT = 0,  /* left-click+drag paints a Tileset slot   */
    EDITOR_MODE_PLACE,      /* left-click places an ObjectDef, right deletes */
    EDITOR_MODE_SELECT,     /* left-click selects, prints entity info  */
    EDITOR_MODE_SHAPE,      /* LMB enables tiles, RMB disables (holes) */
    EDITOR_MODE_COUNT
} EditorMode;

typedef struct {
    EditorMode mode;

    /* PAINT mode's active brush — an index into the active project's
       Tileset (see world/tileset.h), NOT a TerrainType enum anymore.
       -1 means nothing is selected: the honest starting state for a
       project whose Tileset is still empty, since there's nothing to
       paint with until at least one tile type has been defined.
       (This retires TileSpriteMap/the old RMB-assign-from-PLACE-mode
       gesture entirely — PAINT mode's own palette now owns assigning
       a sprite to a slot directly, see ui/panel.c.) */
    int brush;

    /* PLACE mode's active stamp — the name of an ObjectDef (Objects
       tab) to spawn on click. Empty string means nothing is selected:
       the same honest "nothing to place until something's defined"
       starting state as an empty brush above, and the same starting
       state as a fresh project's empty Tileset (world/tileset.h).

       Phase 2 (ObjectDef consolidation) retired PrefabKind and
       BuildingKind — this is now the ONLY way PLACE mode places
       anything. objdef_is_buildable() (core/object_def.h) is what
       decides, per-def, whether placing it drops an instant entity
       (objdef_spawn_instance(), game/prefabs.h) or a resource-costed
       construction blueprint (construction_place_blueprint_objdef(),
       simulation/construction.h) — editor.c branches on that at the
       point of placement, not on a field here. place_sprite_id is
       resolved once by panel.c's palette (which already has the
       SpritesTab name->id lookup) when the row is clicked, so editor.c
       itself never needs to know SpritesTab exists either — same
       "carry the already-resolved value" shape the old PrefabKind
       path used. */
    char place_def_name[OBJDEF_NAME_MAX];
    int  place_sprite_id;

    EntityHandle selected;  /* ENTITY_HANDLE_NULL when nothing selected */

    /* Hovered tile this frame, kept around so editor_render() can draw
       the same highlight editor_update() used to decide what to act on. */
    bool hover_valid;
    int  hover_gx, hover_gy;
} Editor;

void editor_init(Editor *ed);

/* Human-readable mode name, exposed for the HUD (ui/ui.c) as well as
   the editor's own LOG_INFO lines — one source of truth for both.
   (There's no equivalent terrain_name() anymore — a brush's name comes
   from the active project's own Tileset via tileset_name_for(), since
   the engine no longer has a fixed list of terrain names to draw from.) */
const char *editor_mode_name(EditorMode m);

/* Reads keyboard/mouse state and mutates world/registry accordingly.
   Call once per frame, after input_begin_frame()/event polling and
   after the camera's viewport size is current for this frame.

   ui_panel_width is PANEL_WIDTH from ui/panel.h (not included here to
   avoid a circular header — panel.h already includes this header for
   the Editor type it mutates). Any mouse position with x < ui_panel_width
   is treated as "no tile hovered", so a click on the sidebar can never
   also paint/place/select on whatever world tile happens to be behind
   it. Pass 0 to disable this (e.g. from a test harness with no panel).

   ui_top_margin is the same idea for the menu bar above
   the World view (TOP_BAR_H, see ui/menubar.h)
   — any mouse position with y < ui_top_margin is likewise treated as
   "no tile hovered". Without this, clicking a tab (or anything else in
   that strip) also paints/places/selects on whatever world tile
   happens to be behind it.

   ui_right_margin mirrors ui_panel_width for a panel docked on the
   RIGHT edge instead of the left (currently: ui/shape_pane.h's flat-
   grid mask painter, shown only in EDITOR_MODE_SHAPE). Any mouse
   position with x >= ui_right_margin is treated as "no tile hovered".
   Pass 0 to disable this (the convention every non-Shape-mode caller
   uses). */
void editor_update(Editor *ed, Registry *reg, World *world, const Camera *cam,
                    ResourceStore *resources, SpatialGrid *sgrid,
                    int ui_panel_width, int ui_top_margin, int ui_right_margin);

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
