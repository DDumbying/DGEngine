
#include "harvest.h"

#include <string.h>
#include "../core/log.h"

bool system_harvest_entity(Registry *reg, EntityHandle h, ResourceStore *rs) {
    if (!entity_handle_valid(reg, h)) return false;

    Entity e = h.index;

    HealthComponent   *health   = entity_get_health(reg, e);
    ResourceComponent *resource = entity_get_resource(reg, e);

    /* Entity must have both Health and Resource to be harvestable. */
    if (!health || !resource) {
        LOG_WARN("system_harvest_entity: entity %u is not harvestable "
                 "(missing Health or Resource component)", e);
        return false;
    }

    /* Deal one harvest hit.  Clamp to 0 — never go negative. */
    int damage = resource->yield_per_hit;
    if (damage > health->current) damage = health->current;
    health->current -= damage;

    LOG_INFO("Harvested entity %u: -%d hp (%d/%d remaining)",
             e, damage, health->current, health->max);

    if (health->current <= 0) {
        /* Entity is fully depleted — credit full yield and remove it.
           Phase 2B: resource->kind is a plain name now (any string a
           project wants), not a 2-value enum — resource_store_add()
           handles "have I seen this name before" itself, so there's no
           branch here at all anymore. */
        resource_store_add(rs, resource->kind, resource->yield_per_hit);
        LOG_INFO("Entity %u depleted — gained %d %s, entity removed",
                 e, resource->yield_per_hit, resource->kind);
        entity_destroy(reg, e);
        return true;   /* caller's EntityHandle is now stale */
    }

    /* Entity still alive — credit partial yield (the damage done). */
    resource_store_add(rs, resource->kind, damage);

    return false;
}
