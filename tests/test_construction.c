#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include "simulation/construction.h"

/* Link-time stubs, same technique as test_weather.c — construction.c's
   rendering path doesn't need a real GL context to test the logic that
   matters here (labor accumulation, completion, cost gating). */
void renderer_draw_quad(float x, float y, float w, float h,
                        float r, float g, float b, float a) {
    (void)x; (void)y; (void)w; (void)h; (void)r; (void)g; (void)b; (void)a;
}
void renderer_grid_to_screen(float gx, float gy, float *out_x, float *out_y) {
    *out_x = gx; *out_y = gy; /* identity is fine — no test below checks position */
}

static int approx(float a, float b) { return fabsf(a - b) < 0.0001f; }

/* Phase 2 (ObjectDef consolidation): BuildingKind/campfire retired —
   every buildable thing is now an ObjectDef instance. Hand-construct
   one in memory (no filesystem dependency in a unit test) matching the
   "Campfire" example the old BUILDING_CAMPFIRE hardcoded: costs 20
   wood, takes 8 seconds to build. objdef_is_buildable()'s contract is
   "has a float property named build_time" — see object_def.c. */
static void make_test_campfire_def(ObjectDef *def) {
    memset(def, 0, sizeof(*def));
    snprintf(def->name, sizeof(def->name), "Campfire");
    snprintf(def->sprite, sizeof(def->sprite), "campfire");

    def->props[0].type = PROP_FLOAT;
    snprintf(def->props[0].name, sizeof(def->props[0].name), "build_time");
    def->props[0].value.as_float = 8.0f;

    def->props[1].type = PROP_STRING;
    snprintf(def->props[1].name, sizeof(def->props[1].name), "build_cost_kind");
    snprintf(def->props[1].value.as_string, sizeof(def->props[1].value.as_string), "wood");

    def->props[2].type = PROP_INT;
    snprintf(def->props[2].name, sizeof(def->props[2].name), "build_cost_amount");
    def->props[2].value.as_int = 20;

    def->prop_count = 3;
}

int main(void) {
    ObjectDef campfire;
    make_test_campfire_def(&campfire);

    /* --- Catalog accessors (now ObjectDef-driven, not BuildingKind) --- */
    assert(objdef_is_buildable(&campfire));

    ResourceKind ck; int cost; float build_time;
    objdef_get_build_spec(&campfire, &ck, &cost, &build_time);
    assert(ck == RESOURCE_WOOD);
    assert(cost == 20);
    assert(build_time > 0.0f);
    printf("PASS: objdef_get_build_spec resolves cost/time from an ObjectDef's own properties\n");

    /* A non-buildable ObjectDef (no build_time property) is correctly
       reported as such — this is the actual dispatch editor.c uses to
       decide instant-spawn vs. blueprint placement. */
    ObjectDef tree;
    memset(&tree, 0, sizeof(tree));
    snprintf(tree.name, sizeof(tree.name), "Tree");
    snprintf(tree.sprite, sizeof(tree.sprite), "tree");
    assert(!objdef_is_buildable(&tree));
    printf("PASS: objdef_is_buildable() correctly rejects a def with no build_time property\n");

    /* --- Afford / pay --- */
    ResourceStore rs;
    resource_store_init(&rs);

    assert(!objdef_can_afford_build(&rs, &campfire)); /* starts at 0 */
    assert(!objdef_try_pay_build_cost(&rs, &campfire)); /* rejected, no partial spend */
    assert(rs.wood == 0);

    resource_store_add_wood(&rs, cost - 1);
    assert(!objdef_can_afford_build(&rs, &campfire)); /* one short */

    resource_store_add_wood(&rs, 1);
    assert(objdef_can_afford_build(&rs, &campfire)); /* exact amount */
    assert(objdef_try_pay_build_cost(&rs, &campfire));
    assert(rs.wood == 0); /* fully spent */
    printf("PASS: objdef_can_afford_build/objdef_try_pay_build_cost gate correctly, "
           "no partial spend on rejection\n");

    /* --- Refund --- */
    objdef_refund_build_cost(&rs, &campfire);
    assert(rs.wood == cost);
    printf("PASS: objdef_refund_build_cost restores the exact amount that was paid\n");
    resource_store_try_spend_wood(&rs, cost); /* spend it back down for the next section */

    /* --- Blueprint placement --- */
    Registry *reg = malloc(sizeof(Registry));
    assert(reg);
    registry_init(reg);

    Entity bp = construction_place_blueprint_objdef(reg, &campfire, /*sprite_id=*/-1, 3.0f, 4.0f);
    assert(bp != ENTITY_NULL);
    assert(entity_alive(reg, bp));

    TransformComponent *t = entity_get_transform(reg, bp);
    assert(t && approx(t->x, 3.0f) && approx(t->y, 4.0f));

    RenderableComponent *rd = entity_get_renderable(reg, bp);
    assert(rd != NULL); /* blueprint look is present immediately */
    assert(rd->a < 1.0f); /* dimmed/translucent while incomplete */

    ConstructionComponent *c = entity_get_construction(reg, bp);
    assert(c != NULL);
    assert(strcmp(c->def_name, "Campfire") == 0);
    assert(!c->complete);
    assert(approx(c->build_time_done, 0.0f));
    assert(approx(c->build_time_total, build_time));
    printf("PASS: construction_place_blueprint_objdef spawns an incomplete blueprint "
           "with the right components, referencing the def by name\n");

    /* --- Labor accumulation / completion contract --- */
    float total = c->build_time_total;
    float half = total * 0.5f;

    assert(!system_build_entity(reg, bp, half)); /* not done yet */
    c = entity_get_construction(reg, bp);
    assert(!c->complete);
    assert(approx(c->build_time_done, half));

    /* Cross the finish line: should return true exactly on this call */
    bool just_completed = system_build_entity(reg, bp, half + 1.0f);
    assert(just_completed);
    c = entity_get_construction(reg, bp);
    assert(c->complete);
    assert(approx(c->build_time_done, total)); /* clamped, not overshot */

    /* The look should have changed too — dimmed/translucent blueprint
       tint clears to full opacity on completion. No second sprite_id to
       swap to anymore (see construction.c's doc comment on why — a
       user-defined object only ever has the one sprite it was given). */
    rd = entity_get_renderable(reg, bp);
    assert(rd != NULL && approx(rd->a, 1.0f));

    /* Calling again post-completion: harmless no-op, not a second
       completion signal. */
    assert(!system_build_entity(reg, bp, 5.0f));
    c = entity_get_construction(reg, bp);
    assert(approx(c->build_time_done, total)); /* unchanged, didn't overshoot further */
    printf("PASS: system_build_entity accumulates labor, returns true exactly once "
           "on completion, clamps at total, and no-ops afterward\n");

    /* --- Defensive: nonexistent entity / no ConstructionComponent --- */
    assert(!system_build_entity(reg, ENTITY_NULL, 1.0f));
    assert(!system_build_entity(reg, 9999, 1.0f)); /* never created */

    Entity plain = entity_create(reg);
    entity_add_transform(reg, plain, (TransformComponent){0.0f, 0.0f});
    assert(!system_build_entity(reg, plain, 1.0f)); /* no ConstructionComponent */
    printf("PASS: system_build_entity is a safe no-op on invalid/non-construction entities\n");

    /* --- construction_render doesn't crash on a mix of complete/incomplete --- */
    construction_render(reg, NULL);
    printf("PASS: construction_render runs without crashing on a populated registry\n");

    free(reg);

    printf("test_construction: ALL TESTS PASSED\n");
    return 0;
}
