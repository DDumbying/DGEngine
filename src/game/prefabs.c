#include "prefabs.h"

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
                0.16f, 0.34f, 0.14f, 1.0f, 14.0f, 30.0f});
            entity_add_health(reg, e, (HealthComponent){100, 100});
            entity_add_resource(reg, e, (ResourceComponent){RESOURCE_WOOD, 25});
            break;

        case PREFAB_ROCK:
            entity_add_renderable(reg, e, (RenderableComponent){
                0.55f, 0.55f, 0.58f, 1.0f, 18.0f, 14.0f});
            entity_add_health(reg, e, (HealthComponent){100, 100});
            entity_add_resource(reg, e, (ResourceComponent){RESOURCE_STONE, 20});
            break;

        case PREFAB_WORKER: {
            entity_add_renderable(reg, e, (RenderableComponent){
                0.90f, 0.40f, 0.20f, 1.0f, 16.0f, 24.0f});
            entity_add_health(reg, e, (HealthComponent){100, 100});
            
            MoveComponent m;
            m.speed = 3.0f; /* 3 tiles per second */
            m.progress = 0.0f;
            m.src_x = (int)gx;
            m.src_y = (int)gy;
            m.dst_x = (int)gx;
            m.dst_y = (int)gy;
            m.moving = false;
            entity_add_move(reg, e, m);

            TaskComponent t;
            t.kind = TASK_IDLE;
            t.target_x = (int)gx;
            t.target_y = (int)gy;
            t.path.len = 0;
            t.path_step = 0;
            t.timer = 0.0f;
            entity_add_task(reg, e, t);
            break;
        }

        default:
            break;
    }
    return e;
}
