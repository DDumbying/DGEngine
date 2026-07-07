#include "prefabs.h"
#include "../renderer/atlas.h"
#include <string.h>
#include <stdio.h>
#include "../core/log.h"

/* Looks up a property by name + expected type. Returns NULL if absent
   or present with a different type — callers treat "wrong type" the
   same as "absent" (e.g. a string property named "health" just isn't
   a health value) rather than erroring, since the Objects tab doesn't
   stop you from naming things ambiguously. */
static const ObjectProperty *find_prop(const ObjectDef *def, const char *name, PropertyType type) {
    for (int i = 0; i < def->prop_count; i++) {
        if (def->props[i].type == type && strcmp(def->props[i].name, name) == 0)
            return &def->props[i];
    }
    return NULL;
}

Entity objdef_spawn_instance(Registry *reg, const ObjectDef *def, int sprite_id,
                              float gx, float gy) {
    Entity e = entity_create(reg);
    if (e == ENTITY_NULL) return ENTITY_NULL;

    entity_add_transform(reg, e, (TransformComponent){gx, gy});

    /* Default box size — a reasonable placeholder until a project
       needs a way to set per-object footprint explicitly (see
       ENGINE_DESIGN.md §17's multi-tile-object note). White tint: the
       sprite (if resolved) carries its own color, so there's nothing
       to tint. */
    entity_add_renderable(reg, e, (RenderableComponent){
        1.0f, 1.0f, 1.0f, 1.0f, 24.0f, 32.0f, sprite_id, 1, 0.0f, 0.0f, 0});

    DefinitionComponent d;
    memset(&d, 0, sizeof(d));
    snprintf(d.def_name, sizeof(d.def_name), "%s", def->name);
    entity_add_definition(reg, e, d);

    /* Property -> component mapping — see the doc comment on
       objdef_spawn_instance() in prefabs.h for the convention. */
    const ObjectProperty *health = find_prop(def, "health", PROP_INT);
    if (health) {
        int hp = health->value.as_int;
        entity_add_health(reg, e, (HealthComponent){hp, hp});
    }

    /* "drops" now accepts any resource name — Phase 2B retired the old
       ResourceKind enum that limited this to "wood"/"stone" specifically.
       A project can name its own resources ("gold", "mana", whatever)
       and this just carries that name straight into the
       ResourceComponent; resource_store_add() (simulation.h) handles
       "have I seen this name before" itself when the entity is
       eventually harvested. */
    const ObjectProperty *drops = find_prop(def, "drops", PROP_STRING);
    if (drops && drops->value.as_string[0]) {
        const ObjectProperty *yield = find_prop(def, "yield", PROP_INT);
        int amount = yield ? yield->value.as_int : 10;
        ResourceComponent rc;
        /* Manual bounded copy instead of snprintf("%s", ...) — the
           source (an ObjectProperty string, up to 63 bytes) can be
           wider than RESOURCE_NAME_MAX (32), and truncating via
           snprintf here is completely safe (it always NUL-terminates
           within the given size) but gcc's -Wformat-truncation can't
           see that at compile time, since it can't know the source
           string's actual runtime length. Same reasoning/pattern as
           tileset_strcpy() in world/tileset.h. */
        int ki = 0;
        for (; ki + 1 < (int)sizeof(rc.kind) && drops->value.as_string[ki]; ki++)
            rc.kind[ki] = drops->value.as_string[ki];
        rc.kind[ki] = '\0';
        rc.yield_per_hit = amount;
        entity_add_resource(reg, e, rc);
    }

    return e;
}
