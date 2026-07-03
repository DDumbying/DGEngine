/* Unit tests for world_generator.c — all four topology types.
   Links world.c + world_generator.c + log.c only (no SDL/GL).
   Renderer/atlas stubs are shared with test_world_save.c.

   These generators now ONLY ever touch WorldShape (which tiles exist),
   never Tile.type (what a tile looks like) — an island/rooms layout is
   a shape, not a color scheme, and the engine no longer presumes what
   any Tileset slot "means" the way the old hardcoded TerrainType enum
   let it. So these tests check playability/shape, not terrain type. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "world/world.h"
#include "world/world_generator.h"
#include "world/world_shape.h"
#include "world/tileset.h"

/* ---- renderer/atlas stubs (same as test_world_save.c) ------------- */
void renderer_bind_texture(unsigned int t) { (void)t; }
void renderer_flush_texture(void) {}
void renderer_draw_iso_tile(int gx, int gy, float r, float g, float b, float a) {
    (void)gx;(void)gy;(void)r;(void)g;(void)b;(void)a;
}
void renderer_draw_iso_tile_uv(int gx, int gy, float r, float g, float b, float a, UVRect uv) {
    (void)gx;(void)gy;(void)r;(void)g;(void)b;(void)a;(void)uv;
}
UVRect atlas_get_uv(const SpriteAtlas *a, SpriteId id) {
    (void)a;(void)id;
    UVRect uv={0,0,1,1}; return uv;
}

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); failures++; } \
    else printf("ok: %s\n", msg); \
} while(0)

/* -----------------------------------------------------------------
   Helpers */

static int count_playable(const World *w) {
    int n = 0;
    for (int y = 0; y < w->height; y++)
        for (int x = 0; x < w->width; x++)
            if (world_tile_playable(w, x, y)) n++;
    return n;
}

/* -----------------------------------------------------------------
   RECT — no change to a cleared world */

static void test_rect(void) {
    World w;
    world_create(&w, 16, 16);
    world_clear(&w);
    world_topology_generate(&w, WORLD_TOPO_RECT, 42);

    CHECK(!w.shape.active, "RECT leaves shape inactive");
    CHECK(count_playable(&w) == 16*16, "RECT: every tile playable");
    /* RECT is a no-op on Tile.type by design — world_clear() already
       set every tile to undefined (-1), and RECT has no reason to
       touch that; it's purely "don't cut any holes". */
    bool all_undefined = true;
    for (int i = 0; i < 16*16; i++)
        if (w.tiles[i].type != -1) all_undefined = false;
    CHECK(all_undefined, "RECT: doesn't touch Tile.type");

    world_destroy(&w);
}

/* -----------------------------------------------------------------
   FREEFORM — activates mask, but starts blank (paint outward), not
   all-enabled (carve holes). "Paint your own shape" means starting
   from nothing, same as a blank document starts with zero words. */

static void test_freeform(void) {
    World w;
    world_create(&w, 12, 10);
    world_clear(&w);
    world_topology_generate(&w, WORLD_TOPO_FREEFORM, 0);

    CHECK(w.shape.active, "FREEFORM activates shape mask");
    CHECK(count_playable(&w) == 0, "FREEFORM: starts blank, zero tiles enabled");
    /* Tiles themselves untouched — they just aren't visible/playable
       yet until the Shape Pane paints some of them enabled. */
    bool all_undefined = true;
    for (int i = 0; i < 12*10; i++)
        if (w.tiles[i].type != -1) all_undefined = false;
    CHECK(all_undefined, "FREEFORM: doesn't touch Tile.type");

    world_destroy(&w);
}

/* -----------------------------------------------------------------
   ISLAND — guaranteed disabled border, guaranteed enabled interior.
   No longer paints WATER/SAND/GRASS terrain — that was the exact
   engine-presumes-meaning mistake this whole pass removes. An island
   is now purely a WorldShape boundary; what a project's tiles look
   like inside or outside that boundary is its own Tileset's business,
   entirely untouched by this generator. */

static void test_island(void) {
    World w;
    int W = 40, H = 40;
    world_create(&w, W, H);
    world_clear(&w);
    world_topology_generate(&w, WORLD_TOPO_ISLAND, 12345);

    CHECK(w.shape.active, "ISLAND activates shape mask");

    /* Corners must be disabled (holes) — they're the farthest from
       the centre, so every sane falloff excludes them. */
    CHECK(!world_shape_enabled(&w.shape, 0, 0),      "ISLAND: top-left corner disabled");
    CHECK(!world_shape_enabled(&w.shape, W-1, 0),    "ISLAND: top-right corner disabled");
    CHECK(!world_shape_enabled(&w.shape, 0, H-1),    "ISLAND: bottom-left corner disabled");
    CHECK(!world_shape_enabled(&w.shape, W-1, H-1),  "ISLAND: bottom-right corner disabled");

    /* Centre must be enabled. */
    CHECK(world_shape_enabled(&w.shape, W/2, H/2), "ISLAND: centre tile is enabled");

    /* Must have a genuine boundary — not all-enabled, not all-disabled. */
    int n_enabled = count_playable(&w);
    CHECK(n_enabled > 0,      "ISLAND: has enabled (land) tiles");
    CHECK(n_enabled < W*H,    "ISLAND: not ALL tiles are enabled");

    /* Tile.type is completely untouched by this generator now. */
    bool all_undefined = true;
    for (int i = 0; i < W*H; i++)
        if (w.tiles[i].type != -1) all_undefined = false;
    CHECK(all_undefined, "ISLAND: doesn't touch Tile.type — shape only");

    /* Reproducible with same seed */
    World w2;
    world_create(&w2, W, H);
    world_clear(&w2);
    world_topology_generate(&w2, WORLD_TOPO_ISLAND, 12345);
    bool same = true;
    for (int y = 0; y < H && same; y++)
        for (int x = 0; x < W && same; x++)
            if (world_shape_enabled(&w.shape, x, y) != world_shape_enabled(&w2.shape, x, y))
                same = false;
    CHECK(same, "ISLAND: same seed produces same shape");

    /* Different seeds must differ somewhere (with any reasonable seed pair) */
    World w3;
    world_create(&w3, W, H);
    world_clear(&w3);
    world_topology_generate(&w3, WORLD_TOPO_ISLAND, 99999);
    bool diff = false;
    for (int y = 0; y < H && !diff; y++)
        for (int x = 0; x < W && !diff; x++)
            if (world_shape_enabled(&w.shape, x, y) != world_shape_enabled(&w3.shape, x, y))
                diff = true;
    CHECK(diff, "ISLAND: different seeds produce different shapes");

    world_destroy(&w);
    world_destroy(&w2);
    world_destroy(&w3);
}

/* -----------------------------------------------------------------
   ROOMS — BSP dungeon, most tiles are holes. No longer paints STONE
   floors — same reasoning as ISLAND above, this is shape-only now. */

static void test_rooms(void) {
    World w;
    int W = 48, H = 48;
    world_create(&w, W, H);
    world_clear(&w);
    world_topology_generate(&w, WORLD_TOPO_ROOMS, 7777);

    CHECK(w.shape.active, "ROOMS activates shape mask");

    int playable = count_playable(&w);
    int total    = W * H;

    /* Most tiles should be holes (walls/void). Allow up to 50% to be
       carved floor so the test survives even generous BSP params. */
    CHECK(playable > 0,              "ROOMS: at least some tiles are playable");
    CHECK(playable < total,          "ROOMS: some tiles are holes (not every tile carved)");
    CHECK(playable < total / 2,      "ROOMS: majority of tiles are holes (BSP left walls)");

    /* Tile.type is completely untouched — room floors are the
       project's own Tileset/PAINT mode's business, not the
       generator's. */
    bool all_undefined = true;
    for (int i = 0; i < W*H; i++)
        if (w.tiles[i].type != -1) all_undefined = false;
    CHECK(all_undefined, "ROOMS: doesn't touch Tile.type — shape only");

    /* Corners must be holes (BSP never carves right to the border). */
    CHECK(!world_tile_playable(&w, 0, 0),     "ROOMS: top-left corner is a hole");
    CHECK(!world_tile_playable(&w, W-1, H-1), "ROOMS: bottom-right corner is a hole");

    /* Different seed -> at least some different playable tile. */
    World w2;
    world_create(&w2, W, H);
    world_clear(&w2);
    world_topology_generate(&w2, WORLD_TOPO_ROOMS, 1234567);
    bool diff = false;
    for (int y = 0; y < H && !diff; y++)
        for (int x = 0; x < W && !diff; x++)
            if (world_tile_playable(&w, x, y) != world_tile_playable(&w2, x, y))
                diff = true;
    CHECK(diff, "ROOMS: different seeds produce different layouts");

    world_destroy(&w);
    world_destroy(&w2);
}

/* ----------------------------------------------------------------- */

int main(void) {
    test_rect();
    test_freeform();
    test_island();
    test_rooms();

    if (failures) {
        printf("\n%d check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nAll checks passed.\n");
    return 0;
}
