/*  Tests for the Phase 4 additions to ecs/registry: the generation
    counter / EntityHandle (catches stale handles after slot reuse) and
    registry_save/registry_load (binary entity serialization).

    Deliberately has no SDL2/GL dependency — links only registry.c and
    log.c — so it builds and runs anywhere, no display required.
    `make test` builds and runs this and test_picking.c.

    Registry instances here are heap-allocated, not stack-allocated --
    Registry holds several flat MAX_ENTITIES-sized component arrays
    (registry.h explains why: simplest possible sparse set, trades
    memory for zero indirection) and has grown large enough that three
    of them on the stack at once overflows the default stack size
    under ASan. registry_load() already mallocs its own temporary
    Registry internally for the same reason -- this just applies the
    same rule at the call site instead of assuming a local fits. */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include "ecs/registry.h"

static int approx(float a, float b) { return fabsf(a - b) < 0.0001f; }

int main(void) {
    Registry *reg = malloc(sizeof(Registry));
    assert(reg);
    registry_init(reg);

    Entity e1 = entity_create(reg);
    entity_add_transform(reg, e1, (TransformComponent){3.0f, 4.0f});
    entity_add_renderable(reg, e1, (RenderableComponent){0.1f, 0.2f, 0.3f, 1.0f, 10.0f, 20.0f, -1});
    entity_add_health(reg, e1, (HealthComponent){77, 100});

    Entity e2 = entity_create(reg); /* transform only */
    entity_add_transform(reg, e2, (TransformComponent){-1.5f, 8.25f});

    Entity e3 = entity_create(reg); /* renderable only */
    entity_add_renderable(reg, e3, (RenderableComponent){0.55f, 0.55f, 0.58f, 1.0f, 18.0f, 14.0f, -1});

    /* --- Generation counter / EntityHandle --- */
    EntityHandle h1 = entity_to_handle(reg, e1);
    assert(entity_handle_valid(reg, h1));

    entity_destroy(reg, e1);
    assert(!entity_handle_valid(reg, h1));

    Entity e1b = entity_create(reg);          /* recycles e1's slot */
    assert(e1b == e1);
    assert(!entity_handle_valid(reg, h1));    /* stale handle correctly rejected */
    entity_add_transform(reg, e1b, (TransformComponent){99.0f, 99.0f});
    printf("PASS: generation counter rejects a stale handle after slot reuse\n");

    /* --- Save -> load round trip --- */
    assert(registry_save(reg, "/tmp/dge_test_entities.dge"));

    Registry *loaded = malloc(sizeof(Registry));
    assert(loaded);
    assert(registry_load(loaded, "/tmp/dge_test_entities.dge"));

    int found_e1b = 0, found_e2 = 0, found_e3 = 0;
    for (Entity e = 0; e < (Entity)MAX_ENTITIES; e++) {
        if (!entity_alive(loaded, e)) continue;
        TransformComponent  *t  = entity_get_transform(loaded, e);
        RenderableComponent *rd = entity_get_renderable(loaded, e);
        HealthComponent      *h = entity_get_health(loaded, e);

        if (t && approx(t->x, 99.0f) && approx(t->y, 99.0f) && !rd && !h) found_e1b++;
        if (t && approx(t->x, -1.5f) && approx(t->y, 8.25f) && !rd && !h) found_e2++;
        if (!t && rd && approx(rd->w, 18.0f) && !h) found_e3++;
    }
    assert(found_e1b == 1);
    assert(found_e2 == 1);
    assert(found_e3 == 1);
    printf("PASS: save/load round-trip preserves exact component combinations\n");

    /* --- Corrupt file: should fail cleanly, not crash --- */
    FILE *f = fopen("/tmp/dge_test_garbage.dge", "wb");
    fwrite("XXXX", 1, 4, f);
    fclose(f);
    Registry *garbage_target = malloc(sizeof(Registry));
    assert(garbage_target);
    assert(!registry_load(garbage_target, "/tmp/dge_test_garbage.dge"));
    printf("PASS: registry_load rejects bad magic instead of crashing\n");

    free(reg);
    free(loaded);
    free(garbage_target);

    printf("test_registry: ALL TESTS PASSED\n");
    return 0;
}
