#include "pathfinder.h"

#include <string.h>
#include <math.h>

/* -----------------------------------------------------------------------
   A* on a fixed tile grid.

   Data structures — all fixed-size stack arrays (no malloc):
     open[]      — set of nodes yet to be expanded, stored as flat array
     g[]         — cost from start to each tile (flat: index = y*w + x)
     f[]         — g + heuristic (flat)
     parent_x/y[]— which tile we came from (flat, -1 = none)
     closed[]    — bool: tile has been expanded (flat)

   The open set is searched linearly for the minimum-f node each step.
   This is O(n) per step and O(n²) total, acceptable for n ≤ 1024.

   Heuristic: Manhattan distance — admissible (never overestimates) for
   4-directional uniform-cost movement, so A* is guaranteed optimal. */

#define MAX_TILES 4096  /* 64x64 upper bound — more than current world size */

/* Manhattan heuristic */
static int heuristic(int ax, int ay, int bx, int by) {
    int dx = ax - bx; if (dx < 0) dx = -dx;
    int dy = ay - by; if (dy < 0) dy = -dy;
    return dx + dy;
}

/* 4-directional neighbours */
static const int DIR_DX[4] = { 0,  0, -1,  1};
static const int DIR_DY[4] = {-1,  1,  0,  0};

bool pathfinder_find(const World *world,
                     int sx, int sy,
                     int gx, int gy,
                     Path *out)
{
    out->len = 0;

    /* Trivial case: start == goal */
    if (sx == gx && sy == gy) return false;

    int w = world->width;
    int h = world->height;

    /* Bounds check */
    if (sx < 0 || sy < 0 || sx >= w || sy >= h) return false;
    if (gx < 0 || gy < 0 || gx >= w || gy >= h) return false;

    /* Goal must be walkable */
    const Tile *goal_tile = world_get_tile(world, gx, gy);
    if (!goal_tile || !tile_is_walkable(goal_tile->type)) return false;

    int n = w * h;
    if (n > MAX_TILES) return false;   /* safety guard */

    /* ---- Per-node arrays (stack allocated, zero-init) ---- */
    int   g_cost[MAX_TILES];
    int   f_cost[MAX_TILES];
    int   parent_x[MAX_TILES];
    int   parent_y[MAX_TILES];
    bool  closed[MAX_TILES];
    bool  in_open[MAX_TILES];

    for (int i = 0; i < n; i++) {
        g_cost[i]   = 0x7FFFFFFF;   /* infinity */
        f_cost[i]   = 0x7FFFFFFF;
        parent_x[i] = -1;
        parent_y[i] = -1;
        closed[i]   = false;
        in_open[i]  = false;
    }

    int start_idx = sy * w + sx;
    g_cost[start_idx] = 0;
    f_cost[start_idx] = heuristic(sx, sy, gx, gy);
    in_open[start_idx] = true;

    int open_count = 1;   /* number of nodes currently in the open set */

    while (open_count > 0) {
        /* Find the open node with the lowest f_cost (linear scan). */
        int cur_idx = -1;
        int cur_f   = 0x7FFFFFFF;
        for (int i = 0; i < n; i++) {
            if (in_open[i] && f_cost[i] < cur_f) {
                cur_f   = f_cost[i];
                cur_idx = i;
            }
        }
        if (cur_idx < 0) break;   /* open set empty (shouldn't happen) */

        int cx = cur_idx % w;
        int cy = cur_idx / w;

        /* Reached the goal — reconstruct path. */
        if (cx == gx && cy == gy) {
            /* Walk parent chain to count steps */
            int len = 0;
            int rx = gx, ry = gy;
            while (!(rx == sx && ry == sy)) {
                len++;
                if (len > PATH_MAX_LEN) { out->len = 0; return false; }
                int ridx   = ry * w + rx;
                int next_x = parent_x[ridx];
                int next_y = parent_y[ridx];
                rx = next_x;
                ry = next_y;
            }
            /* Fill path array in forward order */
            out->len = len;
            rx = gx; ry = gy;
            for (int i = len - 1; i >= 0; i--) {
                out->x[i] = rx;
                out->y[i] = ry;
                int ridx   = ry * w + rx;
                int next_x = parent_x[ridx];
                int next_y = parent_y[ridx];
                rx = next_x;
                ry = next_y;
            }
            return true;
        }

        in_open[cur_idx] = false;
        open_count--;
        closed[cur_idx] = true;

        /* Expand neighbours */
        for (int d = 0; d < 4; d++) {
            int nx = cx + DIR_DX[d];
            int ny = cy + DIR_DY[d];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;

            int nidx = ny * w + nx;
            if (closed[nidx]) continue;

            const Tile *nt = world_get_tile(world, nx, ny);
            if (!nt || !tile_is_walkable(nt->type)) continue;

            int tentative_g = g_cost[cur_idx] + 1;   /* uniform cost = 1 */
            if (tentative_g < g_cost[nidx]) {
                parent_x[nidx] = cx;
                parent_y[nidx] = cy;
                g_cost[nidx]   = tentative_g;
                f_cost[nidx]   = tentative_g + heuristic(nx, ny, gx, gy);
                if (!in_open[nidx]) {
                    in_open[nidx] = true;
                    open_count++;
                }
            }
        }
    }

    /* Open set exhausted without reaching goal — no path exists. */
    return false;
}
