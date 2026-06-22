#ifndef DGE_PATH_H
#define DGE_PATH_H

/*  A path is an ordered sequence of tile-grid coordinates that an entity
    walks along, produced by the pathfinder and consumed by the movement
    system.

    PATH_MAX_LEN caps memory usage.  On a 32x32 world the longest possible
    path is 1024 steps; 64 is enough for any realistic route in current
    map sizes and keeps TaskComponent (which embeds one Path) at a
    manageable size.  Revisit when map sizes grow. */

#define PATH_MAX_LEN 64

typedef struct {
    int x[PATH_MAX_LEN];
    int y[PATH_MAX_LEN];
    int len;   /* number of valid steps; 0 = empty / unreachable */
} Path;

#endif /* DGE_PATH_H */
