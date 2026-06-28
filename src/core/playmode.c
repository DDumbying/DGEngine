#include "playmode.h"

#include <string.h>
#include "../core/log.h"

bool playmode_snapshot(PlaySnapshot *snap, const World *world, const Registry *registry,
                        const ResourceStore *resources, const SimClock *sim_clock,
                        const WeatherSystem *weather) {
    snap->valid = false;

    /* World is the one struct here with a heap pointer (Tile *tiles) --
       everything else below is a flat memcpy of fixed-size data, which
       is already a complete, correct deep copy with nothing else to do. */
    if (!world_create(&snap->world, world->width, world->height)) {
        LOG_ERROR("playmode_snapshot: failed to allocate %dx%d tile buffer",
                  world->width, world->height);
        return false;
    }
    memcpy(snap->world.tiles, world->tiles,
           (size_t)world->width * (size_t)world->height * sizeof(Tile));

    memcpy(&snap->registry,  registry,  sizeof(Registry));
    memcpy(&snap->resources, resources, sizeof(ResourceStore));
    memcpy(&snap->sim_clock, sim_clock, sizeof(SimClock));
    memcpy(&snap->weather,   weather,   sizeof(WeatherSystem));

    snap->valid = true;
    LOG_INFO("Play mode: snapshot taken (%dx%d world, ready to restore on Stop)",
             world->width, world->height);
    return true;
}

void playmode_restore(PlaySnapshot *snap, World *world, Registry *registry,
                       ResourceStore *resources, SimClock *sim_clock,
                       WeatherSystem *weather) {
    if (!snap->valid) {
        LOG_ERROR("playmode_restore: called with no valid snapshot -- ignoring");
        return;
    }

    /* world_destroy() frees the live tiles buffer (whatever Play did to
       it), then world_create() + memcpy gives the live World a fresh
       allocation holding the snapshot's contents -- not an alias to
       snap->world.tiles, so freeing the snapshot later (or restoring
       from it again) can't double-free or corrupt the live copy. */
    world_destroy(world);
    if (!world_create(world, snap->world.width, snap->world.height)) {
        LOG_ERROR("playmode_restore: failed to reallocate %dx%d tile buffer -- "
                  "world left empty, this should not happen", snap->world.width,
                  snap->world.height);
        return;
    }
    memcpy(world->tiles, snap->world.tiles,
           (size_t)snap->world.width * (size_t)snap->world.height * sizeof(Tile));

    memcpy(registry,  &snap->registry,  sizeof(Registry));
    memcpy(resources, &snap->resources, sizeof(ResourceStore));
    memcpy(sim_clock, &snap->sim_clock, sizeof(SimClock));
    memcpy(weather,   &snap->weather,   sizeof(WeatherSystem));

    LOG_INFO("Play mode: stopped -- editor state restored");
}

void playmode_snapshot_free(PlaySnapshot *snap) {
    if (!snap->valid) return;
    world_destroy(&snap->world);
    snap->valid = false;
}
