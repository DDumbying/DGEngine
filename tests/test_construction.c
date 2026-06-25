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

int main(void) {
    /* --- Catalog accessors --- */
    assert(strcmp(building_name(BUILDING_CAMPFIRE), "campfire") == 0);
    assert(building_cost_kind(BUILDING_CAMPFIRE) == RESOURCE_WOOD);
    assert(building_cost_amount(BUILDING_CAMPFIRE) > 0);
    assert(building_build_time(BUILDING_CAMPFIRE) > 0.0f);
    printf("PASS: building catalog accessors return sane values for BUILDING_CAMPFIRE\n");

    /* --- Afford / pay --- */
    ResourceStore rs;
    resource_store_init(&rs);
    int cost = building_cost_amount(BUILDING_CAMPFIRE);

    assert(!building_can_afford(&rs, BUILDING_CAMPFIRE)); /* starts at 0 */
    assert(!building_try_pay_cost(&rs, BUILDING_CAMPFIRE)); /* rejected, no partial spend */
    assert(rs.wood == 0);

    resource_store_add_wood(&rs, cost - 1);
    assert(!building_can_afford(&rs, BUILDING_CAMPFIRE)); /* one short */

    resource_store_add_wood(&rs, 1);
    assert(building_can_afford(&rs, BUILDING_CAMPFIRE)); /* exact amount */
    assert(building_try_pay_cost(&rs, BUILDING_CAMPFIRE));
    assert(rs.wood == 0); /* fully spent */
    printf("PASS: building_can_afford/building_try_pay_cost gate correctly, "
           "no partial spend on rejection\n");

    /* --- Blueprint placement --- */
    Registry *reg = malloc(sizeof(Registry));
    assert(reg);
    registry_init(reg);

    Entity bp = construction_place_blueprint(reg, BUILDING_CAMPFIRE, 3.0f, 4.0f);
    assert(bp != ENTITY_NULL);
    assert(entity_alive(reg, bp));

    TransformComponent *t = entity_get_transform(reg, bp);
    assert(t && approx(t->x, 3.0f) && approx(t->y, 4.0f));

    RenderableComponent *rd = entity_get_renderable(reg, bp);
    assert(rd != NULL); /* blueprint look is present immediately */

    ConstructionComponent *c = entity_get_construction(reg, bp);
    assert(c != NULL);
    assert(c->kind == BUILDING_CAMPFIRE);
    assert(!c->complete);
    assert(approx(c->build_time_done, 0.0f));
    assert(approx(c->build_time_total, building_build_time(BUILDING_CAMPFIRE)));
    printf("PASS: construction_place_blueprint spawns an incomplete blueprint "
           "with the right components\n");

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

    /* The look should have changed too (finished, not blueprint) — at
       minimum, alpha should now be fully opaque (blueprint is 0.45). */
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
