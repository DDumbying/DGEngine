
#ifndef DGE_SIMULATION_H
#define DGE_SIMULATION_H

#include <stdbool.h>

/*  Simulation
    Two things live here:

    1. SimClock  — an in-world time counter, completely separate from the
       wall-clock delta-time used for camera movement and rendering.
       Game time passes at a configurable rate (default 1 game-second per
       real-second at speed 1).  Pausing stops game-time without stopping
       rendering, which is the correct separation for a simulation game.

    2. ResourceStore — the player's accumulated resources.  This is a
       simple flat struct at this stage: wood and stone.  It will grow in
       later phases (food, population cap, etc.).  Kept here rather than
       on an entity so there's a single authoritative place to read from
       for any future UI or scripting layer. */

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

typedef struct {
    int wood;
    int stone;
} ResourceStore;

void resource_store_init(ResourceStore *rs);

/* Add amounts (clamped to >= 0).  Logs the change. */
void resource_store_add_wood(ResourceStore *rs, int amount);
void resource_store_add_stone(ResourceStore *rs, int amount);

/* Read-only affordability checks. */
bool resource_store_has_wood(const ResourceStore *rs, int amount);
bool resource_store_has_stone(const ResourceStore *rs, int amount);

/*  Atomic check-and-subtract: returns false (no change, no log) if the
    store doesn't have at least `amount`; otherwise subtracts and logs.
    This is deliberately different from resource_store_add_wood/stone's
    unconditional clamp-at-0 behavior — passing a negative amount there
    silently clamps instead of rejecting, which is wrong for "can I
    afford this" callers (construction.c) that need a real yes/no
    before committing to spending. */
bool resource_store_try_spend_wood(ResourceStore *rs, int amount);
bool resource_store_try_spend_stone(ResourceStore *rs, int amount);

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
      uint32 version  = 1
      double elapsed
      float  speed
      float  saved_speed
      int32  wood
      int32  stone

    speed/saved_speed are both persisted (not just elapsed) so saving
    while paused and reloading stays paused, and resuming afterward
    restores the speed you actually had before pausing — not a reset
    to 1x. Version mismatch is a hard failure, same policy as
    world_load: no migration support, by choice. */
bool simulation_save(const SimClock *clk, const ResourceStore *rs, const char *path);
bool simulation_load(SimClock *clk, ResourceStore *rs, const char *path);

#endif /* DGE_SIMULATION_H */
