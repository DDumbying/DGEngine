#ifndef DGE_LEVEL_H
#define DGE_LEVEL_H

/*  Level — one map within a project.

    Before this existed, a project WAS one World — exactly one
    world.dge, exactly one entities.dge, forever. This is the direct
    answer to "why can't different levels have different map shapes":
    there was nowhere for a second map to live. See ENGINE_DESIGN.md
    §6 for the full design reasoning; this is Phase 3, Part A of the
    production sequence in ROADMAP.md — the core data model plus
    enough of main.c/panel.c wiring to create/switch between levels.
    LevelTransitionComponent (an entity walking through a door to
    change levels during actual gameplay) is Part B, not built here —
    Part A is an authoring-time concept (which map am I editing right
    now), not yet a runtime one (the player moving between maps).

    Each Level owns its own World — own dimensions, own WorldShape, own
    painted Tile.type values, own entities — but every Level in a
    project shares the same Tileset and ObjectDefRegistry (both of
    those already live independently of any single World/Registry
    instance, so "shared across levels" falls out for free rather than
    needing new plumbing). This is exactly why Tileset-as-data (Phase 1)
    had to land before this could: a Level system built on the old
    project-wide-fixed TerrainType enum would have forced every level
    to use the same five terrains. Now every level draws from the same
    *defined* palette but can use any subset of it, at any size, in any
    shape.

    A Level's world/entity data are NOT embedded in this struct — they
    live in their own .dge files (world_path/entity_path), in exactly
    the same v4/v9 formats world_save()/registry_save() already
    produce. A Level is a named pointer to a world.dge + entities.dge
    pair plus a default spawn point, not a new file format of its own.

    SimClock/ResourceStore/WeatherSystem stay project-wide, NOT
    per-level — sim.dge/weather.dge are unaffected by any of this. A
    colony's stockpile or a party's elapsed playtime doesn't reset when
    walking through a door to a different map; only the map and its
    entities are level-scoped. */

#include <stdbool.h>

#define LEVEL_NAME_MAX 64
#define LEVEL_MAX      64

typedef struct {
    char  name[LEVEL_NAME_MAX];    /* "Level 1", "Goblin Cave", whatever   */
    char  world_path[256];         /* this level's own world .dge file    */
    char  entity_path[256];        /* this level's own entities .dge file */
    float spawn_x, spawn_y;        /* default player/party entry point    */
    char  entry_marker[LEVEL_NAME_MAX]; /* reserved for Part B's named-marker
                                            transitions; unused by Part A  */
} Level;

typedef struct {
    Level levels[LEVEL_MAX];
    int   count;
    int   active_index;  /* -1 if count == 0 (should not normally happen --
                             see level_registry_bootstrap() below) */
} LevelRegistry;

void level_registry_init(LevelRegistry *lr);

/* Adds a new Level named `name`, with world_path/entity_path derived
   from the name (levels/<slug>_world.dge, levels/<slug>_entities.dge —
   see level.c for the exact slugging rule: lowercased, spaces to
   underscores, anything not [a-z0-9_] dropped, so "Goblin Cave!"
   becomes "goblin_cave"). Does NOT create the World/Registry data
   itself or touch disk beyond the manifest write callers do
   separately — this only registers the slot and reserves its paths.
   Returns the new index, or -1 if the registry is full
   (LEVEL_MAX) or name is empty/all-invalid-characters after slugging. */
int level_registry_add(LevelRegistry *lr, const char *name);

/* Bounds-checked accessors. NULL/false on an out-of-range index. */
const Level *level_registry_get(const LevelRegistry *lr, int index);
bool level_registry_set_active(LevelRegistry *lr, int index);

/* The currently active Level, or NULL if count == 0. Every main.c call
   site that used to read a fixed WORLD_SAVE_PATH/ENTITY_SAVE_PATH now
   reads level_registry_active(&levels)->world_path/entity_path
   instead. */
const Level *level_registry_active(const LevelRegistry *lr);

/* If the registry is empty, ensures exactly one Level exists, named
   "Level 1", sized to fit whatever the caller's project already
   uses (grid_w/grid_h are only used to compute nothing here, actually
   — Part A doesn't pre-size anything in this struct at all; the first
   Level's World gets created/sized by the SAME world_create() +
   world_topology_generate() call main.c's ENTER_EDITOR already makes,
   this function only guarantees there's a Level entry for that World
   to be saved into). A fresh project's LevelRegistry starting with
   exactly one auto-created Level (rather than zero) is the one
   deliberate exception to this consolidation's "start empty, honest"
   pattern (Tileset, ObjectDefRegistry) — there has to be at least one
   Level for the World tab to have anything to point at, the same way
   world_create() itself is never optional even when its Tileset is. */
void level_registry_bootstrap(LevelRegistry *lr);

/* Migrates a pre-Level-system project: if legacy_world_path and
   legacy_entity_path both exist on disk (a project saved before Phase
   3), renames them (not copies — this is a one-time upgrade, not a
   duplication) into the paths a freshly bootstrapped "Level 1" entry
   expects, so an existing project's map and entities survive this
   upgrade under the new Level scheme without the player losing
   anything. Safe to call on a project that never had legacy files
   (this is a no-op then) or one that already has a levels/ manifest
   (also a no-op — this only ever fires once, right before the first
   bootstrap on a project that predates Levels entirely). Returns true
   if a migration actually happened (caller uses this to decide whether
   to log it), false otherwise (including "nothing to migrate"). */
bool level_registry_migrate_legacy(const char *legacy_world_path,
                                    const char *legacy_entity_path,
                                    const Level *target);

/* levels/manifest.def — flat key=value, same convention as project.dge
   and .theme files (see ui/theme.c's own doc comment on why this
   pattern keeps getting reused: no new parsing infrastructure needed
   each time). Creates the levels/ directory if it doesn't exist yet. */
bool level_registry_save(const LevelRegistry *lr, const char *manifest_path);
bool level_registry_load(LevelRegistry *lr, const char *manifest_path);

#endif /* DGE_LEVEL_H */
