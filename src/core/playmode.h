#ifndef DGE_PLAYMODE_H
#define DGE_PLAYMODE_H

#include <stdbool.h>
#include "../world/world.h"
#include "../ecs/registry.h"
#include "../simulation/simulation.h"
#include "../simulation/weather.h"

/*  Edit/Play split.

    Everything built so far (World, Registry, ResourceStore, SimClock,
    WeatherSystem) already runs the same simulation code regardless of
    who's driving the input — a worker still pathfinds and builds the
    same way whether a click came from the editor's SELECT mode or a
    future game-rule's input handler. What's been missing isn't a
    second simulation, it's a second *input/UI* mode, plus a safe way
    to test it without risking the thing you're authoring.

    The model: entering Play takes a deep snapshot of the live
    World+Registry+ResourceStore+SimClock+WeatherSystem, then the game
    runs on the *live* structures as normal (same systems, unmodified)
    while input/UI is gated to "Play" behavior instead of "Edit"
    behavior (see main.c's GameMode checks). Exiting Play restores the
    snapshot byte-for-byte, so nothing a playtest does -- a building
    finishing, a tree being chopped, a worker dying -- ever leaks back
    into what you were editing. This is the same reasoning a save/load
    round-trip already gives you, just in-memory and automatic instead
    of needing an explicit F5/F9.

    All five structs happen to be safe to deep-copy with one rule
    apiece: Registry/ResourceStore/SimClock/WeatherSystem are plain
    fixed-size data (memcpy is a complete, correct copy) and World
    holds one heap pointer (Tile *tiles) that needs an actual
    allocation + copy, not an aliased pointer -- playmode_snapshot()
    handles that distinction internally so callers don't have to know
    which structs need which treatment. */

typedef struct {
    World         world;
    Registry      registry;
    ResourceStore resources;
    SimClock      sim_clock;
    WeatherSystem weather;
    bool          valid; /* false until playmode_snapshot() succeeds */
} PlaySnapshot;

/*  Deep-copies the five live structures into snap. Returns false (and
    leaves snap->valid false) only if the World's tile buffer fails to
    allocate -- everything else is a fixed-size copy that can't fail. */
bool playmode_snapshot(PlaySnapshot *snap, const World *world, const Registry *registry,
                        const ResourceStore *resources, const SimClock *sim_clock,
                        const WeatherSystem *weather);

/*  Restores the five live structures from a snapshot taken earlier by
    playmode_snapshot(). Frees the live World's current tile buffer and
    replaces it with a fresh copy of the snapshot's (so the live World
    ends up with its own allocation, not aliased to snap's) -- after
    this call snap can still be safely passed to playmode_snapshot_free()
    and is untouched, so the same snapshot could be restored from again
    if that's ever useful (e.g. "restart playtest" without re-snapshotting). */
void playmode_restore(PlaySnapshot *snap, World *world, Registry *registry,
                       ResourceStore *resources, SimClock *sim_clock,
                       WeatherSystem *weather);

/* Frees the snapshot's own tile buffer. Call when done with a snapshot
   (after restoring, or when abandoning a playtest without restoring). */
void playmode_snapshot_free(PlaySnapshot *snap);

#endif /* DGE_PLAYMODE_H */
