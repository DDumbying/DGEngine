#ifndef DGE_PREFABS_H
#define DGE_PREFABS_H

#include "../ecs/registry.h"

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

#endif /* DGE_PREFABS_H */
