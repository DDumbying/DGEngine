#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include "simulation/simulation.h"

static const char *PATH = "/tmp/test_simulation.dge";

static void test_simclock(void) {
    SimClock clk;
    simclock_init(&clk);
    assert(clk.elapsed == 0.0);
    assert(clk.speed == 1.0f);
    assert(!simclock_is_paused(&clk));

    float gdt = simclock_tick(&clk, 1.0f);
    assert(gdt == 1.0f);
    assert(clk.elapsed == 1.0);

    clk.speed = 2.0f;
    gdt = simclock_tick(&clk, 1.0f);
    assert(gdt == 2.0f);
    assert(clk.elapsed == 3.0);

    simclock_pause(&clk);
    assert(simclock_is_paused(&clk));
    gdt = simclock_tick(&clk, 1.0f);
    assert(gdt == 0.0f);
    assert(clk.elapsed == 3.0); /* unchanged while paused */

    simclock_resume(&clk);
    assert(!simclock_is_paused(&clk));
    assert(clk.speed == 2.0f); /* restored, not reset to 1x */

    printf("PASS: SimClock tick/pause/resume\n");
}

static void test_resource_store_basic(void) {
    ResourceStore rs;
    resource_store_init(&rs);
    assert(rs.count == 0);

    /* Absent resource reads as 0, not an error */
    assert(resource_store_get(&rs, "wood") == 0);
    assert(!resource_store_has(&rs, "wood", 1));

    /* Adding creates the entry on first use */
    resource_store_add(&rs, "wood", 10);
    assert(rs.count == 1);
    assert(resource_store_get(&rs, "wood") == 10);

    /* A brand new resource name, never declared anywhere ahead of
       time -- Phase 2B's whole point: the store grows to fit whatever
       names show up. */
    resource_store_add(&rs, "gold", 5);
    assert(rs.count == 2);
    assert(resource_store_get(&rs, "gold") == 5);
    assert(resource_store_get(&rs, "wood") == 10); /* unaffected */

    /* Adding again to an existing name accumulates, doesn't duplicate */
    resource_store_add(&rs, "wood", 3);
    assert(rs.count == 2); /* still 2 distinct kinds */
    assert(resource_store_get(&rs, "wood") == 13);

    printf("PASS: ResourceStore grows dynamically, accumulates correctly\n");
}

static void test_resource_store_spend(void) {
    ResourceStore rs;
    resource_store_init(&rs);
    resource_store_add(&rs, "stone", 20);

    assert(resource_store_has(&rs, "stone", 20));
    assert(!resource_store_has(&rs, "stone", 21));

    assert(!resource_store_try_spend(&rs, "stone", 21)); /* rejected */
    assert(resource_store_get(&rs, "stone") == 20); /* unchanged on rejection */

    assert(resource_store_try_spend(&rs, "stone", 20));
    assert(resource_store_get(&rs, "stone") == 0);

    /* Spending a name that was never added is also a clean rejection,
       not a crash or an implicit negative balance. */
    assert(!resource_store_try_spend(&rs, "mana", 1));

    printf("PASS: resource_store_try_spend is atomic (check+commit), never partial\n");
}

static void test_resource_store_negative_add_clamps(void) {
    ResourceStore rs;
    resource_store_init(&rs);
    resource_store_add(&rs, "wood", 5);
    resource_store_add(&rs, "wood", -100); /* would go negative */
    assert(resource_store_get(&rs, "wood") == 0); /* clamped, not negative */
    printf("PASS: resource_store_add clamps at 0, never goes negative\n");
}

static void test_resource_store_full(void) {
    ResourceStore rs;
    resource_store_init(&rs);
    char name[RESOURCE_NAME_MAX];
    for (int i = 0; i < RESOURCE_STORE_MAX_KINDS; i++) {
        snprintf(name, sizeof name, "kind%d", i);
        resource_store_add(&rs, name, 1);
    }
    assert(rs.count == RESOURCE_STORE_MAX_KINDS);

    /* One more distinct name than the store can hold -- dropped with a
       warning, not a buffer overrun. */
    resource_store_add(&rs, "overflow", 1);
    assert(rs.count == RESOURCE_STORE_MAX_KINDS); /* unchanged */
    assert(resource_store_get(&rs, "overflow") == 0);

    printf("PASS: ResourceStore rejects a new name past capacity, doesn't overflow\n");
}

static void test_roundtrip_v2(void) {
    SimClock clk;
    simclock_init(&clk);
    clk.elapsed = 123.5;
    clk.speed = 0.0f;       /* paused */
    clk.saved_speed = 2.0f; /* was running at 2x before pausing */

    ResourceStore rs;
    resource_store_init(&rs);
    resource_store_add(&rs, "wood", 42);
    resource_store_add(&rs, "gold", 7);

    assert(simulation_save(&clk, &rs, PATH));

    SimClock loaded_clk;
    ResourceStore loaded_rs;
    assert(simulation_load(&loaded_clk, &loaded_rs, PATH));

    assert(loaded_clk.elapsed == 123.5);
    assert(loaded_clk.speed == 0.0f);
    assert(loaded_clk.saved_speed == 2.0f);
    assert(simclock_is_paused(&loaded_clk));

    assert(loaded_rs.count == 2);
    assert(resource_store_get(&loaded_rs, "wood") == 42);
    assert(resource_store_get(&loaded_rs, "gold") == 7);

    printf("PASS: simulation_save/load round-trips SimClock + named ResourceStore (v2)\n");
}

/* Hand-write a v1-format file (fixed int32 wood + int32 stone, no
   names at all) to confirm simulation_load() migrates it into the new
   named store as "wood"/"stone" entries — same "old data keeps
   meaning what it used to mean" discipline as every other version
   migration in this codebase (world.c's v3->v4 Tileset migration,
   registry.c's v8->v9 ResourceComponent migration). */
static void test_migrate_v1(void) {
    FILE *f = fopen(PATH, "wb");
    assert(f);

    unsigned int version = 1u;
    double elapsed = 55.0;
    float speed = 1.5f, saved_speed = 1.5f;
    int32_t wood = 30, stone = 12;

    fwrite("DGES", 1, 4, f);
    fwrite(&version, sizeof version, 1, f);
    fwrite(&elapsed, sizeof elapsed, 1, f);
    fwrite(&speed, sizeof speed, 1, f);
    fwrite(&saved_speed, sizeof saved_speed, 1, f);
    fwrite(&wood, sizeof wood, 1, f);
    fwrite(&stone, sizeof stone, 1, f);
    fclose(f);

    SimClock clk;
    ResourceStore rs;
    assert(simulation_load(&clk, &rs, PATH));

    assert(clk.elapsed == 55.0);
    assert(clk.speed == 1.5f);
    assert(rs.count == 2);
    assert(resource_store_get(&rs, "wood") == 30);
    assert(resource_store_get(&rs, "stone") == 12);

    printf("PASS: simulation_load migrates a v1 fixed wood/stone file into the named store\n");
}

int main(void) {
    test_simclock();
    test_resource_store_basic();
    test_resource_store_spend();
    test_resource_store_negative_add_clamps();
    test_resource_store_full();
    test_roundtrip_v2();
    test_migrate_v1();
    remove(PATH);

    printf("test_simulation: ALL TESTS PASSED\n");
    return 0;
}
