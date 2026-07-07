#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#ifdef _WIN32
#  include <direct.h>
#  define rmdir _rmdir
#else
#  include <unistd.h>
#endif
#include "core/level.h"

static const char *MANIFEST = "/tmp/test_level_manifest.def";

static void test_init_empty(void) {
    LevelRegistry lr;
    level_registry_init(&lr);
    assert(lr.count == 0);
    assert(lr.active_index == -1);
    assert(level_registry_active(&lr) == NULL);
    printf("PASS: level_registry_init starts empty, active_index -1\n");
}

static void test_add_and_slugging(void) {
    LevelRegistry lr;
    level_registry_init(&lr);

    int i0 = level_registry_add(&lr, "Level 1");
    assert(i0 == 0);
    assert(lr.count == 1);
    assert(lr.active_index == 0); /* first add becomes active automatically */
    assert(strcmp(lr.levels[0].name, "Level 1") == 0);
    assert(strstr(lr.levels[0].world_path, "level_1_world.dge") != NULL);
    assert(strstr(lr.levels[0].entity_path, "level_1_entities.dge") != NULL);

    int i1 = level_registry_add(&lr, "Goblin Cave!");
    assert(i1 == 1);
    assert(lr.active_index == 0); /* adding a second doesn't steal active */
    assert(strstr(lr.levels[1].world_path, "goblin_cave_world.dge") != NULL);

    /* A name that slugs to nothing is refused, not silently accepted
       with a garbage empty-string filename. */
    int bad = level_registry_add(&lr, "!!!");
    assert(bad == -1);
    assert(lr.count == 2); /* unchanged */

    printf("PASS: level_registry_add slugs names correctly, refuses empty slugs\n");
}

static void test_capacity(void) {
    LevelRegistry lr;
    level_registry_init(&lr);
    char name[32];
    for (int i = 0; i < LEVEL_MAX; i++) {
        snprintf(name, sizeof name, "L%d", i);
        int idx = level_registry_add(&lr, name);
        assert(idx == i);
    }
    assert(lr.count == LEVEL_MAX);

    int overflow = level_registry_add(&lr, "one too many");
    assert(overflow == -1);
    assert(lr.count == LEVEL_MAX); /* unchanged */

    printf("PASS: level_registry_add rejects past LEVEL_MAX capacity\n");
}

static void test_get_and_set_active(void) {
    LevelRegistry lr;
    level_registry_init(&lr);
    level_registry_add(&lr, "A");
    level_registry_add(&lr, "B");

    assert(level_registry_get(&lr, 0) != NULL);
    assert(level_registry_get(&lr, 1) != NULL);
    assert(level_registry_get(&lr, 2) == NULL);  /* out of range */
    assert(level_registry_get(&lr, -1) == NULL); /* out of range */

    assert(level_registry_set_active(&lr, 1));
    assert(level_registry_active(&lr) == &lr.levels[1]);
    assert(strcmp(level_registry_active(&lr)->name, "B") == 0);

    assert(!level_registry_set_active(&lr, 99)); /* rejected, unchanged */
    assert(level_registry_active(&lr) == &lr.levels[1]); /* still B */

    printf("PASS: level_registry_get/set_active are bounds-checked correctly\n");
}

static void test_bootstrap(void) {
    LevelRegistry lr;
    level_registry_init(&lr);
    assert(lr.count == 0);

    level_registry_bootstrap(&lr);
    assert(lr.count == 1);
    assert(strcmp(lr.levels[0].name, "Level 1") == 0);
    assert(lr.active_index == 0);

    /* Bootstrapping an already-populated registry is a no-op -- doesn't
       add a redundant second "Level 1". */
    level_registry_add(&lr, "Level 2");
    assert(lr.count == 2);
    level_registry_bootstrap(&lr);
    assert(lr.count == 2); /* unchanged */

    printf("PASS: level_registry_bootstrap creates exactly one default level, "
           "is a no-op once any level exists\n");
}

static void test_save_load_roundtrip(void) {
    LevelRegistry lr;
    level_registry_init(&lr);
    level_registry_add(&lr, "Level 1");
    level_registry_add(&lr, "Goblin Cave");
    level_registry_set_active(&lr, 1);
    lr.levels[1].spawn_x = 5.5f;
    lr.levels[1].spawn_y = 8.0f;
    snprintf(lr.levels[1].entry_marker, sizeof(lr.levels[1].entry_marker), "north_gate");

    assert(level_registry_save(&lr, MANIFEST));

    LevelRegistry loaded;
    assert(level_registry_load(&loaded, MANIFEST));

    assert(loaded.count == 2);
    assert(loaded.active_index == 1);
    assert(strcmp(loaded.levels[0].name, "Level 1") == 0);
    assert(strcmp(loaded.levels[1].name, "Goblin Cave") == 0);
    assert(loaded.levels[1].spawn_x == 5.5f);
    assert(loaded.levels[1].spawn_y == 8.0f);
    assert(strcmp(loaded.levels[1].entry_marker, "north_gate") == 0);
    assert(strcmp(loaded.levels[0].world_path, lr.levels[0].world_path) == 0);

    printf("PASS: level_registry_save/load round-trips every field correctly\n");
}

static void test_load_missing_file(void) {
    LevelRegistry lr;
    bool ok = level_registry_load(&lr, "/tmp/does_not_exist_level_manifest.def");
    assert(!ok);
    printf("PASS: level_registry_load fails cleanly on a missing manifest\n");
}

static void test_load_malformed_active_clamped(void) {
    /* Hand-write a manifest with an out-of-range "active=" value --
       load should clamp it back to a valid index (0) rather than
       leaving active_index pointing at nothing. */
    FILE *f = fopen(MANIFEST, "w");
    assert(f);
    fprintf(f, "count=1\n");
    fprintf(f, "active=99\n");
    fprintf(f, "level0_name=Solo\n");
    fprintf(f, "level0_world=levels/solo_world.dge\n");
    fprintf(f, "level0_entities=levels/solo_entities.dge\n");
    fprintf(f, "level0_spawn_x=0\n");
    fprintf(f, "level0_spawn_y=0\n");
    fprintf(f, "level0_marker=\n");
    fclose(f);

    LevelRegistry lr;
    assert(level_registry_load(&lr, MANIFEST));
    assert(lr.count == 1);
    assert(lr.active_index == 0); /* clamped from the bogus 99 */

    printf("PASS: level_registry_load clamps an out-of-range active index\n");
}

static void test_migrate_legacy(void) {
    const char *legacy_world  = "/tmp/test_legacy_world.dge";
    const char *legacy_entity = "/tmp/test_legacy_entities.dge";

    FILE *fw = fopen(legacy_world, "w");  assert(fw); fprintf(fw, "world-data");  fclose(fw);
    FILE *fe = fopen(legacy_entity, "w"); assert(fe); fprintf(fe, "entity-data"); fclose(fe);

    LevelRegistry lr;
    level_registry_init(&lr);
    level_registry_bootstrap(&lr);
    const Level *lv = level_registry_active(&lr);

    bool migrated = level_registry_migrate_legacy(legacy_world, legacy_entity, lv);
    assert(migrated);

    /* Old files gone (renamed, not copied) */
    FILE *check = fopen(legacy_world, "r");
    assert(check == NULL);

    /* New files exist with the migrated content */
    FILE *nf = fopen(lv->world_path, "r");
    assert(nf != NULL);
    char buf[32] = {0};
    fread(buf, 1, sizeof(buf) - 1, nf);
    fclose(nf);
    assert(strcmp(buf, "world-data") == 0);

    remove(lv->world_path);
    remove(lv->entity_path);

    /* Calling again with nothing left to migrate is a clean no-op */
    bool migrated_again = level_registry_migrate_legacy(legacy_world, legacy_entity, lv);
    assert(!migrated_again);

    printf("PASS: level_registry_migrate_legacy moves old files into the new "
           "Level's paths exactly once, no-ops afterward\n");
}

int main(void) {
    test_init_empty();
    test_add_and_slugging();
    test_capacity();
    test_get_and_set_active();
    test_bootstrap();
    test_save_load_roundtrip();
    test_load_missing_file();
    test_load_malformed_active_clamped();
    test_migrate_legacy();
    remove(MANIFEST);

    /* level_registry_save()/level_registry_migrate_legacy() both
       mkdir("levels") relative to the CWD -- legitimate production
       behavior (main.c needs that directory to exist before saving
       level data into it), but this test running from wherever `make
       test` invokes it (the repo root) means the tests above leave a
       real "levels" directory behind unless cleaned up here. Every
       individual file this test creates inside it is already removed
       by the tests above (test_migrate_legacy removes the two files
       it creates there), so this should always be empty -- rmdir()
       fails harmlessly (silently) if it isn't, rather than force-
       deleting something unexpected. */
    rmdir("levels");

    printf("test_level: ALL TESTS PASSED\n");
    return 0;
}
