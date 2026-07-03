#include "world_generator.h"
#include "../core/log.h"

#include <stdlib.h>
#include <math.h>

/* ---------------------------------------------------------------------
   Tiny self-contained hash/noise/RNG -- deliberately not shared with
   world.c's value_noise()/hash2() (those are file-static there). A
   topology generator runs once at project creation, so duplicating a
   few lines of noise math here is a better trade than exporting
   world.c's internals just for this one caller. */

static unsigned int hash2(int x, int y, unsigned int seed) {
    unsigned int h = (unsigned int)x * 374761393u + (unsigned int)y * 668265263u + seed * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static float rand01(int x, int y, unsigned int seed) {
    return (float)(hash2(x, y, seed) & 0xFFFFFu) / (float)0xFFFFFu;
}

/* Cheap value noise: bilinear-interpolate a coarse hash grid. Good
   enough for a coastline silhouette; not trying to be Perlin noise. */
static float value_noise(float x, float y, unsigned int seed) {
    int x0 = (int)floorf(x), y0 = (int)floorf(y);
    int x1 = x0 + 1,         y1 = y0 + 1;
    float fx = x - (float)x0, fy = y - (float)y0;

    float v00 = rand01(x0, y0, seed), v10 = rand01(x1, y0, seed);
    float v01 = rand01(x0, y1, seed), v11 = rand01(x1, y1, seed);

    float ix0 = v00 + (v10 - v00) * fx;
    float ix1 = v01 + (v11 - v01) * fx;
    return ix0 + (ix1 - ix0) * fy;
}

/* xorshift32 -- enough randomness for room placement, doesn't need to
   be cryptographic or even especially high quality. */
static unsigned int xorshift32(unsigned int *state) {
    unsigned int x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x ? x : 1u; /* xorshift32 can't recover from state 0 */
    return x;
}

static int rng_range(unsigned int *state, int lo, int hi_inclusive) {
    if (hi_inclusive <= lo) return lo;
    unsigned int span = (unsigned int)(hi_inclusive - lo + 1);
    return lo + (int)(xorshift32(state) % span);
}

/* ---------------------------------------------------------------------
   WORLD_TOPO_ISLAND

   Distance-from-center falloff combined with coastline noise decides
   which tiles are IN the playable world and which aren't — tiles near
   the edge are forced out regardless of noise (guarantees an actual
   island, not just "mostly land that happens to touch the border"),
   tiles near the center are forced in, and the band in between is
   where the noise gets to decide, which is what makes the coastline
   irregular instead of a perfect circle.

   This used to also assign Tile.type (WATER for the excluded ring,
   SAND for a beach band, GRASS/DIRT/STONE inland by a second noise
   pass) — that was the engine presuming what "water" or "sand" means,
   exactly the mistake this whole generator-cleanup removes. An island
   is a SHAPE, not a color scheme: this function now does the one thing
   that's actually topology (which tiles exist) and leaves what those
   tiles look like entirely to the project's own Tileset and PAINT
   mode, the same way WORLD_TOPO_ROOMS already correctly treated walls
   as true holes instead of a fake "wall-colored" terrain type. */
static void generate_island(World *w, unsigned int seed) {
    if (!world_shape_activate(&w->shape, w->width, w->height)) {
        LOG_ERROR("world_topology_generate: ISLAND couldn't activate shape mask, aborting");
        return;
    }

    float cx = (float)w->width  * 0.5f;
    float cy = (float)w->height * 0.5f;
    float max_r = sqrtf(cx * cx + cy * cy);

    for (int y = 0; y < w->height; y++) {
        for (int x = 0; x < w->width; x++) {
            float dx = (float)x - cx, dy = (float)y - cy;
            float r = sqrtf(dx * dx + dy * dy) / max_r; /* 0 center .. ~1 corner */

            float n = value_noise((float)x * 0.15f, (float)y * 0.15f, seed);
            /* Push the falloff threshold around by the noise so the
               coastline wobbles instead of being a clean circle. */
            float land_threshold = 0.62f + (n - 0.5f) * 0.35f;

            Tile *t = &w->tiles[y * w->width + x];
            t->variant = (unsigned char)(hash2(x, y, seed + 9973u) & 0x3u);
            t->height  = r;

            world_shape_set(&w->shape, x, y, r <= land_threshold);
        }
    }
    LOG_INFO("World topology: ISLAND shape generated (seed=%u) — "
             "paint the playable tiles with your own Tileset", seed);
}

/* ---------------------------------------------------------------------
   WORLD_TOPO_ROOMS

   Classic recursive BSP dungeon: split the grid into two rectangles
   (alternating split axis by aspect ratio, like most BSP dungeon
   generators), recurse until a leaf is small enough, place one room
   per leaf with some margin, then connect each room to its sibling
   with an L-shaped corridor. Anything not inside a room or corridor
   is a true hole in the WorldShape mask -- no render, no pathing, no
   placing, not just a terrain type pretending to be a wall.

   Room/corridor tiles are enabled in the shape mask only — same as
   ISLAND above, this generator no longer assigns Tile.type. What a
   room floor looks like is the project's Tileset, not this
   generator's business. */

#define ROOMS_MIN_LEAF   8   /* stop splitting once a region is this small */
#define ROOMS_MAX_DEPTH  6   /* hard cap regardless of leaf size           */
#define ROOM_MARGIN      1   /* gap kept between a room and its leaf edge  */
#define ROOM_MIN_SIZE    4

typedef struct { int x, y, w, h; } Rect;

static Rect carve_room(World *w, unsigned int *rng, Rect leaf) {
    int max_w = leaf.w - ROOM_MARGIN * 2;
    int max_h = leaf.h - ROOM_MARGIN * 2;
    if (max_w < ROOM_MIN_SIZE) max_w = ROOM_MIN_SIZE;
    if (max_h < ROOM_MIN_SIZE) max_h = ROOM_MIN_SIZE;

    int rw = rng_range(rng, ROOM_MIN_SIZE, max_w);
    int rh = rng_range(rng, ROOM_MIN_SIZE, max_h);
    rw = rw < leaf.w ? rw : leaf.w;
    rh = rh < leaf.h ? rh : leaf.h;

    int rx = leaf.x + rng_range(rng, 0, leaf.w - rw);
    int ry = leaf.y + rng_range(rng, 0, leaf.h - rh);

    for (int y = ry; y < ry + rh && y < w->height; y++) {
        for (int x = rx; x < rx + rw && x < w->width; x++) {
            if (x < 0 || y < 0) continue;
            world_shape_set(&w->shape, x, y, true);
        }
    }
    Rect room = { rx, ry, rw, rh };
    return room;
}

static void carve_corridor(World *w, int x0, int y0, int x1, int y1) {
    /* L-shaped: horizontal first, then vertical -- simple, always
       connects, occasionally crosses a room corner, which is fine
       (a corridor through a room corner still reads as "connected"
       rather than as a bug). */
    int xa = x0 < x1 ? x0 : x1, xb = x0 < x1 ? x1 : x0;
    for (int x = xa; x <= xb; x++) {
        if (x < 0 || x >= w->width || y0 < 0 || y0 >= w->height) continue;
        world_shape_set(&w->shape, x, y0, true);
    }
    int ya = y0 < y1 ? y0 : y1, yb = y0 < y1 ? y1 : y0;
    for (int y = ya; y <= yb; y++) {
        if (y < 0 || y >= w->height || x1 < 0 || x1 >= w->width) continue;
        world_shape_set(&w->shape, x1, y, true);
    }
}

/* Returns the center of the room carved for this leaf (or for one of
   its children, recursively) via out_cx/out_cy, so the caller can
   connect siblings without needing a dynamic list of every room. */
static void bsp_split(World *w, unsigned int *rng, Rect region, int depth,
                       int *out_cx, int *out_cy) {
    bool leaf = depth >= ROOMS_MAX_DEPTH
             || region.w < ROOMS_MIN_LEAF * 2
             || region.h < ROOMS_MIN_LEAF * 2;

    if (leaf) {
        Rect room = carve_room(w, rng, region);
        *out_cx = room.x + room.w / 2;
        *out_cy = room.y + room.h / 2;
        return;
    }

    bool split_vertical = region.w > region.h; /* split the long axis */
    Rect a = region, b = region;
    if (split_vertical) {
        int cut = rng_range(rng, ROOMS_MIN_LEAF, region.w - ROOMS_MIN_LEAF);
        a.w = cut;
        b.x = region.x + cut;
        b.w = region.w - cut;
    } else {
        int cut = rng_range(rng, ROOMS_MIN_LEAF, region.h - ROOMS_MIN_LEAF);
        a.h = cut;
        b.y = region.y + cut;
        b.h = region.h - cut;
    }

    int acx, acy, bcx, bcy;
    bsp_split(w, rng, a, depth + 1, &acx, &acy);
    bsp_split(w, rng, b, depth + 1, &bcx, &bcy);
    carve_corridor(w, acx, acy, bcx, bcy);

    /* This node's "center" for its own parent's corridor is just one
       of its children's centers -- doesn't matter which, the corridor
       just needs to land inside a carved room somewhere in this
       subtree. */
    *out_cx = acx;
    *out_cy = acy;
}

static void generate_rooms(World *w, unsigned int seed) {
    /* Every tile starts disabled (a hole) -- carve_room()/
       carve_corridor() enable exactly the tiles that end up walkable. */
    if (!world_shape_activate(&w->shape, w->width, w->height)) {
        LOG_ERROR("world_topology_generate: ROOMS couldn't activate shape mask, aborting");
        return;
    }
    for (int y = 0; y < w->height; y++)
        for (int x = 0; x < w->width; x++)
            world_shape_set(&w->shape, x, y, false);

    unsigned int rng = seed ? seed : 1u;
    Rect region = { 0, 0, w->width, w->height };
    int cx, cy;
    bsp_split(w, &rng, region, 0, &cx, &cy);
    (void)cx; (void)cy;

    LOG_INFO("World topology: ROOMS shape generated (seed=%u) — "
             "paint the room floors with your own Tileset", seed);
}

/* ---------------------------------------------------------------------
   Public entry point */

void world_topology_generate(World *w, WorldTopology topology, unsigned int seed) {
    switch (topology) {
        case WORLD_TOPO_RECT:
            /* No-op: world_create()+world_clear() already produced a
               plain filled rectangle, which is exactly what RECT
               means. Nothing to generate. */
            break;

        case WORLD_TOPO_FREEFORM:
            /* Start blank: the mask activates but every tile begins
               disabled, so a brand-new FREEFORM world opens as an empty
               canvas the Shape Pane grows outward from — not a filled
               rectangle you carve holes into. "Paint your own shape"
               (the project-creation description) means starting from
               nothing, the same way a blank document starts with zero
               words rather than a page you have to erase first. */
            if (world_shape_activate(&w->shape, w->width, w->height)) {
                for (int y = 0; y < w->height; y++)
                    for (int x = 0; x < w->width; x++)
                        world_shape_set(&w->shape, x, y, false);
            }
            LOG_INFO("World topology: FREEFORM activated (blank canvas, paint outward)");
            break;

        case WORLD_TOPO_ISLAND:
            generate_island(w, seed);
            break;

        case WORLD_TOPO_ROOMS:
            generate_rooms(w, seed);
            break;

        default:
            LOG_WARN("world_topology_generate: unknown topology %d, leaving as rectangle", (int)topology);
            break;
    }
}
