
#ifndef DGE_SIMULATION_H
#define DGE_SIMULATION_H

#include <stdbool.h>

/*  Phase 5 — Simulation
    Two things live here:

    1. SimClock  — an in-world time counter, completely separate from the
       wall-clock delta-time used for camera movement and rendering.
       Game time passes at a configurable rate (default 1 game-second per
       real-second at speed 1).  Pausing stops game-time without stopping
       rendering, which is the correct separation for a simulation game.

    2. ResourceStore — the player's accumulated resources.

       Phase 2 (ObjectDef consolidation), Part B: ResourceKind — the
       hardcoded wood/stone 2-value enum — is retired. A resource kind
       is now just a name (a plain string, same "engine knows shapes of
       data, never meanings" reasoning as Tileset/ObjectDef): a project
       can have wood/stone, or gold/mana, or nothing at all, or twenty
       different named materials. ResourceStore doesn't need a
       project-wide "here are the resource kinds that exist" registry
       the way Tileset needs one for World, because there's no compact
       per-tile storage pressure here (a handful of named amounts, not
       one index per grid cell) — the store just grows to fit whatever
       names get used, the same "starts empty, grows from use" pattern
       already established for a fresh project's Tileset/ObjectDefs. */

/* ---- SimClock ---- */

typedef struct {
    double  elapsed;      /* total game-seconds elapsed since init          */
    float   speed;        /* multiplier: 0 = paused, 1 = realtime, 2 = 2x  */
    float   saved_speed;  /* last non-zero speed, restored on resume       */
} SimClock;

void simclock_init(SimClock *clk);

/* Advance the clock by dt real seconds.  Call once per frame before any
   system that depends on game-time.  Returns the game-time delta for
   this frame (0 when paused). */
float simclock_tick(SimClock *clk, float dt);

/* Pause / resume helpers (toggle speed between 0 and the last non-zero
   value so unpausing restores the previous speed rather than forcing 1x). */
void simclock_pause(SimClock *clk);
void simclock_resume(SimClock *clk);
bool simclock_is_paused(const SimClock *clk);

/* ---- ResourceStore ---- */

#define RESOURCE_NAME_MAX        32
#define RESOURCE_STORE_MAX_KINDS 32

typedef struct {
    char name[RESOURCE_NAME_MAX];
    int  amount;
} ResourceEntry;

typedef struct {
    ResourceEntry entries[RESOURCE_STORE_MAX_KINDS];
    int           count;
} ResourceStore;

void resource_store_init(ResourceStore *rs);

/*  Add amount (result clamped to >= 0) to the named resource. Creates
    a new entry (starting from 0) the first time a given name is seen
    — a project doesn't declare its resource kinds ahead of time
    anywhere, harvest/scripts/construction just start using whatever
    names they want and the store grows to match, up to
    RESOURCE_STORE_MAX_KINDS distinct names. Logs the change. */
void resource_store_add(ResourceStore *rs, const char *kind_name, int amount);

/* Current amount of the named resource. 0 if that name has never been
   added (not an error — same "absent means zero" contract a fresh
   sandbox economy should have). */
int resource_store_get(const ResourceStore *rs, const char *kind_name);

/* Read-only affordability check. */
bool resource_store_has(const ResourceStore *rs, const char *kind_name, int amount);

/*  Atomic check-and-subtract: returns false (no change, no log) if the
    store doesn't have at least `amount`; otherwise subtracts and logs.
    Deliberately different from resource_store_add's unconditional
    clamp-at-0 behavior — passing a negative amount there silently
    clamps instead of rejecting, which is wrong for "can I afford this"
    callers (construction.c) that need a real yes/no before committing
    to spending. */
bool resource_store_try_spend(ResourceStore *rs, const char *kind_name, int amount);

/* Log current totals — called on any change and available on demand. */
void resource_store_log(const ResourceStore *rs);

/*  Binary save/load for SimClock + ResourceStore together. They're
    declared in the same header and conceptually one "session state"
    blob (how much time has passed, what's in the stockpile), so one
    small file rather than two — the same reasoning registry.c uses
    for bundling several component types into one entities.dge rather
    than one file per component.

    Own file (sim.dge), independent of world.dge/entities.dge, same
    "each subsystem owns its serialization" pattern as the rest of the
    engine. Format, magic+version style matching world.c/registry.c:
      char   magic[4] = "DGES"
      uint32 version  = 2
      double elapsed
      float  speed
      float  saved_speed
      uint32 resource_count
      then resource_count times:
        uint8 name_len, char name[name_len]  (not NUL-terminated on disk)
        int32 amount

    speed/saved_speed are both persisted (not just elapsed) so saving
    while paused and reloading stays paused, and resuming afterward
    restores the speed you actually had before pausing — not a reset
    to 1x.

    Version 1 (fixed int32 wood + int32 stone, no name strings at all)
    migrates automatically on load: those two values become "wood" and
    "stone" entries in the new named store — same "migrate explicitly,
    never silently reinterpret" discipline as world.c's v3->v4 Tileset
    migration, not a policy that every future resource type needs a
    hardcoded migration path. Anything beyond a version 1/2 mismatch is
    still a hard failure, same policy as world_load: no migration
    support past the one compatibility bridge that actually needed to
    exist for pre-Phase-2B save files. */
bool simulation_save(const SimClock *clk, const ResourceStore *rs, const char *path);
bool simulation_load(SimClock *clk, ResourceStore *rs, const char *path);

#endif /* DGE_SIMULATION_H */
