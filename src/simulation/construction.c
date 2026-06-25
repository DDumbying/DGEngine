#include "construction.h"

#include "../core/log.h"
#include "../renderer/renderer.h"
#include "../renderer/atlas.h"

/* ---------------------------------------------------------------------
   Building catalog. One BuildingKind today; every entry is a switch so
   a second building extends these, not a redesign. */

#define CAMPFIRE_COST_WOOD   20
#define CAMPFIRE_BUILD_TIME  6.0f /* seconds of worker labor */

const char *building_name(BuildingKind kind) {
    switch (kind) {
        case BUILDING_CAMPFIRE: return "campfire";
        default:                 return "unknown";
    }
}

ResourceKind building_cost_kind(BuildingKind kind) {
    switch (kind) {
        case BUILDING_CAMPFIRE: return RESOURCE_WOOD;
        default:                 return RESOURCE_WOOD;
    }
}

int building_cost_amount(BuildingKind kind) {
    switch (kind) {
        case BUILDING_CAMPFIRE: return CAMPFIRE_COST_WOOD;
        default:                 return 0;
    }
}

float building_build_time(BuildingKind kind) {
    switch (kind) {
        case BUILDING_CAMPFIRE: return CAMPFIRE_BUILD_TIME;
        default:                 return 0.0f;
    }
}

bool building_can_afford(const ResourceStore *rs, BuildingKind kind) {
    int amount = building_cost_amount(kind);
    return building_cost_kind(kind) == RESOURCE_WOOD
         ? resource_store_has_wood(rs, amount)
         : resource_store_has_stone(rs, amount);
}

bool building_try_pay_cost(ResourceStore *rs, BuildingKind kind) {
    int amount = building_cost_amount(kind);
    return building_cost_kind(kind) == RESOURCE_WOOD
         ? resource_store_try_spend_wood(rs, amount)
         : resource_store_try_spend_stone(rs, amount);
}

/* ---------------------------------------------------------------------
   Visuals. Blueprint = dim/translucent outline of the finished color;
   finished = full opacity, slightly larger. Kept here (not prefabs.c)
   since these are construction states of a building, not a standalone
   spawnable prefab. */

static RenderableComponent blueprint_look(BuildingKind kind) {
    switch (kind) {
        case BUILDING_CAMPFIRE:
            return (RenderableComponent){1.0f, 1.0f, 1.0f, 0.55f, 28.0f, 28.0f, SPRITE_CAMPFIRE_BLUEPRINT};
        default:
            return (RenderableComponent){0.5f, 0.5f, 0.5f, 0.45f, 28.0f, 28.0f, SPRITE_NONE};
    }
}

static RenderableComponent finished_look(BuildingKind kind) {
    switch (kind) {
        case BUILDING_CAMPFIRE:
            return (RenderableComponent){1.0f, 1.0f, 1.0f, 1.0f, 28.0f, 32.0f, SPRITE_CAMPFIRE_COMPLETE};
        default:
            return (RenderableComponent){0.5f, 0.5f, 0.5f, 1.0f, 28.0f, 28.0f, SPRITE_NONE};
    }
}

/* ---------------------------------------------------------------------
   Placement, labor, rendering. */

Entity construction_place_blueprint(Registry *reg, BuildingKind kind, float gx, float gy) {
    Entity e = entity_create(reg);
    if (e == ENTITY_NULL) return ENTITY_NULL;

    entity_add_transform(reg, e, (TransformComponent){gx, gy});
    entity_add_renderable(reg, e, blueprint_look(kind));

    ConstructionComponent c;
    c.kind             = kind;
    c.build_time_total = building_build_time(kind);
    c.build_time_done  = 0.0f;
    c.complete         = false;
    entity_add_construction(reg, e, c);

    return e;
}

bool system_build_entity(Registry *reg, Entity e, float labor_seconds) {
    if (!entity_alive(reg, e)) return false;

    ConstructionComponent *c = entity_get_construction(reg, e);
    if (!c || c->complete) return false;

    c->build_time_done += labor_seconds;
    if (c->build_time_done >= c->build_time_total) {
        c->build_time_done = c->build_time_total;
        c->complete = true;
        entity_add_renderable(reg, e, finished_look(c->kind));
        LOG_INFO("Entity %u finished building %s", e, building_name(c->kind));
        return true;
    }
    return false;
}

void construction_render(Registry *reg, const Camera *cam) {
    (void)cam;

    const float BAR_W = 24.0f;
    const float BAR_H = 4.0f;
    const float BAR_Y_OFFSET = 26.0f; /* px above the tile's screen center */

    for (Entity e = 0; e < (Entity)MAX_ENTITIES; e++) {
        if (!reg->alive[e] || !reg->has_construction[e] || !reg->has_transform[e])
            continue;

        const ConstructionComponent *c = &reg->construction[e];
        if (c->complete) continue;

        const TransformComponent *t = &reg->transform[e];
        float sx, sy;
        renderer_grid_to_screen(t->x, t->y, &sx, &sy);

        float frac = c->build_time_total > 0.0f
                   ? c->build_time_done / c->build_time_total : 0.0f;
        if (frac < 0.0f) frac = 0.0f;
        if (frac > 1.0f) frac = 1.0f;

        float bar_x = sx - BAR_W * 0.5f;
        float bar_y = sy + BAR_Y_OFFSET;

        /* Background track, then the filled portion on top. */
        renderer_draw_quad(bar_x, bar_y, BAR_W, BAR_H, 0.15f, 0.15f, 0.15f, 0.75f);
        if (frac > 0.0f)
            renderer_draw_quad(bar_x, bar_y, BAR_W * frac, BAR_H, 0.95f, 0.65f, 0.20f, 0.95f);
    }
}
