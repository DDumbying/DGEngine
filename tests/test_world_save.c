/* Unit test for world.c save/load round-tripping — the v4 format
   (WorldShape mask + Tileset, replacing v3's WorldShape + fixed 5-slot
   TileSpriteMap), the v3->v4 migration path (old sprite_map becomes an
   equivalent 5-slot Tileset so existing projects don't lose sprite
   assignments), and backward-compat loading of an old v1 file with
   neither section present.

   Links only world.c + log.c (no SDL2/GL) by stubbing the handful of
   renderer/atlas functions world.c references — this test never calls
   world_render(), but the linker still needs those symbols resolved
   since they're referenced from within world.o. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "world/world.h"
#include "world/world_shape.h"
#include "world/tileset.h"

/* ---- Stubs for renderer/atlas symbols world.c references but this
   test never actually exercises (world_render() is not called here). */
void renderer_bind_texture(unsigned int tex_id) { (void)tex_id; }
void renderer_flush_texture(void) {}
void renderer_draw_iso_tile(int gx, int gy, float r, float g, float b, float a) {
    (void)gx; (void)gy; (void)r; (void)g; (void)b; (void)a;
}
void renderer_draw_iso_tile_uv(int gx, int gy, float r, float g, float b, float a, UVRect uv) {
    (void)gx; (void)gy; (void)r; (void)g; (void)b; (void)a; (void)uv;
}
UVRect atlas_get_uv(const SpriteAtlas *a, SpriteId id) {
    (void)a; (void)id;
    UVRect uv = {0.0f, 0.0f, 1.0f, 1.0f};
    return uv;
}

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); failures++; } \
    else printf("ok: %s\n", msg); \
} while (0)

static const char *PATH = "/tmp/test_world_save.dge";

static void test_roundtrip_full_v4(void) {
    World w;
    CHECK(world_create(&w, 5, 4), "create 5x4 world");

    int stone = tileset_add_slot(&w.tileset, "Stone");
    int water = tileset_add_slot(&w.tileset, "Water");
    tileset_set_walkable(&w.tileset, water, false);
    tileset_set_sprite(&w.tileset, stone, 7);
    tileset_set_sprite(&w.tileset, water, 12);

    world_set_tile(&w, 1, 1, stone);
    world_set_tile(&w, 2, 2, water);

    CHECK(world_shape_activate(&w.shape, w.width, w.height), "activate shape mask");
    world_shape_set(&w.shape, 0, 0, false);     /* a hole */
    world_shape_set(&w.shape, 4, 3, false);     /* another hole, far corner */
    world_shape_fill_rect(&w.shape, 1, 0, 2, 1, true);

    CHECK(world_save(&w, PATH), "save v4 world");

    World loaded;
    memset(&loaded, 0, sizeof loaded);
    CHECK(world_load(&loaded, PATH), "load v4 world");

    CHECK(loaded.width == 5 && loaded.height == 4, "dimensions round-trip");
    CHECK(world_get_tile(&loaded, 1, 1)->type == stone, "stone tile round-trips");
    CHECK(world_get_tile(&loaded, 2, 2)->type == water, "water tile round-trips");

    CHECK(loaded.tileset.count == 2, "tileset slot count round-trips");
    CHECK(strcmp(tileset_name_for(&loaded.tileset, stone), "Stone") == 0,
          "tileset slot name round-trips");
    CHECK(tileset_sprite_for(&loaded.tileset, stone) == 7, "tileset stone sprite round-trips");
    CHECK(tileset_sprite_for(&loaded.tileset, water) == 12, "tileset water sprite round-trips");
    CHECK(tileset_is_walkable(&loaded.tileset, stone) == true, "tileset stone walkable round-trips");
    CHECK(tileset_is_walkable(&loaded.tileset, water) == false, "tileset water walkable round-trips");

    CHECK(loaded.shape.active, "shape mask is active after load");
    CHECK(!world_shape_enabled(&loaded.shape, 0, 0), "hole at (0,0) survives round-trip");
    CHECK(!world_shape_enabled(&loaded.shape, 4, 3), "hole at (4,3) survives round-trip");
    CHECK(world_shape_enabled(&loaded.shape, 1, 0), "re-enabled tile (1,0) survives round-trip");
    CHECK(world_tile_playable(&loaded, 1, 0), "world_tile_playable agrees for enabled tile");
    CHECK(!world_tile_playable(&loaded, 0, 0), "world_tile_playable agrees for hole");

    world_destroy(&loaded);
    world_destroy(&w);
}

static void test_roundtrip_no_shape_empty_tileset(void) {
    /* Shape inactive (never touched), Tileset still empty (freshly
       created, nothing defined yet) -- v4 file should still write a
       shape_active=0 byte and a tileset.count=0, with no mask bytes
       or slot records in between. Distinct code path from
       test_roundtrip_full_v4, and the actual "brand new project"
       state (see tileset.h's doc comment on why a fresh Tileset
       starts empty rather than with a default palette). */
    World w;
    CHECK(world_create(&w, 3, 3), "create 3x3 world (no shape, no tileset)");

    CHECK(world_save(&w, PATH), "save world with inactive shape, empty tileset");

    World loaded;
    memset(&loaded, 0, sizeof loaded);
    CHECK(world_load(&loaded, PATH), "load world with inactive shape, empty tileset");

    CHECK(!loaded.shape.active, "shape stays inactive when never activated");
    CHECK(world_tile_playable(&loaded, 1, 1), "every tile playable when shape inactive");
    CHECK(loaded.tileset.count == 0, "tileset stays empty when never defined");

    world_destroy(&loaded);
    world_destroy(&w);
}

/* Hand-write a v3-format file (WorldShape section present, old fixed
   5-slot sprite_map tail instead of a Tileset) to confirm world_load()
   migrates it into an equivalent 5-slot Tileset — Grass/Dirt/Sand/
   Water/Stone in that order, walkable=true except Water — so a
   project saved before this change keeps its sprite assignments and
   its tiles keep meaning what they used to mean. Tile.type bytes from
   a v3 file (the old TerrainType enum values 0-4) map directly onto
   the migrated Tileset's slot indices unchanged, since the migration
   creates the 5 slots in that same order. */
static void test_migrate_v3_sprite_map(void) {
    FILE *f = fopen(PATH, "wb");
    CHECK(f != NULL, "open v3-format file for writing");

    unsigned int version = 3u, width = 2u, height = 1u;
    fwrite("DGEW", 1, 4, f);
    fwrite(&version, sizeof version, 1, f);
    fwrite(&width,   sizeof width,   1, f);
    fwrite(&height,  sizeof height,  1, f);
    /* Two tiles: old TERRAIN_DIRT (=1), old TERRAIN_WATER (=3).
       v1-v3 files store type as a single BYTE per tile (only v4+
       widened it to int32_t — see world.c's world_load() version
       branch), so this has to match that exactly or every field after
       it reads shifted. */
    unsigned char types[2] = { 1, 3 };
    for (int i = 0; i < 2; i++) {
        unsigned char type = types[i];
        unsigned char variant = 0;
        float height_f = 0.0f;
        fwrite(&type, 1, 1, f);
        fwrite(&variant, 1, 1, f);
        fwrite(&height_f, sizeof height_f, 1, f);
    }
    unsigned char shape_active = 0;
    fwrite(&shape_active, 1, 1, f);
    /* Old 5-slot sprite_map: GRASS=0, DIRT=1, SAND=2, WATER=3, STONE=4 */
    int32_t old_sprite_map[5] = { 5, 6, -1, 9, -1 };
    fwrite(old_sprite_map, sizeof old_sprite_map, 1, f);
    fclose(f);

    World loaded;
    memset(&loaded, 0, sizeof loaded);
    CHECK(world_load(&loaded, PATH), "load v3 file (triggers migration)");

    CHECK(loaded.tileset.count == 5, "v3 migration creates exactly 5 slots");
    CHECK(strcmp(tileset_name_for(&loaded.tileset, 0), "Grass") == 0, "migrated slot 0 = Grass");
    CHECK(strcmp(tileset_name_for(&loaded.tileset, 1), "Dirt")  == 0, "migrated slot 1 = Dirt");
    CHECK(strcmp(tileset_name_for(&loaded.tileset, 3), "Water") == 0, "migrated slot 3 = Water");
    CHECK(tileset_sprite_for(&loaded.tileset, 0) == 5, "migrated Grass sprite round-trips");
    CHECK(tileset_sprite_for(&loaded.tileset, 1) == 6, "migrated Dirt sprite round-trips");
    CHECK(tileset_sprite_for(&loaded.tileset, 3) == 9, "migrated Water sprite round-trips");
    CHECK(tileset_is_walkable(&loaded.tileset, 0) == true,  "migrated Grass is walkable");
    CHECK(tileset_is_walkable(&loaded.tileset, 3) == false, "migrated Water is NOT walkable");

    /* Old raw type bytes (1=DIRT, 3=WATER) map straight onto the
       migrated Tileset's slot indices, unchanged. */
    CHECK(world_get_tile(&loaded, 0, 0)->type == 1, "v3 tile 0 keeps old DIRT slot index");
    CHECK(world_get_tile(&loaded, 1, 0)->type == 3, "v3 tile 1 keeps old WATER slot index");
    CHECK(!world_tile_walkable(&loaded, world_get_tile(&loaded, 1, 0)),
          "v3-migrated water tile is unwalkable via world_tile_walkable()");

    world_destroy(&loaded);
}

/* Hand-write a v1-format file (no shape section, no sprite_map/tileset
   section at all) to confirm world_load() still accepts it and
   defaults sanely -- this is the actual backward-compat contract the
   version bumps promised. v1/v2 files have no terrain-meaning data to
   migrate (unlike v3's sprite_map), so their tiles legitimately end up
   referencing an empty Tileset — rendered as the missing-texture
   checker on load, an honest reflection of "this project never
   defined what its terrain means" rather than silently reinstating
   old hardcoded colors that no longer exist anywhere in the engine. */
static void test_load_legacy_v1(void) {
    FILE *f = fopen(PATH, "wb");
    CHECK(f != NULL, "open legacy v1 file for writing");

    unsigned int version = 1u, width = 2u, height = 2u;
    fwrite("DGEW", 1, 4, f);
    fwrite(&version, sizeof version, 1, f);
    fwrite(&width,   sizeof width,   1, f);
    fwrite(&height,  sizeof height,  1, f);
    for (int i = 0; i < 4; i++) {
        unsigned char type = 1 /* old TERRAIN_DIRT byte value */, variant = 0;
        float height_f = 0.0f;
        fwrite(&type, 1, 1, f);
        fwrite(&variant, 1, 1, f);
        fwrite(&height_f, sizeof height_f, 1, f);
    }
    fclose(f);

    World loaded;
    memset(&loaded, 0, sizeof loaded);
    CHECK(world_load(&loaded, PATH), "load legacy v1 file");
    CHECK(loaded.width == 2 && loaded.height == 2, "legacy v1 dimensions correct");
    /* The raw byte (1) survives as Tile.type, but with no Tileset at
       all it now references an undefined slot -- exactly the "don't
       silently guess a meaning" behaviour this pass is built around. */
    CHECK(world_get_tile(&loaded, 0, 0)->type == 1, "legacy v1 raw type byte preserved");
    CHECK(!loaded.shape.active, "legacy v1 file defaults shape inactive");
    CHECK(loaded.tileset.count == 0, "legacy v1 file has no tileset data -- stays empty");
    CHECK(!world_tile_walkable(&loaded, world_get_tile(&loaded, 0, 0)),
          "legacy v1 tile with no tileset is treated as unwalkable, not silently grass");

    world_destroy(&loaded);
}

int main(void) {
    test_roundtrip_full_v4();
    test_roundtrip_no_shape_empty_tileset();
    test_migrate_v3_sprite_map();
    test_load_legacy_v1();
    remove(PATH);

    if (failures) {
        printf("\n%d check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nAll checks passed.\n");
    return 0;
}
