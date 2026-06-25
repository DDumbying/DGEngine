#ifndef DGE_CONSTRUCTION_H
#define DGE_CONSTRUCTION_H

#include <stdbool.h>
#include "../ecs/registry.h"
#include "../renderer/camera.h"
#include "simulation.h"

/*  Construction (the deferred half of the original Phase 5 plan).

    A blueprint is an ordinary entity — Transform + Renderable (dim,
    semi-transparent "outline" look) + ConstructionComponent with
    complete=false — placed via the editor's PLACE mode. Placing one
    pays its cost immediately from the ResourceStore: editor.c does the
    pay-or-refuse (it already holds the ResourceStore pointer), this
    module just knows what things cost, so editor.c never has to branch
    on which specific resource a building needs.

    A worker must then be commanded (SELECT mode, M, hovering the
    blueprint) to TASK_BUILD it. Each frame the worker's agent-system
    tick finds it adjacent and still incomplete, it calls
    system_build_entity() to add one frame's labor. There's exactly one
    BuildingKind today (BUILDING_CAMPFIRE); every function here is
    written as a switch over BuildingKind specifically so adding a
    second building later means extending these switches, not
    redesigning the placement/payment/labor flow around them. */

const char  *building_name(BuildingKind kind);
ResourceKind building_cost_kind(BuildingKind kind);
int          building_cost_amount(BuildingKind kind);
float        building_build_time(BuildingKind kind);

/* Read-only affordability check — doesn't spend anything. Used by both
   editor.c (refuse placement before committing) and ui.c (cost-preview
   HUD line, colored green/red by affordability). */
bool building_can_afford(const ResourceStore *rs, BuildingKind kind);

/* Atomically pays kind's cost from rs (wood or stone, whichever it
   costs) via resource_store_try_spend_wood/stone. Returns false (no
   change) if unaffordable — same "check and commit together" contract
   as resource_store_try_spend_wood/stone. */
bool building_try_pay_cost(ResourceStore *rs, BuildingKind kind);

/*  Spawns a blueprint entity at grid position (gx, gy) with
    ConstructionComponent{kind, build_time_total = building_build_time(kind),
    build_time_done = 0, complete = false}.

    Does NOT touch the ResourceStore — the caller must already have
    called building_try_pay_cost() (or equivalent) before this, so the
    economy is never left half-committed: if entity_create() happens to
    fail because the registry is full, the caller already paid and is
    responsible for refunding (e.g. resource_store_add_wood to give the
    cost back) on a ENTITY_NULL return. Keeping payment and spawning as
    two separate steps the caller sequences itself, rather than one
    function that does both, means there's exactly one place
    (editor.c's placement handler) that has to reason about the
    refund-on-failure case. */
Entity construction_place_blueprint(Registry *reg, BuildingKind kind, float gx, float gy);

/*  One frame's worth of labor on a blueprint. Adds labor_seconds to its
    build_time_done; if that crosses build_time_total, flips complete
    to true, swaps the entity's RenderableComponent to the finished
    look, logs completion, and returns true exactly once — on the call
    that completes it. The caller (agent.c) should clear the worker's
    task to TASK_IDLE on a true return.

    Returns false on every other call: entity doesn't exist, has no
    ConstructionComponent, or is already complete. The "already
    complete" case is a harmless no-op rather than an error, so agent.c
    doesn't need to special-case "this blueprint finished last frame"
    before calling in. */
bool system_build_entity(Registry *reg, Entity e, float labor_seconds);

/*  Draws a small progress bar floating above every incomplete
    blueprint's tile. World-space (pans/zooms with the camera via
    renderer_grid_to_screen(), which reads the projection
    renderer_begin() already set up) — call between
    system_render_entities() and renderer_begin_ui(), the same slot
    weather_render() occupies. cam is currently unused (kept for
    signature symmetry with weather_render(), which has the identical
    "camera not needed yet but matches the pattern" comment) in case a
    future effect needs camera-relative info (e.g. distance fade). */
void construction_render(Registry *reg, const Camera *cam);

#endif /* DGE_CONSTRUCTION_H */
