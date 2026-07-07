#ifndef DGE_PREFABS_H
#define DGE_PREFABS_H

#include "../ecs/registry.h"
#include "../core/object_def.h"

/*  Phase 2 (ObjectDef consolidation): PrefabKind — the hardcoded
    tree/rock/worker enum and its prefab_spawn()/prefab_name() — has
    been retired. Every placeable thing is an ObjectDef now (see
    core/object_def.h); PLACE mode's palette lists whatever the active
    project has defined in its objects/ folder, exactly the same shape
    PAINT mode's palette lists whatever the project has defined in its
    Tileset (see world/tileset.h, Phase 1). A fresh project's
    ObjectDefRegistry starts empty — same "honest, no hidden default
    content" reasoning as a fresh project's empty Tileset.

    This file now holds exactly one function: the data-driven spawn
    path. It lives here (not core/object_def.c) because spawning is
    ECS- and atlas-aware territory (Registry, sprite_id), which core/
    deliberately stays free of — object_def.c only knows how to
    read/write/hold ObjectDef data, never how to turn one into a live
    entity. */

/*  Creates an entity from a user-defined ObjectDef. Returns
    ENTITY_NULL if the registry is full.

    sprite_id is resolved by the caller (main.c/panel.c, which already
    has the SpritesTab name->id table — see sprites_tab_find_id()) and
    passed in rather than looked up here, so this function doesn't need
    to know SpritesTab exists either. Pass SPRITE_NONE if the def's
    sprite name doesn't resolve to anything (e.g. renamed/deleted) —
    the entity still gets placed, just with the renderer's flat-color
    fallback instead of a missing sprite silently failing to spawn at
    all.

    Property -> component mapping (see object_def.c's own comment for
    the full convention): an int property named "health" becomes a
    HealthComponent; a string property named "drops" set to any
    resource name (Phase 2B retired the old wood/stone-only
    ResourceKind enum — "wood", "gold", "mana", whatever a project
    wants) becomes a ResourceComponent (yield from an int property
    named "yield", default 10 if absent). This is what makes "the
    properties you set in the Objects tab actually do something" true
    instead of cosmetic — every other property is still recorded in
    the ObjectDef and visible in the Inspector, it just isn't engine
    behavior (that's a scripting-phase job, once scripts can read
    them). */
Entity objdef_spawn_instance(Registry *reg, const ObjectDef *def, int sprite_id,
                              float gx, float gy);

#endif /* DGE_PREFABS_H */
