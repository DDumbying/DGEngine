#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include "ai/pathfinder.h"
#include "world/world.h"
#include "world/tileset.h"

/*  Implement world_get_tile/world_get_tile_mut/world_tile_walkable here
    to avoid linking the full world.c (which pulls in renderer
    dependencies and requires GL/SDL2). Tileset-as-data means
    "walkable" is no longer a property pathfinder.c can read off a raw
    TerrainType byte — it has to ask the World's own Tileset, so this
    stub has to actually carry a real (if tiny) Tileset now, not just
    fake tile storage. */
const Tile *world_get_tile(const World *w, int x, int y) {
    if (x < 0 || y < 0 || x >= w->width || y >= w->height) return NULL;
    return &w->tiles[y * w->width + x];
}

Tile *world_get_tile_mut(World *w, int x, int y) {
    if (x < 0 || y < 0 || x >= w->width || y >= w->height) return NULL;
    return &w->tiles[y * w->width + x];
}

bool world_tile_walkable(const World *w, const Tile *t) {
    if (!t) return false;
    return tileset_is_walkable(&w->tileset, t->type);
}

int main(void) {
    World w;
    w.width = 10;
    w.height = 10;
    w.tiles = calloc(w.width * w.height, sizeof(Tile));
    assert(w.tiles);

    /* Two slots: 0 = walkable ("Grass"), 1 = not walkable ("Water") —
       stands in for what used to be TERRAIN_GRASS/TERRAIN_WATER. Every
       test below refers to these by slot index instead of enum value. */
    tileset_init(&w.tileset);
    int GRASS = tileset_add_slot(&w.tileset, "Grass");
    int WATER = tileset_add_slot(&w.tileset, "Water");
    tileset_set_walkable(&w.tileset, WATER, false);

    /* Initialize all tiles to the walkable slot */
    for (int i = 0; i < w.width * w.height; i++) {
        w.tiles[i].type = GRASS;
    }

    /* 1. Trivial test: start == goal */
    {
        Path path;
        bool found = pathfinder_find(&w, 0, 0, 0, 0, &path);
        assert(!found);
        assert(path.len == 0);
        printf("PASS: trivial path start == goal rejected\n");
    }

    /* 2. Simple path: (0,0) to (2,2) on empty field */
    {
        Path path;
        bool found = pathfinder_find(&w, 0, 0, 2, 2, &path);
        assert(found);
        /* Manhattan distance of 4 steps */
        assert(path.len == 4);
        /* Verify path steps lead to goal and are adjacent */
        int cx = 0, cy = 0;
        for (int i = 0; i < path.len; i++) {
            int dx = abs(path.x[i] - cx);
            int dy = abs(path.y[i] - cy);
            assert((dx == 1 && dy == 0) || (dx == 0 && dy == 1));
            cx = path.x[i];
            cy = path.y[i];
        }
        assert(cx == 2 && cy == 2);
        printf("PASS: simple pathfound successfully on empty grid\n");
    }

    /* 3. Obstacle test: water barrier at x = 1, except at (1, 3) */
    /*
       S . .
       W . .
       W . .
       . . . (walkable at y=3)
    */
    for (int y = 0; y < 3; y++) {
        w.tiles[y * w.width + 1].type = WATER;
    }
    {
        Path path;
        bool found = pathfinder_find(&w, 0, 0, 2, 0, &path);
        assert(found);
        /* The path must go around the water barrier through (1, 3) */
        bool went_around = false;
        for (int i = 0; i < path.len; i++) {
            if (path.x[i] == 1 && path.y[i] == 3) {
                went_around = true;
            }
        }
        assert(went_around);
        printf("PASS: pathfinder successfully navigates around water obstacle\n");
    }

    /* 4. Unreachable test: goal surrounded by water */
    w.tiles[0 * w.width + 9].type = WATER;
    w.tiles[1 * w.width + 8].type = WATER;
    w.tiles[1 * w.width + 9].type = WATER;
    /* (0, 9) is surrounded by water and boundary */
    {
        Path path;
        bool found = pathfinder_find(&w, 0, 0, 9, 0, &path);
        assert(!found);
        assert(path.len == 0);
        printf("PASS: unreachable goal correctly identified\n");
    }

    /* 5. Undefined-slot test: a tile whose type references a slot
       index that doesn't exist in the Tileset (e.g. -1, the "never
       painted" default) must be treated as unwalkable — same "refuse
       to path into ground that was never actually defined" reasoning
       as tileset_is_walkable()'s own doc comment. */
    {
        Tile undefined_tile;
        undefined_tile.type = -1;
        undefined_tile.variant = 0;
        undefined_tile.height = 0.0f;
        assert(!world_tile_walkable(&w, &undefined_tile));
        printf("PASS: undefined tile slot treated as unwalkable\n");
    }

    free(w.tiles);
    printf("test_pathfinder: ALL TESTS PASSED\n");
    return 0;
}
