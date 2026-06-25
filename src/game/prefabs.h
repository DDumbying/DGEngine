#ifndef DGE_PREFABS_H
#define DGE_PREFABS_H

#include "../ecs/registry.h"
#include "../core/object_def.h"

/*  A "prefab" here is just a function that creates an entity with a
    fixed set of components — no data-driven prefab format yet (that's
    a Simulation/Modding-phase question: do prefabs come from a JSON
    table, a script, or stay hardcoded like this?). For now there are
    exactly two, shared between main.c's demo spawner and the editor's
    placement mode, so the definition lives in one place. */
typedef enum {
    PREFAB_TREE = 0,
    PREFAB_ROCK,
    PREFAB_WORKER,
    PREFAB_COUNT
} PrefabKind;

/* Human-readable name, for log messages. */
const char *prefab_name(PrefabKind kind);

/* Creates an entity at grid position (gx, gy) with the given prefab's
   components. Returns ENTITY_NULL if the registry is full. */
Entity prefab_spawn(Registry *reg, PrefabKind kind, float gx, float gy);

/*  Phase L->World: the data-driven counterpart to prefab_spawn() above
    — creates an entity from a user-defined ObjectDef instead of a
    hardcoded PrefabKind. Lives here (not in core/object_def.c) because
    spawning is ECS- and atlas-aware territory (Registry, sprite_id),
    which core/ deliberately stays free of — object_def.c only knows
    how to read/write/hold ObjectDef data, never how to turn one into
    live entities.

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
    HealthComponent; a string property named "drops" set to "wood" or
    "stone" becomes a ResourceComponent (yield from an int property
    named "yield", default 10 if absent). This is what makes "the
    properties you set in the Objects tab actually do something" true
    instead of cosmetic — every other property is still recorded in
    the ObjectDef and visible in the Inspector, it just isn't engine
    behavior (that's Phase N's job, once scripts can read them). */
Entity objdef_spawn_instance(Registry *reg, const ObjectDef *def, int sprite_id,
                              float gx, float gy);

#endif /* DGE_PREFABS_H */
