# DGEngine — Engine Design Document

**A to Z: what this engine is, how every system works, and how it produces
a real, shippable isometric game.**

This document is the production-level design reference. `ROADMAP.md` tracks
*sequencing* (what order things get built in); this document is the
*architecture* everything in that sequence has to fit into. Where a system
below is already built, it's marked **[BUILT]** with the real file names.
Where it's designed but not yet built, it's marked **[PLANNED]**. Nothing
in this document is aspirational marketing — every BUILT tag reflects code
that exists and compiles today; every PLANNED section is a real, scoped
design, not a wish.

---

## Table of Contents

1. [Vision & Non-Negotiable Principles](#1-vision--non-negotiable-principles)
2. [The Three Layers: Engine, Project, Runtime](#2-the-three-layers-engine-project-runtime)
3. [Coordinate Spaces](#3-coordinate-spaces)
4. [The Camera System](#4-the-camera-system)
5. [World & Tile System](#5-world--tile-system)
6. [The Level / Scene System](#6-the-level--scene-system)
7. [Content Authoring: Tileset & ObjectDef](#7-content-authoring-tileset--objectdef)
8. [The ECS](#8-the-ecs)
9. [Simulation Layer](#9-simulation-layer)
10. [AI: Pathfinding & Tasks](#10-ai-pathfinding--tasks)
11. [Scripting: the Lua Host](#11-scripting-the-lua-host)
12. [Genre: How One Engine Makes Different Games](#12-genre-how-one-engine-makes-different-games)
13. [Win / Lose / Goals](#13-win--lose--goals)
14. [The Editor](#14-the-editor)
15. [Theming & Editor Preferences](#15-theming--editor-preferences)
16. [Save Formats & Versioning](#16-save-formats--versioning)
17. [The Runtime Build](#17-the-runtime-build)
18. [Audio](#18-audio)
19. [Testing Philosophy](#19-testing-philosophy)
20. [Full File Map (End State)](#20-full-file-map-end-state)
21. [Production Sequencing](#21-production-sequencing)
22. [Explicit Non-Goals](#22-explicit-non-goals)

---

## 1. Vision & Non-Negotiable Principles

DGEngine is a **C11 engine specialized for isometric games** — not one
game with an editor bolted on. Every design decision in this document is
downstream of five rules. When something in the codebase violates one of
these, that's a defect to fix, not a style preference to argue about.

**1. The engine knows shapes of data. It never knows meanings.**
The engine may know "a tile has an integer type index." It may never know
"a tile can be grass." It may know "an entity can carry a Transform, a
Sprite, a Health component." It may never know "a worker can harvest
wood." The moment engine source contains a game-specific word — `campfire`,
`wood`, `goblin` — that's a defect. `TerrainType` containing `grass` was
this defect; it's fixed. `BuildingKind` containing `campfire` is this same
defect, not yet fixed — see [§7](#7-content-authoring-tileset--objectdef).

**2. A project is 100% data. The engine is 100% code.**
Tileset, ObjectDefs, Levels, win/lose scripts, which simulation systems
run — none of it should require recompiling the engine. If defining new
game content ever requires editing a `.c` file, that's a design failure
for that piece of content.

**3. Editor and Runtime are two consumers of the same project data.**
They are not two modes of one process. The editor authors data. The
runtime plays it. A shipped game is engine code plus one project's data,
with zero editor UI compiled in. Until this split is real
(see [§17](#17-the-runtime-build)), DGEngine produces editable sandboxes,
not shippable games — this is named plainly, not softened.

**4. Genre is an emergent property of project data, not an engine switch.**
`GenreProfile` (SANDBOX_SIM / TACTICS / FREEFORM) is a **transitional**
mechanism — a coarse gate on which built-in systems tick, useful today
because the engine hasn't finished separating "what runs" from "what it
means." The end state is that a project's own `rules.def`
(see [§12](#12-genre-how-one-engine-makes-different-games)) fully
determines behavior, and the fixed three-genre enum becomes unnecessary.
This is a known, named piece of debt — not a hidden one.

**5. Every system fails honestly, never silently.**
An undefined tile renders as a missing-texture checker, not a guessed
color. An entity with a stale reference is caught and logged, not
dereferenced blind. A save file from an old version migrates explicitly
or is refused with a clear error — never silently reinterpreted. This
principle shows up constantly in the codebase's actual comments and is
treated as load-bearing, not decorative.

---

## 2. The Three Layers: Engine, Project, Runtime

```
ENGINE    Generic C11 code. Ships unmodified across every project.
          Contains zero game-specific words. Owns: ECS, renderer,
          world/tile/shape, spatial grid, save/load, Lua host,
          asset/atlas pipeline, input/window/platform, camera.

PROJECT   Pure data, authored in the editor. Zero C required to define
          new content:
            - Tileset       (terrain slots: name + sprite + walkable)
            - ObjectDefs    (entity templates: properties + behaviors)
            - Levels        (one or more Worlds, each independently
                              shaped/sized, sharing the project's
                              Tileset/ObjectDefs)
            - rules.def     (which systems run, win/lose conditions,
                              turn-based vs realtime)
            - Lua scripts   (behavior, win/lose logic, per-object
                              event handlers)

RUNTIME   Engine + exactly one project's data, editor UI stripped
          entirely. This is what a player downloads and runs. Doesn't
          exist yet — see §17. Until it exists, DGEngine produces
          editable sandboxes, not distributable games.
```

The Project Manager (`ui/project_manager.c`) **[BUILT]** and the
`WorldShape`/Shape Pane system **[BUILT]** already behave like correctly
Engine-scoped pieces — neither one knows anything about wood, campfires,
or grass. They're the template every other system is being brought up to
match.

---

## 3. Coordinate Spaces

Three spaces, kept deliberately distinct. Conflating them is the single
most common isometric-engine bug in the wild (and was, historically, the
source of DGEngine's own click-hitbox bugs before Tileset-as-data forced a
cleanup — see `ui/panel.c`'s header comment on `thumb_rect()`).

**Grid space** — integer tile coordinates `(x, y)`. All gameplay logic
lives here. Pathfinding (`ai/pathfinder.c`), occupancy
(`world/spatial_grid.c`), adjacency, "can I stand here" — every one of
these operates purely in grid space and has no idea the game is rendered
isometrically. **[BUILT]**

**Screen space** — actual pixels on the window. The isometric look is
*only* a projection formula applied at draw time:

```
screen_x = (grid_x - grid_y) * (tile_w / 2)
screen_y = (grid_x + grid_y) * (tile_h / 2)
```

This lives entirely inside `renderer_draw_iso_tile()` /
`renderer_draw_iso_tile_uv()` (`renderer/renderer.c`). Nothing outside the
renderer ever computes this transform itself — every other system reasons
in grid space and lets the renderer project at the last possible moment.
**[BUILT]**

**Camera space** — an offset + zoom applied between grid and screen. The
camera never rotates the world; true isometric is a fixed-angle
projection, not an orbiting 3D camera. `Camera` (`renderer/camera.h`) is
position + zoom + viewport, exactly this shape. **[BUILT]**

**Picking** (turning a screen-space mouse click back into a grid tile) is
the inverse of the projection formula, implemented once in
`editor/editor.c`'s `pick_tile()` and reused everywhere clicking-a-tile
matters. **[BUILT]**

---

## 4. The Camera System

### What's built

`Camera` (`renderer/camera.h`) supports pan, zoom-at-point, and
screen-to-world conversion. **[BUILT]** Today it is exclusively
**user-driven** — the editor's free pan/zoom, appropriate for authoring,
inappropriate for a shipped game's runtime.

### What's planned: camera drivers

A camera in a real game is driven by *something* — the current driver is
swapped based on context, not hardcoded into one behavior:

```c
typedef enum {
    CAMERA_DRIVE_FREE,      /* editor: user pan/zoom, no bounds       */
    CAMERA_DRIVE_FOLLOW,    /* runtime: centered on an entity, with
                                deadzone + optional lerp smoothing    */
    CAMERA_DRIVE_BOUNDED,   /* runtime: free pan, but clamped to the
                                current Level's extent                */
    CAMERA_DRIVE_FIXED,     /* runtime: locked — many tactics games
                                don't let you pan or zoom at all       */
} CameraDriveMode;
```

`CAMERA_DRIVE_BOUNDED`'s clamp rectangle comes directly from the current
`Level`'s `World` dimensions (see [§6](#6-the-level--scene-system)) — this
is the concrete link between "camera behavior" and "map shape" the
original question was really asking about. A tactics level and a sandbox
level can use the same camera *code* with entirely different bounds,
because the bounds are level data, not camera code.

`rules.def` (see [§12](#12-genre-how-one-engine-makes-different-games))
picks the default drive mode for a project; a Level can override it (a
cutscene-style intro level might force `CAMERA_DRIVE_FIXED` even in a
project that's normally `CAMERA_DRIVE_BOUNDED`).

**[PLANNED]** — scoped, not yet built. Small in code size once the Level
system exists to source bounds/follow-targets from.

---

## 5. World & Tile System

### World

`World` (`world/world.h`) **[BUILT]** is a flat `Tile[width*height]` grid
plus:
- a `Tileset` (which terrain slots exist — see [§7](#7-content-authoring-tileset--objectdef))
- a `WorldShape` (which tiles are part of the playable area)

A `Tile` (`world/tile.h`) is `{ int type; float height; unsigned char
variant; }`. `type` is an index into the World's own `Tileset`, or `-1`
for undefined. There is no engine-level default terrain. **[BUILT]**

### WorldShape

`WorldShape` (`world/world_shape.h`) **[BUILT]** is a packed bitmask,
one bit per tile, deciding whether that tile is part of the playable
world at all. Disabled tiles are not rendered, not paintable, not
walkable, not shown on the minimap — true holes, not a "wall" terrain
type pretending to be a boundary.

Three ways a WorldShape gets its bits set:
1. **Manual painting** — the Shape Pane (`ui/shape_pane.c`, **[BUILT]**),
   a flat orthogonal grid (deliberately *not* isometric — see that file's
   own design notes on why diamond hit-testing is worse for reasoning
   about topology than a plain square grid).
2. **Topology generators** — `world_topology_generate()`
   (`world/world_generator.c`, **[BUILT]**): RECT (no-op), FREEFORM
   (blank, paint outward), ISLAND (noisy coastline falloff), ROOMS (BSP
   dungeon). All four only ever touch the shape mask, never `Tile.type`
   — an island is a boundary, not a color scheme.
3. **[PLANNED]** Runtime script mutation — a project could conceivably
   grow/shrink its playable area during play (a "the floor is
   collapsing" mechanic). `world_shape_set()` is already safe to call at
   any time; a Lua binding (`dge.set_tile_enabled(x, y, bool)`) is a small
   addition once scripting needs it.

### Rendering

`world_render()` (`world/world.c`) **[BUILT]** draws every playable tile:
a real sprite if the tile's Tileset slot has one assigned, an honest
missing-texture checker otherwise. Two-pass (textured batch, then
untextured missing-tile batch) to avoid per-tile texture rebinding.

### What's explicitly not built: chunking

At the sizes DGEngine currently targets (hundreds of tiles per side), a
flat array + a flat bitmask is fine. A truly enormous sparse world
(RimWorld-at-planet-scale, an open-world isometric RPG) would eventually
want chunk streaming — only the chunks near the camera loaded and
rendered. This is a real, large piece of future work, deliberately
**[PLANNED, unscoped]** — not needed for the current production target
and not worth designing prematurely.

---

## 6. The Level / Scene System

**This is the largest concrete gap this document identifies. Nothing
below is built yet.** Its absence is the direct answer to "why can't
different levels have different map shapes" — there is currently exactly
one `World` in the entire running program. A project doesn't *contain* a
map; it *is* one map.

### The data shape

```c
typedef struct {
    char name[64];              /* "Level 1", "Goblin Cave", whatever   */
    char world_path[256];       /* level's own .dge file, own Tileset
                                    reference (shared project Tileset,
                                    but this Level's own painted tiles
                                    and its own WorldShape)              */
    float spawn_x, spawn_y;     /* default player/party entry point     */
    char entry_marker[64];      /* optional: named entry point, so a
                                    transition can target "the door
                                    that leads back to town" rather than
                                    always the same default spawn        */
} Level;

typedef struct {
    Level levels[LEVEL_MAX];
    int   count;
    int   active_index;         /* which Level is currently loaded      */
} LevelRegistry;
```

Each `Level` owns its own `World` — its own dimensions, its own
`WorldShape`, its own painted `Tile.type` values — but all Levels in a
project share the *same* `Tileset` and `ObjectDefRegistry`. This is
exactly why Tileset-as-data
([§7](#7-content-authoring-tileset--objectdef)) had to happen first: a
Level system built on top of the old per-project-fixed `TerrainType`
enum would have been forced to make every level use the same five
terrains. Now, every level draws from the same *defined* palette but can
use any subset of it, at any size, in any shape.

### Transitions

An entity carries an optional `LevelTransitionComponent`:

```c
typedef struct {
    char target_level[64];      /* Level.name to load                   */
    char target_marker[64];     /* which entry_marker to spawn at, or
                                    "" for the Level's default spawn_x/y */
} LevelTransitionComponent;
```

Walking onto (or interacting with) an entity carrying this component
triggers: unload current Level's `World`/`Registry` state (or push it on
a stack, for "go back" semantics — decision deferred to implementation
time), load the target Level's `World`, reposition the player entity at
the target marker, reposition the camera (see [§4](#4-the-camera-system)'s
`CAMERA_DRIVE_BOUNDED` picking up the new Level's extent automatically).

### How this maps onto real games

- **Diablo/Path of Exile-style**: many small-to-medium Levels, linear or
  branching transitions, each hand-authored or procedurally regenerated
  per-run.
- **Stardew-style**: farm/town/mine-floor-N are all separate Levels,
  connected by named markers ("came in from the north gate").
- **RimWorld/sandbox-style**: effectively one Level per playthrough — the
  Level system doesn't force multiple levels to exist, a project can
  define exactly one and never use transitions at all.
- **Tactics-style (Fire Emblem)**: each battle map is its own Level,
  often with no return transition — completing one loads the next in a
  fixed sequence, which is just `rules.def`-driven scripting on top of
  the same transition mechanism.

The Level system doesn't presume any of these shapes. It's the minimal
data structure that makes all of them expressible.

### Editor implications

- Project Manager gains a Levels list (parallel to how it already lists
  recent projects) — create/rename/delete/duplicate a Level.
- The World tab edits whichever Level is currently active; switching
  active Level swaps which `World`/`WorldShape` the panel and Shape Pane
  are pointed at. No change to `panel.c`'s internals — it already takes
  a `World*` parameter, so "which World" becomes "which Level's World,"
  not a new code path.
- Save format: `project.dge` gains a `levels/` manifest; each Level's
  `.dge` world file is otherwise identical in format to what
  `world_save()`/`world_load()` already produce (**[BUILT]**, v4 format,
  see [§16](#16-save-formats--versioning)) — a Level *is* a named
  `world.dge` file plus spawn metadata, not a new file format.

**Sequencing note:** this is Phase 3 in the production sequence
([§21](#21-production-sequencing)), after ObjectDef consolidation. It's
listed here in full because it's architecturally central, not because
it's next in line to be coded.

---

## 7. Content Authoring: Tileset & ObjectDef

### Tileset — terrain as data **[BUILT]**

`world/tileset.h`. An ordered list of slots (`TILESET_MAX_SLOTS = 64`),
each: a display name (project's own words, never read by engine logic),
a sprite reference (`-1` = unassigned, renders as missing-texture), and a
`walkable` bool — the *only* piece of meaning the engine itself needs
from a tile, because pathfinding has to know it.

A new project starts with an **empty** Tileset. This is deliberate — see
[§1](#1-vision--non-negotiable-principles) principle 1. `PAINT` mode's
sidebar is a live, editable list of the active Level's Tileset: click a
row to paint with it, right-click to rename, click the swatch to assign
a sprite, "+ ADD TILE" to define a new slot. `ui/panel.c` **[BUILT]**.

### ObjectDef — entities as data **[BUILT]**

`core/object_def.h`. Already the correctly-designed half of content
authoring — the template every other content system (Tileset just
finished catching up to it; `BuildingKind`/`PrefabKind`/`ResourceKind`
below have not yet). An `ObjectDef` is a name, a sprite, a flat bag of up
to `OBJDEF_MAX_PROPS` typed properties (`int`/`float`/`string`/`bool`),
and up to `OBJDEF_MAX_BEHAV` named event→Lua-script bindings
(`on_click`, `on_tick`, `on_build`, and any project-invented event name —
the engine doesn't enumerate valid event names, it just calls whatever's
bound). Persisted as plain-text `.obj` files under the project's
`objects/` folder — human-editable outside the engine entirely if
someone wants to.

Two property names (`build_time`, `build_cost_kind`, `build_cost_amount`)
are recognized by convention, not by a type system — their *presence*
turns an ObjectDef into something `construction.c`'s systems treat as
buildable. This mirrors `BuildingKind`'s cost/time contract on purpose.

### PrefabKind / BuildingKind — retired **[BUILT]**

**Placement (Phase 2, Part A) is done.** `PrefabKind` (`game/prefabs.h`)
and `BuildingKind` (`simulation/construction.h`) — tree/rock/worker's
fixed-enum instant placement and campfire's fixed-enum resource-costed
blueprint — are gone. Every placeable/buildable thing is now an
`ObjectDef` instance; `objdef_is_buildable()` decides, per-def, whether
placing it drops an instant entity (`objdef_spawn_instance()`) or a
construction blueprint (`construction_place_blueprint_objdef()`). PLACE
mode's sidebar palette is an ObjectDef list, the same row-based shape as
PAINT mode's Tileset palette (§7 above) — click a row to select it as
the stamp.

Retiring these also deleted `sprite_id_for_prefab()`/
`sprite_id_for_building()` (dead code, never called outside `atlas.c`)
and fixed a real layering violation those two functions caused:
`renderer/atlas.h` no longer `#include`s `game/prefabs.h`/
`simulation/construction.h`, so the renderer layer no longer reaches
upward into game-layer types — consistent with
[§1](#1-vision--non-negotiable-principles)'s first principle.

It also fixed a real latent bug: the old RMB-delete refund path read a
blueprint's cost via `building_cost_kind(c->kind)` unconditionally,
which was correct for the one hardcoded `BUILDING_CAMPFIRE` but silently
wrong for a custom ObjectDef blueprint (whose `kind` field was never
set). A single always-`def_name`-driven path can't have that bug — the
refund always reloads the real definition and resolves its actual cost.

Entity save format bumped to v8 accordingly (`ConstructionComponent`
drops `kind`/`is_custom`, always carries `def_name`); v7 and earlier
migrate automatically on load, synthesizing `def_name="Campfire"`
wherever the old format implied `BUILDING_CAMPFIRE` — same "migrate
explicitly, never silently reinterpret" discipline as world.c's v3→v4
Tileset migration ([§16](#16-save-formats--versioning)).

### ResourceKind — not yet consolidated

**[PLANNED — Phase 2, Part B, not yet started.]**

`ResourceKind` (`simulation/simulation.h`) — wood/stone, a 2-value enum
— is the one piece of the original three-enum problem left standing.
The plan: `ResourceKind`'s fixed wood/stone becomes project data, a
`resources.def` listing whatever resource kinds a project wants (a
tactics game defines zero; a farming game defines `seed`/`crop`/`gold`).
`ResourceStore` becomes a dynamic map instead of a two-field struct.

This touches real surface area beyond what Part A did:
`ResourceComponent`, `harvest.c`'s wood/stone dispatch, the sim save
format (`sim.dge`), the HUD's `W:%d S:%d` readout, and the Lua API's
`dge.get_resource(kind)`/`dge.add_resource(kind, amount)` (currently
hardcoded to only accept the strings `"wood"`/`"stone"`). Deliberately
scoped as its own pass rather than bundled into Part A.

This is real, scoped, next-in-line work — not a someday item.

---

## 8. The ECS

`ecs/registry.h` / `ecs/components.h` **[BUILT]**. Fixed parallel
component arrays, `MAX_ENTITIES = 4096`, `alive[]` bitmask plus one flat
array per component type (`TransformComponent`, `RenderableComponent`,
`HealthComponent`, `ResourceComponent`, `MoveComponent`, `TaskComponent`,
`ConstructionComponent`, `DefinitionComponent`). No behavior lives on a
component — every component is plain data, all behavior lives in systems
that read/write by entity id.

`world/spatial_grid.h` **[BUILT]** gives O(1) "what entity (if any)
occupies this tile" lookup — a flat `Entity[width*height]` array, correct
for the current invariant that at most one entity occupies a tile at a
time.

**[PLANNED, unscoped]**: at 4096 entities and a `bool
has_X[MAX_ENTITIES]` per component type, memory is wasted and iteration
isn't cache-friendly at scale. A dense/sparse-set layout would raise the
practical entity ceiling by an order of magnitude or more. Not a current
bottleneck; named here so it isn't forgotten, not scheduled.

---

## 9. Simulation Layer

`simulation/simulation.h` (`SimClock`, `ResourceStore`),
`simulation/weather.h` (`WeatherSystem`), `simulation/harvest.h`,
`simulation/construction.h` — all **[BUILT]**.

`SimClock` advances in-world time with a configurable speed multiplier
and pause/resume. `WeatherSystem` auto-advances through
sunny/rain/snow/none, affecting movement speed. `harvest.c`/
`construction.c` are the systems that currently only understand
`ResourceKind`/`BuildingKind` — see [§7](#7-content-authoring-tileset--objectdef)
for their consolidation into ObjectDef-driven equivalents
(`construction_place_blueprint_objdef()` already exists alongside the
`BuildingKind` path, **[BUILT]**, as the sibling that Phase 2 promotes to
the *only* path).

**Gating**: main.c only ticks this whole layer for `GENRE_SANDBOX_SIM`
projects (**[BUILT]**, see [§12](#12-genre-how-one-engine-makes-different-games)).
`TACTICS`/`FREEFORM` projects skip it entirely rather than ticking a
clock nothing reads.

---

## 10. AI: Pathfinding & Tasks

`ai/pathfinder.c` **[BUILT]**: A* over the tile grid, blocked by
`world_tile_walkable()` (Tileset-driven, see [§7](#7-content-authoring-tileset--objectdef)).

`ai/agent.c` **[BUILT]**: task-driven movement —
`TASK_IDLE`/`TASK_MOVE_TO`/`TASK_HARVEST`/`TASK_BUILD`, embedded `Path`
in `TaskComponent` so save/load round-trips it without heap bookkeeping.

**[PLANNED]**: `TASK_HARVEST`/`TASK_BUILD` are exactly the kind of
engine-presumed-meaning task types [§1](#1-vision--non-negotiable-principles)
warns against — they only make sense for `GENRE_SANDBOX_SIM`. The
consolidation plan: `TaskKind` becomes an open string/id a project's Lua
scripts register handlers for (`on_task_tick(task_name, entity, dt)`), and
`agent.c` dispatches by that name instead of a fixed enum. A tactics
project registers `attack`/`defend`; a farming project registers
`till`/`water`. This naturally falls out of the Phase 2 ObjectDef
consolidation and isn't separately scheduled — it's the same underlying
fix applied to task types instead of placement types.

---

## 11. Scripting: the Lua Host

`scripting/lua_host.c`/`.h` **[BUILT]**. One shared `lua_State`, safe
standard libs only (`base`/`math`/`string`/`table` — no `io`/`os`/`package`/
`debug`, so scripts can't touch the filesystem or the process directly).
Scripts load-and-cache on first use; `lua_host_clear_cache()` forces a
reload after an edit.

Engine API surface exposed today:

```
dge.get_property(entity, name)      dge.set_property(entity, name, value)
dge.move_to(entity, x, y)
dge.spawn(def_name, x, y)           dge.destroy(entity)
dge.get_resource(kind)              dge.add_resource(kind, amount)
dge.get_genre()                     dge.log(message)
```

`dge.get_genre()` **[BUILT]** exists specifically so a `TACTICS`/
`FREEFORM` project's scripts can tell, at runtime, that the engine isn't
ticking `SimClock`/weather for them and adjust — e.g. driving their own
turn timer instead of expecting `on_tick` to mean "a frame of simulated
time passed."

**[PLANNED]** API growth, each tied to a system above becoming real:
`dge.set_tile_enabled(x, y, bool)` (WorldShape mutation, §5),
`dge.load_level(name, marker)` (Level transitions, §6),
`dge.win()`/`dge.lose()` (§13), `dge.camera_follow(entity)`/
`dge.camera_set_bounds(...)` (camera drivers, §4).

---

## 12. Genre: How One Engine Makes Different Games

`GenreProfile` (`core/project.h`) **[BUILT]**: `GENRE_SANDBOX_SIM`,
`GENRE_TACTICS`, `GENRE_FREEFORM`, chosen once at project creation.
Currently gates:

- Whether `SimClock`/`WeatherSystem`/harvest-build AI tick at all
  (`main.c`, **[BUILT]**)
- Whether the World panel shows Weather controls (`ui/panel.c`,
  **[BUILT]**)
- Whether the HUD shows resource/clock/weather readouts vs. just the
  genre name (`ui/ui.c`, **[BUILT]**)
- What `dge.get_genre()` reports to scripts (**[BUILT]**)

This is explicitly named in [§1](#1-vision--non-negotiable-principles)
as **transitional**. It exists because the engine hasn't finished
separating "which systems run" from "what they mean" — a fixed 3-value
enum is a coarse stand-in for what should eventually be pure project
data. The end state, once [§7](#7-content-authoring-tileset--objectdef)'s
consolidation and a `rules.def` asset both exist:

```
# rules.def — replaces GenreProfile as the actual source of truth
simulation       = true | false
time_model       = realtime | turnbased
camera_drive     = free | follow | bounded | fixed
resources        = <list, possibly empty, defined in resources.def>
win_condition    = scripts/win_check.lua
lose_condition   = scripts/lose_check.lua
```

At that point `GenreProfile` can either be deleted (every project just
has a `rules.def`) or kept as a *starter template picker* in the Project
Manager UI ("Sandbox Sim" pre-fills a `rules.def` with simulation=true;
"Tactics" pre-fills turnbased+bounded-camera) — a convenience for new
projects, not an engine-level behavioral gate anymore. This document
takes no firm position yet on which of those two outcomes is better;
it's flagged as a decision to make once `rules.def` actually exists,
not before.

---

## 13. Win / Lose / Goals

**[PLANNED, unscoped in detail.]** Without this, nothing produced by
DGEngine is "a game" in the ordinary sense — it's a sandbox with no
objective. Minimal viable design:

```c
typedef struct {
    bool  has_win_condition;
    char  win_script[256];       /* Lua path, evaluated each tick when
                                     simulation is on; returns bool     */
    bool  has_lose_condition;
    char  lose_script[256];
    char  on_win_script[256];    /* called once, when win fires         */
    char  on_lose_script[256];
} GameRules;
```

Stored as part of `rules.def` ([§12](#12-genre-how-one-engine-makes-different-games)).
Evaluated once per simulation tick (or once per turn, for turn-based
projects) only while in Play/Runtime, never in the editor's authoring
state. On trigger: a simple full-screen overlay (win/lose banner, final
stats, "return to editor" or "quit" depending on Editor vs Runtime
context) — one render function, deliberately small in scope. The point
of this phase is proving "project data can define what winning means"
end to end, not building a scoring/achievements system.

---

## 14. The Editor

### Screens

`PROJECT_MANAGER` → `EDITOR`, a simple two-state `Screen` enum in
`main.c` **[BUILT]**. The Project Manager (`ui/project_manager.c`,
**[BUILT]**) is the "hub" — Recent/New/Open, world topology and genre
chosen once at creation. Structurally sound already; not being reworked.

### Tabs

`WORLD` / `OBJECTS` / `SPRITES` / `SCRIPTS` / `SETTINGS`, plus a
`PLAY`/`STOP` control (`ui/tabbar.c`, **[BUILT]**).

- **World** — the tile/entity editor: PAINT/PLACE/SELECT/SHAPE modes,
  the Tileset palette sidebar (**[BUILT]**, [§7](#7-content-authoring-tileset--objectdef)),
  the docked Shape Pane (**[BUILT]**, [§5](#5-world--tile-system)).
- **Objects** — create/edit/delete ObjectDefs, property editor, behavior
  slot editor (`ui/objects_tab.c`, **[BUILT]**).
- **Sprites** — imported-image and atlas-sprite browser, real pixel
  thumbnails via `renderer_draw_quad_uv()` (`ui/sprites_tab.c`,
  **[BUILT]**).
- **Scripts** — currently a placeholder tab. **[PLANNED]**: at minimum,
  a path field per script reference plus "open in external editor"
  (most developers will use their own editor for Lua anyway — this
  doesn't need an in-engine code editor to be useful); inline
  syntax-error surfacing once the Lua host's compile-cache reports
  errors somewhere the UI can read them.
- **Settings** — real developer settings: Project (name/path/close),
  World (grid size, tile pixel size, topology), Display (grid lines,
  minimap, vsync), Editor (theme picker, docked/floating panel), About.
  Two-pane sidebar-of-sections layout (`ui/settings_tab.c`, **[BUILT]**).

### Play mode

`main.c`'s `MODE_EDIT`/`MODE_PLAY` **[BUILT]**: gates `editor_update()`/
`panel_update()` behind `MODE_EDIT` so painting/placing is disabled while
playing; snapshot/restore (`core/playmode.h`, **[BUILT]**) preserves
editor state across a play session. This is **in-editor playtesting**,
correctly scoped as that — it is explicitly not the Runtime
([§17](#17-the-runtime-build)) and doesn't try to be.

### Floating panel mode

`EditorSettings.panel_mode` (`WORLD_PANEL_DOCKED` / `WORLD_PANEL_FLOATING`)
**[BUILT as a setting]**, actual floating/draggable rendering
**[PLANNED]** — the toggle exists in Settings today but `panel.c` only
implements the docked layout so far.

---

## 15. Theming & Editor Preferences

`ui/theme.h`/`.c` **[BUILT]**: a single global `Theme` struct, loaded
once from a flat `key=value .theme` file (`themes/default.theme`,
`blue.theme`, `purple.theme`, `amber.theme` ship as starter palettes),
read everywhere through `theme_current()` — no parameter-threading
through every render function's signature. A community theme is a new
`.theme` file dropped in `themes/`, zero recompilation.

`EditorSettings` (`ui/settings_tab.h`) **[BUILT]** persists to
`~/.config/dgengine/editor.cfg` — theme path, panel mode, display
toggles survive across sessions.

This system is explicitly editor-only, not project data — a theme is a
*developer's* preference for how the editor looks while they work, not
something a shipped game reads.

---

## 16. Save Formats & Versioning

### `project.dge` **[BUILT]**

Flat text, `key=value` lines: name, path, grid dimensions, tile pixel
size, topology, genre. Unknown/missing keys fall back to
`project_defaults()` — this is how `genre=` safely didn't exist in
pre-GenreProfile save files without breaking old projects.

### World `.dge` binary format **[BUILT]**, currently v4

```
magic    "DGEW"
version  uint32
width, height  uint32
tiles[width*height]:  int32 type, uint8 variant, float height
shape_active  uint8
  if active: width, height, then packed bitmask bytes
tileset.count  uint32
  count times: name_len+name, int32 sprite_id, uint8 walkable
```

Version history, all still loadable:
- **v1** — tiles only, no shape, no tileset section.
- **v2** — adds the WorldShape section.
- **v3** — adds a fixed 5-slot `sprite_map` tail (the pre-Tileset
  system).
- **v4** — replaces the v3 sprite_map with a full `Tileset` table.
  **v3 files migrate automatically on load**: the old 5-slot sprite_map
  becomes an equivalent 5-slot Tileset (`Grass`/`Dirt`/`Sand`/`Water`/
  `Stone`, walkable=true except Water), so a project saved before this
  change keeps its sprite assignments and its tiles keep meaning what
  they used to mean. v1/v2 files have no terrain-meaning data to
  migrate — they load with an empty Tileset, rendered as missing-texture,
  an honest reflection of "this project never defined what its terrain
  means" rather than silently reinstating hardcoded colors that no
  longer exist anywhere in the engine.

Malformed/truncated files are refused with a logged error, never
partially loaded — see [§1](#1-vision--non-negotiable-principles)
principle 5.

### `objects/<name>.obj` **[BUILT]**

Plain text, human-editable, one file per ObjectDef.

### **[PLANNED]** additions

- `levels/` manifest + per-level `.dge` files ([§6](#6-the-level--scene-system))
  — no new binary format needed, a Level's world file is the same v4
  format that already exists.
- `resources.def`, `rules.def` — flat `key=value`, same convention as
  `project.dge` and `.theme` files. No new parsing infrastructure
  needed; the pattern is established three times over already.

---

## 17. The Runtime Build

**[PLANNED, unscoped in detail — the single largest remaining piece of
"is this an engine" work.]**

Today, "playing" a DGEngine project means running the editor binary and
clicking Play. There is no artifact smaller than the whole editor that a
player could be handed. Concretely, a Runtime needs:

- **Build step**: given a project folder, produce a binary (or a
  data-bundle the shared engine binary loads) containing: the engine's
  compiled systems, that project's Tileset/ObjectDefs/Levels/scripts,
  and *zero* of `ui/panel.c`, `ui/objects_tab.c`, `ui/sprites_tab.c`,
  `ui/settings_tab.c`, `ui/project_manager.c`, `ui/shape_pane.c`, or the
  theme system — none of that is meaningful to a player.
- **Runtime main loop**: load project → load the project's designated
  start Level → game loop (input → systems update, including whichever
  camera driver `rules.def` specifies → render world/entities → render
  an **in-game UI layer**, distinct from the editor's immediate-mode
  panels — see below) → on a `LevelTransitionComponent` trigger, swap
  Levels.
- **In-game UI** — a shipped game needs its own HUD/menus/dialogs. The
  editor's `ui/panel.c`-style immediate-mode sidebar is an authoring
  tool's UI, not a player-facing one. This is realistically its own
  design pass once the Runtime's basic loop exists — not detailed here
  beyond naming it as a real, separate requirement.

This is intentionally the least-detailed section in this document. Until
Levels ([§6](#6-the-level--scene-system)) and content consolidation
([§7](#7-content-authoring-tileset--objectdef)) exist, designing the
Runtime in detail would be designing against a moving target. It's named
here so it's never mistaken for "already handled by Play mode" — Play
mode and Runtime solve different problems and one is not a subset of the
other.

---

## 18. Audio

**[PLANNED, unscoped.]** Does not exist anywhere in the codebase today —
no audio subsystem, no sound file handling, nothing. Named explicitly so
it isn't silently forgotten. No design work has been done on this yet;
it will need its own pass once there's a Runtime to attach it to (music/
sfx are primarily a Runtime concern — the editor arguably never needs to
play project audio at all, aside from maybe a preview button in a future
audio-asset tab).

---

## 19. Testing Philosophy

Unit tests (`tests/*.c`) link only the specific `.c` files under test
plus stubs for anything requiring SDL2/GL — this is why, for example,
`test_world_save.c`/`test_world_generator.c`/`test_pathfinder.c` each
define tiny stub implementations of `renderer_bind_texture()` etc. rather
than linking the real renderer. **[BUILT, established pattern]** — every
new system should follow this: link the minimum, stub the rest, no test
should require a display or GL context to run (`make test` runs
headless, by design, per the Makefile's own top comment).

Coverage as of this document: registry, picking, pathfinder, weather,
simulation, construction, spatial grid, world save/load (including the
v3→v4 migration path specifically), world generator (all four
topologies, including seed-determinism and seed-difference checks).

New systems in this document ([§6](#6-the-level--scene-system) Levels,
[§13](#13-win--lose--goals) win/lose, [§17](#17-the-runtime-build)
Runtime) should each land with tests in this same style as part of their
implementation, not as a follow-up.

---

## 20. Full File Map (End State)

Current state plus planned additions, so the intended shape is visible
in one place. `[NEW]` marks files this document introduces that don't
exist yet.

```
src/
  core/        engine.c  log.c  object_def.c  playmode.c  project.c
               time.c  rules.c [NEW]

  ecs/         components.h  registry.c  systems.c

  editor/      editor.c  (play_mode.h retired — see playmode.h instead)

  game/        prefabs.c  [RETIRED in Phase 2]

  renderer/    asset_library.c  atlas.c  camera.c  renderer.c  texture.c

  simulation/  construction.c  harvest.c  simulation.c  weather.c
               resources.c [NEW, replaces fixed ResourceKind]

  scripting/   lua_host.c

  ui/          font.c  font_atlas.c  layout.c  minimap.c
               objects_tab.c  panel.c  project_manager.c
               settings_tab.c  sprites_tab.c  tabbar.c  theme.c
               text.c  textinput.c  ui.c  shape_pane.c
               scripts_tab.c [NEW]  levels_panel.c [NEW]

  world/       spatial_grid.c  tile.h  tileset.h  world.c
               world_shape.h  world_generator.c
               level.c [NEW]

  ai/          agent.c  pathfinder.c

  platform/    input.c  window.c

  math/        mat4.h  vec2.h  vec3.h  math_utils.h

  runtime/     [NEW — entirely new directory]
               runtime_main.c   (Runtime's own entry point, no editor UI)
               game_ui.c        (in-game HUD/menu layer)
               camera_driver.c  (FREE/FOLLOW/BOUNDED/FIXED, §4)

assets/
  font.png          crisp pixel atlas
  sprites.png        starter/example atlas

themes/
  default.theme  blue.theme  purple.theme  amber.theme

<project folder>/
  project.dge
  rules.def         [NEW]
  resources.def     [NEW]
  objects/*.obj
  levels/           [NEW]
    manifest.def
    level_1.dge
    level_2.dge
  scripts/*.lua
```

---

## 21. Production Sequencing

This section is deliberately short — full detail lives in `ROADMAP.md`,
which this document defers to for day-to-day sequencing. Restated here
only to show how the architecture above maps onto an actual order of
work:

```
Phase 1  Tileset-as-data                          DONE
Phase 2  Retire PrefabKind/BuildingKind/ResourceKind
           Part A (Placement)                     DONE
           Part B (Resources)                      next
Phase 3  Level/Scene system (§6)                   depends on Phase 2B
Phase 4  Win/lose/goals (§13)                      depends on Phase 3
Phase 5  Runtime build (§17)                       depends on Phase 3-4
Phase 6  Camera drivers (§4), in-game UI, audio (§18)   depends on Phase 5
Phase 7  ECS scale (dense/sparse), chunked worlds   unscoped, as-needed
```

Phase ordering follows dependency, not difficulty — Levels can't be
meaningfully authored until content (Tileset done, ObjectDef
consolidation next) is fully data-driven, because a Level system built
on the old hardcoded content types would just be multiple copies of the
same fixed content. Win/lose needs Levels to have somewhere to be scoped
to ("win when this Level's condition is met"). Runtime needs Levels and
win/lose both, because a Runtime with no level-transition concept and no
objective isn't meaningfully different from Play mode.

---

## 22. Explicit Non-Goals

Named so scope creep has something to point at and say no to:

- **Not a general-purpose engine.** Isometric-specific projection,
  isometric-specific camera math. Top-down or side-scrolling games are
  not a target; the coordinate-space design in [§3](#3-coordinate-spaces)
  is not meant to generalize to arbitrary projections.
- **Not targeting true 3D.** "Isometric" here means a fixed 2D
  projection of a 2D tile grid, not a 3D scene rendered from an
  isometric-angle camera. No plans to add real 3D meshes, lighting, or a
  Z axis beyond the cosmetic `Tile.height` field already reserved for
  elevation-style shading.
- **Not building a visual scripting system.** Lua is the scripting
  surface; no node-graph editor is planned.
- **Not building multiplayer/networking.** Out of scope entirely; not
  even loosely designed for it. If this ever changes it's a significant
  new document, not an addendum to this one.
- **Not chasing engine-comparison feature parity.** DGEngine isn't
  trying to be a smaller Unity or Godot. Every system in this document
  exists because a real isometric game needs it, not because a
  general-purpose engine would have it.
