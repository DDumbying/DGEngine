#include "construction.h"

#include <string.h>
#include <stdio.h>
#include "../core/log.h"
#include "../renderer/renderer.h"
#include "../renderer/atlas.h"

/* ---------------------------------------------------------------------
   Cost / afford / pay — all resolved from an ObjectDef's own
   properties via objdef_get_build_spec() (core/object_def.h), never
   from a hardcoded enum. See construction.h's doc comment for why the
   old BuildingKind-driven versions of these three functions retired. */

bool objdef_can_afford_build(const ResourceStore *rs, const ObjectDef *def) {
    char kind[RESOURCE_NAME_MAX]; int amount; float build_time;
    objdef_get_build_spec(def, kind, sizeof(kind), &amount, &build_time);
    (void)build_time;
    return resource_store_has(rs, kind, amount);
}

bool objdef_try_pay_build_cost(ResourceStore *rs, const ObjectDef *def) {
    char kind[RESOURCE_NAME_MAX]; int amount; float build_time;
    objdef_get_build_spec(def, kind, sizeof(kind), &amount, &build_time);
    (void)build_time;
    return resource_store_try_spend(rs, kind, amount);
}

void objdef_refund_build_cost(ResourceStore *rs, const ObjectDef *def) {
    char kind[RESOURCE_NAME_MAX]; int amount; float build_time;
    objdef_get_build_spec(def, kind, sizeof(kind), &amount, &build_time);
    (void)build_time;
    resource_store_add(rs, kind, amount);
}

/* ---------------------------------------------------------------------
   Placement, labor, rendering. */

Entity construction_place_blueprint_objdef(Registry *reg, const ObjectDef *def,
                                            int sprite_id, float gx, float gy) {
    Entity e = entity_create(reg);
    if (e == ENTITY_NULL) return ENTITY_NULL;

    entity_add_transform(reg, e, (TransformComponent){gx, gy});

    /* Blueprint look: the object's real sprite, dimmed/translucent so
       it reads as "not finished yet" — a user-defined object only ever
       defines one sprite, so the "in progress" look comes from
       tint/alpha rather than a second swapped sprite_id. */
    entity_add_renderable(reg, e, (RenderableComponent){
        0.65f, 0.7f, 1.0f, 0.55f, 24.0f, 32.0f, sprite_id, 1, 0.0f, 0.0f, 0});

    char cost_kind[RESOURCE_NAME_MAX]; int cost_amount; float build_time;
    objdef_get_build_spec(def, cost_kind, sizeof(cost_kind), &cost_amount, &build_time);
    (void)cost_kind; (void)cost_amount; /* already spent by the caller before this */

    ConstructionComponent c;
    memset(&c, 0, sizeof(c));
    c.build_time_total = build_time;
    c.build_time_done   = 0.0f;
    c.complete          = false;
    snprintf(c.def_name, sizeof(c.def_name), "%s", def->name);
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

        /* No second sprite to swap to (see the blueprint-look comment
           in construction_place_blueprint_objdef) — just clear the
           dimmed/translucent tint back to full opacity, keeping
           whatever sprite_id was already there. */
        RenderableComponent *rd = entity_get_renderable(reg, e);
        if (rd) { rd->r = rd->g = rd->b = rd->a = 1.0f; }
        LOG_INFO("Entity %u finished building '%s'", e, c->def_name);
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
