#ifndef DGE_WORLD_GENERATOR_H
#define DGE_WORLD_GENERATOR_H

#include "world.h"
#include "../core/project.h"

/*  The project-creation topology selector already existed in the UI
    (project_manager.c) and is persisted on Project (core/project.h).
    This is called once, from main.c's ENTER_EDITOR, only when a
    brand-new project's world.dge doesn't exist yet (an existing
    project's saved tiles/shape always win — this never re-generates
    over real data).

    Every topology below only ever touches WorldShape — which tiles
    exist — never Tile.type. What a tile looks like is the project's
    own Tileset (see world/tileset.h), not something a topology
    generator gets to decide; an "island" is a shape, not a color
    scheme, and that distinction is the whole point of this function's
    scope being this narrow.

    WORLD_TOPO_RECT     - no-op. world is already a plain rectangle
                          straight out of world_create()/world_clear().
    WORLD_TOPO_FREEFORM - activates an all-DISABLED WorldShape mask so
                          a fresh FREEFORM project starts as a blank
                          canvas to paint outward from, not a filled
                          rectangle to carve holes into.
    WORLD_TOPO_ISLAND   - noisy coastline falloff from the center;
                          tiles outside it are disabled (not part of
                          the playable world), tiles inside are left
                          for the project's own Tileset/PAINT mode.
    WORLD_TOPO_ROOMS    - classic BSP dungeon: rooms connected by
                          corridors are enabled, everything else is a
                          true hole in the WorldShape mask (no render,
                          no pathfinding, no placing) — room floors are
                          left for the project's own Tileset/PAINT mode.

    Assumes w has already been world_create()'d (and ideally
    world_clear()'d) at the desired width/height — this only ever
    touches the shape mask, it never resizes anything and never writes
    Tile.type. */
void world_topology_generate(World *w, WorldTopology topology, unsigned int seed);

#endif /* DGE_WORLD_GENERATOR_H */
