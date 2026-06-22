#ifndef DGE_COMPONENTS_H
#define DGE_COMPONENTS_H

/*  Plain data. No behavior lives on a component — behavior lives in
    systems, which read/write components by entity id. */

/* Position in tile-grid space. Float (not int) because Phase 5 movement
   needs an entity to be lerping between (2,3) and (3,3) mid-step. */
typedef struct {
    float x, y;
} TransformComponent;

/* How to draw the entity: a colored box anchored to the bottom of its
   tile, sized in pixels. Sprites/textures replace the color in Phase 4
   once the interaction/resource systems give entities actual art. */
typedef struct {
    float r, g, b, a;
    float w, h;
} RenderableComponent;

typedef struct {
    int current;
    int max;
} HealthComponent;

/*  Phase 5: what resource this entity yields when harvested and how much
    per harvest stroke.  Two types are enough for now.
    yield_per_hit is deducted from the entity's HealthComponent each time
    a harvest action fires; if health reaches 0 the entity is destroyed
    and the resources credited to the ResourceStore. */
typedef enum {
    RESOURCE_WOOD  = 0,
    RESOURCE_STONE = 1
} ResourceKind;

typedef struct {
    ResourceKind kind;
    int          yield_per_hit; /* resources credited per successful chop/mine */
} ResourceComponent;

/*  Phase 6: movement state for entities that can walk tile-to-tile.
    TransformComponent.x/y always reflects the interpolated (lerped)
    world position so the renderer shows smooth movement automatically.
    src/dst are the integer grid coords of the current hop; progress
    runs 0->1 over one hop.  When progress reaches 1.0 the system
    snaps x/y to dst and starts the next path step. */
typedef struct {
    float speed;    /* tiles per real second                              */
    float progress; /* 0..1 lerp between src and dst this hop            */
    int   src_x, src_y;
    int   dst_x, dst_y;
    bool  moving;   /* false while idle between path steps               */
} MoveComponent;

/*  Phase 6: the current task an entity is executing.
    TASK_IDLE      -- nothing to do.
    TASK_MOVE_TO   -- follow path[] to (target_x, target_y).
    TASK_HARVEST   -- move adjacent to target resource, then harvest it.
    Path is embedded (not a pointer) so the component is a plain value
    type that save/load can handle without heap bookkeeping. */
#include "../ai/path.h"

typedef enum {
    TASK_IDLE     = 0,
    TASK_MOVE_TO  = 1,
    TASK_HARVEST  = 2
} TaskKind;

typedef struct {
    TaskKind kind;
    int      target_x, target_y;  /* goal tile                         */
    Path     path;                 /* computed route                    */
    int      path_step;            /* index into path.x/y, 0 = first   */
    float    timer;                /* general purpose cooldown/accumulator */
} TaskComponent;

#endif /* DGE_COMPONENTS_H */
