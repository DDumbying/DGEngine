/*  test_spatial_grid.c
    Phase 4 — Spatial Grid unit tests.
    No SDL2/GL required; compiles with just -lm.

    Build + run:
        gcc -std=c11 -Wall -g tests/test_spatial_grid.c src/world/spatial_grid.c \
            src/core/log.c -Isrc -o bin/tests/test_spatial_grid -lm
        ./bin/tests/test_spatial_grid
*/

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include "world/spatial_grid.h"

static int passed = 0;
static int failed = 0;

#define CHECK(cond, msg) do {                                   \
    if (cond) { printf("  [PASS] %s\n", msg); passed++; }      \
    else       { printf("  [FAIL] %s\n", msg); failed++; }     \
} while(0)

static void test_create_destroy(void) {
    printf("create / destroy\n");
    SpatialGrid g;
    CHECK(sgrid_create(&g, 64, 64), "sgrid_create returns true");
    CHECK(g.cells != NULL,          "cells allocated");
    CHECK(g.width == 64,            "width set");
    CHECK(g.height == 64,           "height set");
    /* All cells should be ENTITY_NULL after create */
    bool all_null = true;
    for (int i = 0; i < 64 * 64; i++)
        if (g.cells[i] != ENTITY_NULL) { all_null = false; break; }
    CHECK(all_null, "all cells initialised to ENTITY_NULL");
    sgrid_destroy(&g);
    CHECK(g.cells == NULL, "cells NULL after destroy");
}

static void test_insert_lookup(void) {
    printf("insert / lookup\n");
    SpatialGrid g; sgrid_create(&g, 32, 32);

    sgrid_insert(&g, (Entity)7, 5, 10);
    CHECK(sgrid_at(&g, 5, 10) == 7,           "lookup finds inserted entity");
    CHECK(sgrid_at(&g, 0,  0) == ENTITY_NULL, "empty cell returns ENTITY_NULL");
    CHECK(sgrid_at(&g, 5, 11) == ENTITY_NULL, "adjacent cell still empty");

    /* Out-of-bounds returns ENTITY_NULL */
    CHECK(sgrid_at(&g, -1,  0) == ENTITY_NULL, "negative x out-of-bounds");
    CHECK(sgrid_at(&g,  0, -1) == ENTITY_NULL, "negative y out-of-bounds");
    CHECK(sgrid_at(&g, 32,  0) == ENTITY_NULL, "x == width out-of-bounds");
    CHECK(sgrid_at(&g,  0, 32) == ENTITY_NULL, "y == height out-of-bounds");

    sgrid_destroy(&g);
}

static void test_remove(void) {
    printf("remove\n");
    SpatialGrid g; sgrid_create(&g, 16, 16);

    sgrid_insert(&g, (Entity)3, 4, 4);
    CHECK(sgrid_at(&g, 4, 4) == 3, "entity present before remove");
    sgrid_remove(&g, 4, 4);
    CHECK(sgrid_at(&g, 4, 4) == ENTITY_NULL, "cell empty after remove");

    /* Safe to remove from already-empty cell */
    sgrid_remove(&g, 4, 4);
    CHECK(sgrid_at(&g, 4, 4) == ENTITY_NULL, "double-remove is safe");

    /* Out-of-bounds remove is safe */
    sgrid_remove(&g, -1, 0);
    sgrid_remove(&g, 100, 100);
    CHECK(true, "out-of-bounds removes don't crash");

    sgrid_destroy(&g);
}

static void test_move(void) {
    printf("move\n");
    SpatialGrid g; sgrid_create(&g, 16, 16);

    sgrid_insert(&g, (Entity)9, 2, 3);
    sgrid_move(&g, (Entity)9, 2, 3, 5, 7);
    CHECK(sgrid_at(&g, 2, 3) == ENTITY_NULL, "source cell cleared after move");
    CHECK(sgrid_at(&g, 5, 7) == 9,           "destination cell set after move");

    /* Move to out-of-bounds destination clears source only */
    sgrid_move(&g, (Entity)9, 5, 7, 999, 999);
    CHECK(sgrid_at(&g, 5, 7) == ENTITY_NULL, "source cleared even on OOB dst");

    sgrid_destroy(&g);
}

static void test_rebuild(void) {
    printf("rebuild\n");
    SpatialGrid g; sgrid_create(&g, 8, 8);

    /* Fake registry parallel arrays */
    bool   alive[16];
    bool   has_t[16];
    float  tx[16], ty[16];
    memset(alive, 0, sizeof alive);
    memset(has_t, 0, sizeof has_t);

    alive[0] = true; has_t[0] = true; tx[0] = 1.0f; ty[0] = 2.0f;
    alive[1] = true; has_t[1] = true; tx[1] = 5.0f; ty[1] = 6.0f;
    alive[2] = true; has_t[2] = false; /* no transform */

    sgrid_rebuild(&g, 8, 8, alive, has_t, tx, ty, 16);

    CHECK(sgrid_at(&g, 1, 2) == 0,           "entity 0 at (1,2) after rebuild");
    CHECK(sgrid_at(&g, 5, 6) == 1,           "entity 1 at (5,6) after rebuild");
    CHECK(sgrid_at(&g, 0, 0) == ENTITY_NULL, "entity without transform absent");

    /* Rebuild with new dimensions */
    sgrid_rebuild(&g, 4, 4, alive, has_t, tx, ty, 16);
    CHECK(g.width == 4 && g.height == 4, "dimensions updated by rebuild");
    CHECK(sgrid_at(&g, 1, 2) == 0,       "entity 0 still present after resize-rebuild");

    sgrid_destroy(&g);
}

static void test_overwrite(void) {
    printf("overwrite\n");
    SpatialGrid g; sgrid_create(&g, 16, 16);

    /* Inserting a second entity into the same cell overwrites (last write wins) */
    sgrid_insert(&g, (Entity)10, 3, 3);
    sgrid_insert(&g, (Entity)11, 3, 3);
    CHECK(sgrid_at(&g, 3, 3) == 11, "second insert overwrites first");

    sgrid_destroy(&g);
}

int main(void) {
    printf("=== spatial_grid tests ===\n");
    test_create_destroy();
    test_insert_lookup();
    test_remove();
    test_move();
    test_rebuild();
    test_overwrite();
    printf("=== %d passed, %d failed ===\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
