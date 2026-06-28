#ifndef DGE_PATHFINDER_H
#define DGE_PATHFINDER_H

/*  Phase 6 — A* pathfinder on the tile grid.

    Design choices that match the project philosophy:
    - 4-directional movement (N/S/E/W): simpler, correct for grid strategy.
    - Open set is a flat array with linear-scan minimum — O(n²) overall but
      perfectly fast on 32x32 maps (≤1024 nodes).  A binary heap would be
      O(n log n) but adds ~80 lines of non-obvious bookkeeping.  Revisit
      only if map sizes grow large enough for this to actually matter.
    - All allocations are on the stack / fixed arrays — no malloc needed.
    - Water tiles are always unwalkable; all other terrain is walkable
      (movement costs are uniform for now; weighted costs come with
      Phase 7 advanced terrain features). */

#include <stdbool.h>
#include "path.h"
#include "../world/world.h"

/*  Find a path from (sx, sy) to (gx, gy) on `world`.
    Fills `out` with the ordered sequence of tile coords to walk, NOT
    including the start tile (the entity is already there) but INCLUDING
    the goal tile.
    Returns true if a path was found, false if unreachable or start==goal.
    On false `out->len` is set to 0. */
bool pathfinder_find(const World *world,
                     int sx, int sy,
                     int gx, int gy,
                     Path *out);

#endif /* DGE_PATHFINDER_H */
