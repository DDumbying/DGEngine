
#ifndef DGE_HARVEST_H
#define DGE_HARVEST_H

/*  Phase 5 — Harvest system
    Operates on entities that have all three of:
      Transform, Health, Resource

    One call to system_harvest_entity() per player action (not per-frame
    continuous) is the intended use:
      - Reduces entity health by ResourceComponent.yield_per_hit
      - If health drops to 0: destroys the entity, credits the full
        yield to the ResourceStore, returns true to signal "entity died"
      - If health > 0 after the hit: credits a partial yield (proportional
        damage done), returns false

    Returns false if the entity doesn't exist or doesn't have the
    required components.

    This is a one-shot function keyed by EntityHandle so the caller (the
    editor's SELECT mode) can pass its held handle and get a definitive
    answer back, including safe detection of a stale handle. */

#include <stdbool.h>
#include "../ecs/registry.h"
#include "simulation.h"

/*  Returns true if the entity was destroyed (health == 0 after this hit).
    On destruction the entity is removed from the registry automatically.
    Either way, resources are credited to `rs`. */
bool system_harvest_entity(Registry *reg, EntityHandle h, ResourceStore *rs);

#endif /* DGE_HARVEST_H */
