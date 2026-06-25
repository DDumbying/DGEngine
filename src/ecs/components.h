#ifndef DGE_COMPONENTS_H
#define DGE_COMPONENTS_H

#include "../core/object_def.h"
#include "../simulation/simulation.h"

/*  Plain data. No behavior lives on a component — behavior lives in
    systems, which read/write components by entity id. */

/* Position in tile-grid space. Float (not int) because Phase 5 movement
   needs an entity to be lerping between (2,3) and (3,3) mid-step. */
typedef struct {
    float x, y;
} TransformComponent;

/* How to draw the entity: a colored box anchored to the bottom of its
   tile, sized in pixels. sprite_id >= 0 selects a sprite from the
   active SpriteAtlas; -1 means color-box fallback (pre-Phase-E). */
typedef struct {
    float r, g, b, a;
    float w, h;
    int   sprite_id;   /* SpriteId from renderer/atlas.h, or -1 */
} RenderableComponent;

typedef struct {
    int current;
    int max;
} HealthComponent;

/*  Phase 5: what resource this entity yields when harvested and how much
    per harvest stroke.  Two types are enough for now.
    yield_per_hit is deducted from the entity's HealthComponent each time
    a harvest action fires; if health reaches 0 the entity is destroyed
    and the resources credited to the ResourceStore.

    ResourceKind itself now lives in simulation/simulation.h, not here —
    core/object_def.h's objdef_get_build_spec() needed it too (for
    build_cost_kind), and object_def.h can't include this header to get
    it (this header already includes object_def.h, for OBJDEF_NAME_MAX
    on DefinitionComponent below). simulation.h has no includes of its
    own, so it's the one place both sides can reach without a cycle. */
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
    TASK_BUILD     -- move adjacent to a blueprint, then spend labor on it
                      (see ConstructionComponent / system_build_entity).
    Path is embedded (not a pointer) so the component is a plain value
    type that save/load can handle without heap bookkeeping. */
#include "../ai/path.h"

typedef enum {
    TASK_IDLE     = 0,
    TASK_MOVE_TO  = 1,
    TASK_HARVEST  = 2,
    TASK_BUILD    = 3
} TaskKind;

typedef struct {
    TaskKind kind;
    int      target_x, target_y;  /* goal tile                         */
    Path     path;                 /* computed route                    */
    int      path_step;            /* index into path.x/y, 0 = first   */
    float    timer;                /* general purpose cooldown/accumulator */
} TaskComponent;

/*  Construction: a placed "blueprint" that a worker must spend labor on
    before it does anything. build_time_total/build_time_done are
    seconds of worker labor, accumulated continuously every frame a
    worker is adjacent and assigned (TASK_BUILD) — unlike harvesting,
    there's no natural single "hit" to discretize into a timer-gated
    tick, so this just adds dt directly via system_build_entity().
    complete starts false and flips true exactly once, permanently, the
    moment build_time_done reaches build_time_total; the same entity is
    reused throughout (its RenderableComponent gets swapped to the
    finished look on completion, see simulation/construction.c) rather
    than destroying the blueprint and spawning a replacement. */
typedef enum {
    BUILDING_CAMPFIRE = 0,
    BUILDING_COUNT
} BuildingKind;

typedef struct {
    BuildingKind kind;
    float        build_time_total;
    float        build_time_done;
    bool         complete;

    /*  Construction hookup for user-defined buildable objects (see
        objdef_is_buildable() in core/object_def.h): is_custom==true
        means kind is meaningless and def_name names the ObjectDef
        instead. kind stays a plain BuildingKind (not folded into a
        tagged union) because every existing BUILDING_CAMPFIRE call
        site already switches on it directly — adding a third "which
        union member is active" branch everywhere would touch more
        code than this one extra bool+string does. */
    bool         is_custom;
    char         def_name[OBJDEF_NAME_MAX];
} ConstructionComponent;

/*  Phase L->World: links a placed instance back to the user-defined
    ObjectDef it was placed from (see core/object_def.h). Entities
    spawned from the World tab's hardcoded prefabs (tree/rock/worker)
    or the campfire blueprint do NOT have this — it exists only for
    instances of objects the player actually defined in the Objects
    tab, so systems can tell "built-in" and "user-defined" apart
    without a separate flag.

    Just the name, not a pointer to the ObjectDef itself: the registry
    that owns ObjectDefs can reload from disk (Objects tab Save/Reload),
    which would invalidate any pointer into it, and this component
    needs to survive a save/load round-trip on its own (see
    registry.h's binary format) where a raw pointer wouldn't mean
    anything on the next run anyway. Whoever needs the live ObjectDef
    looks it up by name through objdef_find() when it's actually
    needed (e.g. a future Lua on_tick), instead of caching it here. */
typedef struct {
    char def_name[OBJDEF_NAME_MAX];
} DefinitionComponent;

#endif /* DGE_COMPONENTS_H */
