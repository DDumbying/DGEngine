
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

/* Log current totals — called on any change and available on demand. */
void resource_store_log(const ResourceStore *rs);

#endif /* DGE_SIMULATION_H */
