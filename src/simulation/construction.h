#ifndef DGE_CONSTRUCTION_H
#define DGE_CONSTRUCTION_H

#include <stdbool.h>
#include "../ecs/registry.h"
#include "../renderer/camera.h"
#include "../core/object_def.h"
#include "simulation.h"

/*  Construction.

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
    system_build_entity() to add one frame's labor.

    Phase 2 (ObjectDef consolidation): the old BuildingKind-driven
    "building_*()" functions (one hardcoded switch-over-enum per
    property, exactly one entry: BUILDING_CAMPFIRE) are retired. Every
    blueprint is now an ObjectDef instance — objdef_is_buildable() /
    objdef_get_build_spec() (core/object_def.h) resolve cost and build
    time from an object's own properties instead of a fixed enum. This
    is "one content system instead of three" made concrete: a project
    defines a buildable object the same way it defines any other
    ObjectDef, no separate BuildingKind concept to learn or extend. */

/* Read-only affordability check — doesn't spend anything. Used by both
   editor.c (refuse placement before committing) and ui.c (cost-preview
   HUD line, colored green/red by affordability). */
bool objdef_can_afford_build(const ResourceStore *rs, const ObjectDef *def);

/* Atomically pays def's cost from rs — whatever resource name its
   build_cost_kind property names (see core/object_def.h;
   RESOURCE_NAME_MAX-sized, Phase 2B retired the old fixed wood/stone
   ResourceKind enum, any name works now) — via
   resource_store_try_spend(). Returns false (no change) if
   unaffordable — same "check and commit together" contract as
   resource_store_try_spend(). */
bool objdef_try_pay_build_cost(ResourceStore *rs, const ObjectDef *def);

/* Undoes objdef_try_pay_build_cost() — the "registry was full after we
   already paid" refund case. A function instead of inline math purely
   so the cost-resolution logic (objdef_get_build_spec) lives in
   exactly one place rather than being re-derived at each call site. */
void objdef_refund_build_cost(ResourceStore *rs, const ObjectDef *def);

/*  Spawns a blueprint entity from an ObjectDef at grid position
    (gx, gy) with ConstructionComponent{build_time_total =
    objdef_get_build_spec()'s resolved time, build_time_done = 0,
    complete = false, def_name = def->name}.

    Does NOT touch the ResourceStore — the caller must already have
    called objdef_try_pay_build_cost() before this, so the economy is
    never left half-committed: if entity_create() happens to fail
    because the registry is full, the caller already paid and is
    responsible for refunding (objdef_refund_build_cost()) on a
    ENTITY_NULL return. Keeping payment and spawning as two separate
    steps the caller sequences itself, rather than one function that
    does both, means there's exactly one place (editor.c's placement
    handler) that has to reason about the refund-on-failure case.

    sprite_id is resolved by the caller the same way
    objdef_spawn_instance() (game/prefabs.h) expects (panel.c already
    has the SpritesTab lookup) — pass SPRITE_NONE if it didn't resolve,
    same fallback as the instant-placement path. */
Entity construction_place_blueprint_objdef(Registry *reg, const ObjectDef *def,
                                            int sprite_id, float gx, float gy);

/*  One frame's worth of labor on a blueprint. Adds labor_seconds to its
    build_time_done; if that crosses build_time_total, flips complete
    to true, clears the blueprint's dimmed/translucent tint back to
    full opacity (there's no separate "finished" sprite the way the old
    BuildingKind path swapped SPRITE_CAMPFIRE_BLUEPRINT for
    SPRITE_CAMPFIRE_COMPLETE — a user-defined object only ever has the
    one sprite it was given), logs completion, and returns true exactly
    once — on the call that completes it. The caller (agent.c) should
    clear the worker's task to TASK_IDLE on a true return.

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
