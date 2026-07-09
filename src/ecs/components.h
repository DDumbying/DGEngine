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
   active SpriteAtlas; -1 means color-box fallback (pre-Phase-E).

   Animation (optional, atlas-only): when frame_count > 1, sprite_id is
   the FIRST frame and system_animate_entities() cycles through
   frame_count consecutive atlas cells (sprite_id, sprite_id+1, ...)
   at frame_fps frames/sec, writing the active one into frame_index.
   system_render_entities() then draws sprite_id + frame_index instead
   of sprite_id directly. Leave frame_count at its zero-init default
   (or explicitly 1) for a static sprite -- no behavior change for any
   existing spawn site that doesn't set these fields. Imported asset
   textures (AssetLibrary, not the shared atlas) are never animated;
   they're whole standalone images with no neighboring frames to step
   through. */
typedef struct {
    float r, g, b, a;
    float w, h;
    int   sprite_id;   /* SpriteId from renderer/atlas.h, or -1 */
    int   frame_count;  /* 0 or 1 = static; >1 = this many atlas cells in the strip */
    float frame_fps;    /* playback speed; ignored when frame_count <= 1 */
    float frame_timer;  /* seconds accumulated toward the next frame   */
    int   frame_index;  /* 0..frame_count-1, added to sprite_id when drawing */
} RenderableComponent;

typedef struct {
    int current;
    int max;
} HealthComponent;

/*  Phase 5: what resource this entity yields when harvested and how much
    per harvest stroke.  yield_per_hit is deducted from the entity's
    HealthComponent each time a harvest action fires; if health reaches
    0 the entity is destroyed and the resources credited to the
    ResourceStore.

    Phase 2B (ObjectDef consolidation, resources): kind is now a plain
    resource name string, not a ResourceKind enum — that enum (fixed to
    exactly wood/stone) is retired. See simulation/simulation.h's own
    doc comment for why ResourceStore is a named list rather than an
    index-keyed one: there's no compact per-tile storage pressure here
    the way there is for Tile.type, so a plain name (same as
    ConstructionComponent.def_name/DefinitionComponent.def_name just
    below) is the simpler, more consistent choice than adding a parallel
    indexed-registry system that a handful of named amounts never
    needed.

    RESOURCE_NAME_MAX itself lives in simulation.h (not duplicated
    here) — this header already includes simulation.h for
    ResourceStore-adjacent reasoning elsewhere, so there's no new
    include needed for this change. */
typedef struct {
    char kind[RESOURCE_NAME_MAX];
    int  yield_per_hit; /* resources credited per successful chop/mine */
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
    than destroying the blueprint and spawning a replacement.

    Phase 2 (ObjectDef consolidation): BuildingKind — the hardcoded
    single-entry campfire enum — is retired. Every blueprint is now an
    ObjectDef instance; def_name always names it (see
    core/object_def.h's objdef_is_buildable()/objdef_get_build_spec()).
    There is no more is_custom flag because there is no longer a
    non-custom path to distinguish it from — this also fixes a latent
    bug the old two-path version had: deleting an in-progress blueprint
    in editor.c read c->kind unconditionally for the refund amount,
    which was correct for BuildingKind blueprints but silently wrong
    for ObjectDef ones (whose kind field was never set, so it read as
    BUILDING_CAMPFIRE's cost regardless of what the real object cost).
    A single always-def_name-driven path can't have that bug — the
    refund logic always resolves the real cost via
    objdef_get_build_spec(). */
typedef struct {
    float        build_time_total;
    float        build_time_done;
    bool         complete;
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

/*  Phase 6 Part B: Level Transition. When an entity steps on a tile
    containing an entity with this component, the engine loads the
    target level. */
#define LEVEL_NAME_MAX 64
typedef struct {
    char target_level[LEVEL_NAME_MAX];
    char target_marker[LEVEL_NAME_MAX];
} LevelTransitionComponent;

#endif /* DGE_COMPONENTS_H */
