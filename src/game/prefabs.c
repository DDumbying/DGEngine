#include "prefabs.h"
#include "../renderer/atlas.h"
#include <string.h>
#include <stdio.h>
#include "../core/log.h"

const char *prefab_name(PrefabKind kind) {
    switch (kind) {
        case PREFAB_TREE:   return "tree";
        case PREFAB_ROCK:   return "rock";
        case PREFAB_WORKER: return "worker";
        default:            return "unknown";
    }
}

Entity prefab_spawn(Registry *reg, PrefabKind kind, float gx, float gy) {
    Entity e = entity_create(reg);
    if (e == ENTITY_NULL) return ENTITY_NULL;

    entity_add_transform(reg, e, (TransformComponent){gx, gy});

    switch (kind) {
        case PREFAB_TREE:
            entity_add_renderable(reg, e, (RenderableComponent){
                1.0f, 1.0f, 1.0f, 1.0f, 28.0f, 40.0f, SPRITE_TREE});
            entity_add_health(reg, e, (HealthComponent){100, 100});
            entity_add_resource(reg, e, (ResourceComponent){RESOURCE_WOOD, 25});
            break;

        case PREFAB_ROCK:
            entity_add_renderable(reg, e, (RenderableComponent){
                1.0f, 1.0f, 1.0f, 1.0f, 28.0f, 24.0f, SPRITE_ROCK});
            entity_add_health(reg, e, (HealthComponent){100, 100});
            entity_add_resource(reg, e, (ResourceComponent){RESOURCE_STONE, 20});
            break;

        case PREFAB_WORKER: {
            entity_add_renderable(reg, e, (RenderableComponent){
                1.0f, 1.0f, 1.0f, 1.0f, 20.0f, 32.0f, SPRITE_WORKER});
            entity_add_health(reg, e, (HealthComponent){100, 100});

            MoveComponent m;
            m.speed    = 3.0f;
            m.progress = 0.0f;
            m.src_x    = (int)gx;  m.src_y = (int)gy;
            m.dst_x    = (int)gx;  m.dst_y = (int)gy;
            m.moving   = false;
            entity_add_move(reg, e, m);

            TaskComponent t;
            t.kind      = TASK_IDLE;
            t.target_x  = (int)gx;  t.target_y = (int)gy;
            t.path.len  = 0;
            t.path_step = 0;
            t.timer     = 0.0f;
            entity_add_task(reg, e, t);
            break;
        }

        default:
            break;
    }
    return e;
}

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

    /* Default box size matches the worker prefab's footprint — a
       reasonable default for "some object" until Phase K/L grow a way
       to set per-object size explicitly. White tint: the sprite (if
       resolved) carries its own color, so there's nothing to tint. */
    entity_add_renderable(reg, e, (RenderableComponent){
        1.0f, 1.0f, 1.0f, 1.0f, 24.0f, 32.0f, sprite_id});

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

    const ObjectProperty *drops = find_prop(def, "drops", PROP_STRING);
    if (drops) {
        ResourceKind kind;
        bool recognized = true;
        if (strcmp(drops->value.as_string, "wood") == 0)       kind = RESOURCE_WOOD;
        else if (strcmp(drops->value.as_string, "stone") == 0) kind = RESOURCE_STONE;
        else { recognized = false; kind = RESOURCE_WOOD; }

        if (recognized) {
            const ObjectProperty *yield = find_prop(def, "yield", PROP_INT);
            int amount = yield ? yield->value.as_int : 10;
            entity_add_resource(reg, e, (ResourceComponent){kind, amount});
        } else {
            LOG_WARN("objdef_spawn_instance: '%s' has drops=\"%s\" — only "
                     "\"wood\"/\"stone\" are recognized, ignoring",
                     def->name, drops->value.as_string);
        }
    }

    return e;
}
