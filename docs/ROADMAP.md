# DGEngine — Engine Redesign Roadmap

This document supersedes `enhancement_plan.md`. That file tracked feature
additions to one specific game built on DGEngine. This file tracks turning
DGEngine into an actual engine — something that can produce more than one
kind of isometric game without editing engine source.

Nothing in this document is implemented yet. This is the plan, written
before any code changes, so the direction is agreed before the work starts.

---

## 1. The core problem

DGEngine today is one specific game (a harvest/build sandbox) with an
editor wrapped tightly around it. The tells are all over the codebase:

- `TerrainType` is a 5-slot C enum (grass/dirt/sand/water/stone), not data.
- `PrefabKind` (tree/rock/worker) and fixed atlas slot indices
  (`SPRITE_TREE=0`, `SPRITE_ROCK=1`, ...) assume every imported spritesheet
  means the same five specific things, in that order.
- `BuildingKind`/`ResourceKind` (campfire, wood/stone) are the same
  category of hardcoding, one layer up.
- Weather/resource controls live in the World-editing sidebar, coupling
  "I am painting a tile" to "I am tuning this specific sandbox game."
- There is no path from "finished project" to "thing a player can run"
  that isn't "give them the whole editor and have them click Play."

None of this is a bug list. It's one root cause: **there's no boundary
between the engine and the one game that was built with it.** Every
redesign pass so far (theme system, shape pane, genre profiles) was
correctly peeling one layer of that off, but each pass kept rediscovering
the same root cause somewhere else, because the root cause itself was
never named and fixed.

---

## 2. The architecture this is moving to

Three layers, conceptually separate even before they're separated into
different folders/binaries:

```
ENGINE   Generic. Ships unmodified across every project. Never contains
         a game-specific word in its source. Owns: ECS, renderer,
         world/tile/shape, spatial grid, save/load, Lua host,
         asset/atlas pipeline, input/window/platform.

PROJECT  Pure data, authored in the editor. Zero C required to define:
           - Tileset      (terrain as sprite + name + walkable flag,
                            not an enum)
           - ObjectDefs    (already correct — becomes the ONLY
                            entity-definition path; PrefabKind/
                            BuildingKind/ResourceKind retire into it)
           - rules.def     (which systems run — simulation on/off,
                            win/lose scripts, turn-based vs realtime.
                            Genre becomes an emergent property of what
                            a project's data asks for, not an enum the
                            engine ships with)
           - scenes/levels (more than one World per project)

RUNTIME  What ships to a player: engine + one project's data, with the
         editor UI stripped out entirely. Doesn't exist yet. This is the
         actual measure of whether DGEngine is an engine or an editor —
         until this exists, nothing is "producing a game," it's
         "improving an editor."
```

The Project Manager hub and the Shape Pane already behave like
correctly-scoped engine pieces today — they don't need to move, the rest
of the codebase needs to catch up to their standard.

---

## 3. What's missing outright (not just hardcoded wrong)

Cleaning up enums doesn't get to "ships a real game" on its own. As of
this document's original writing, none of these existed at all:

- Win/lose/goal system — a sandbox with no objective isn't a game.
  **Still missing** — Phase 5.
- Multi-layer tiles — one terrain type per cell, no floor/wall/roof,
  no elevation. Most isometric genres need stacked layers. **Still
  missing** — unscoped, Phase 7+.
- ~~Level/scene concept — one World per project, forever.~~ **Built,
  Phase 3, Part A** — a project can now contain multiple Levels, each
  its own World with its own dimensions/shape/entities. Part B
  (`LevelTransitionComponent`, walking between levels during actual
  gameplay) still doesn't exist — that needs the Runtime (Phase 6).
- Export/build — no path from project to standalone runnable artifact.
  **Still missing** — Phase 6.
- In-game UI layer distinct from the editor's immediate-mode panels.
  **Still missing** — Phase 7+.
- Audio, anywhere. **Still missing** — Phase 7+.

These are named here so they're not silently forgotten, not because all
of them are happening in the next pass.

---

## 4. Sequencing

Ordered around proving the architecture by finishing one real, complete,
shippable game — not around finishing every engine feature first. Later
items will reshape themselves once earlier ones exist, so this list
intentionally doesn't plan past Phase 5 in detail yet.

### Phase 1 — Tileset-as-data ✅ DONE
Collapse `TerrainType` from an engine enum into project data.

- New `Tileset` asset: ordered list of slots, each with a name (whatever
  the project calls it), a required sprite reference, and a walkable
  flag. No engine-presumed meaning. No "grass" fallback color — an
  unassigned slot renders as an honest missing-texture checker, the same
  way every real engine flags a missing texture instead of guessing.
- `Tile.type` changes meaning: from "one of 5 universal terrain types"
  to "an index into this project's Tileset." Existing `.dge` save format
  gets a version bump and a Tileset table, using the same
  table-in-save-file mechanism the sprite-map already established —
  this is an extension of an existing pattern, not a new one.
- PAINT mode's sidebar palette becomes a generated list of the active
  project's Tileset slots, instead of five fixed swatches.
- New projects start with an **empty** Tileset. You define your first
  terrain slot before you can paint anything. Intentionally honest
  instead of quietly handing every project the same five terrains.

**What actually shipped:**
- `world/tileset.h` — the `Tileset`/`TileSlot` data type, add/rename/
  set-sprite/set-walkable/query API.
- `world/tile.h` — `Tile.type` is now a plain `int` slot index (was
  `TerrainType`). `-1` = undefined, a real, handled state throughout.
- `world/world.c` — save format bumped to v4 (Tileset table replaces
  the old fixed 5-slot `TileSpriteMap`). v3 files migrate automatically
  into an equivalent 5-slot Tileset on load, so existing projects don't
  lose sprite assignments. v1/v2 files load with an empty Tileset (no
  meaning to migrate — rendered as missing-texture, not silently
  reinstated as grass). `world_generate()` (the noise-based "Regenerate"
  button) now distributes proportionally across however many slots the
  project has defined, instead of five hardcoded named bands.
- `world/world_generator.c` — ISLAND and ROOMS topology generators now
  touch `WorldShape` only, never `Tile.type`. An island is a boundary,
  not a color scheme; painting what the land looks like is PAINT mode's
  job, not the generator's.
- `editor/editor.c`/`.h` — `Editor.brush` is a Tileset slot index.
  Number keys 1-9 select slots (bounded by however many the project has
  defined, not a fixed enum count). Defensive clamp added so a project
  switch that shrinks the Tileset can't leave a stale out-of-range
  brush selected.
- `ui/panel.c`/`.h` — PAINT mode's sidebar is now a live, scrollable,
  editable Tileset list: click a row to paint with it, right-click to
  rename, click the swatch to open a sprite picker, "+ ADD TILE" to
  define a new slot. This also fixed the real click-hitbox bug that
  prompted this phase — PLACE mode's sprite-thumbnail grid geometry is
  now computed by one shared `thumb_rect()` helper instead of two
  independently-hand-written formulas in `panel_update`/`panel_render`
  that had drifted 14px apart.
- `ai/pathfinder.c`, `ai/agent.c` — walkability checks go through
  `world_tile_walkable()`, which asks the world's own Tileset instead
  of a hardcoded terrain-type switch.
- `ui/ui.c`, `ui/minimap.c` — HUD brush label and minimap tile coloring
  updated for the new Tileset-based lookups (minimap now shows a
  neutral "defined" gray or the missing-texture magenta, since it has
  no cheap way to sample actual sprite pixel colors per-tile).
- Test suite: `tests/test_pathfinder.c`, `tests/test_world_generator.c`,
  `tests/test_world_save.c` rewritten for Tileset-as-data, including a
  dedicated v3→v4 migration test. All passing, zero new warnings.

### Phase 2 — Retire PrefabKind / BuildingKind / ResourceKind
One content system instead of three parallel ones.

**Part A — Placement (PrefabKind + BuildingKind) ✅ DONE**
- PLACE mode stops having its own hardcoded prefabs. It pulls
  exclusively from `ObjectDefRegistry` — the system that's already
  correctly designed.
- Tree/rock/worker/campfire stop being engine concepts. `PrefabKind`,
  `prefab_spawn()`, `BuildingKind`, and the four `building_*()`
  functions are deleted outright — every placeable/buildable thing is
  now an `ObjectDef` instance, dispatched through
  `objdef_is_buildable()`.
- The fixed `sprite_id_for_prefab()`/`sprite_id_for_building()` atlas
  convenience functions are deleted (they were dead code — never
  called outside `atlas.c` even before retirement). Their removal also
  fixed a real layering violation: `renderer/atlas.h` no longer
  `#include`s `game/prefabs.h`/`simulation/construction.h`, so the
  renderer layer no longer reaches upward into game-layer types.
- PLACE mode's sidebar palette is rebuilt from a raw atlas-sprite
  thumbnail grid (which special-cased four fixed slot indices) into a
  scrollable ObjectDef list — same row-based shape as PAINT mode's
  Tileset palette from Phase 1. This also makes the ObjectDef
  placement code path *reachable* for the first time — it existed in
  `editor.c` since the Objects-tab work landed, but nothing in the UI
  had ever actually set `placing_custom`/`place_def_name` before this.
- **A real latent bug was fixed as a side effect**: the RMB-delete
  refund path used to read a blueprint's cost via
  `building_cost_kind(c->kind)` unconditionally — correct for the one
  hardcoded `BUILDING_CAMPFIRE`, silently wrong for any ObjectDef
  blueprint (whose `kind` field was never set, so a deleted in-progress
  custom blueprint refunded campfire's cost regardless of what it
  actually cost). Now that every blueprint is `def_name`-driven with no
  second code path, the refund always reloads the real definition and
  resolves its actual cost.
- Entity save format bumped to v8 (`ConstructionComponent` drops `kind`
  and `is_custom`, always carries `def_name`). v7 and earlier migrate
  automatically: an old non-custom `BUILDING_CAMPFIRE` construction
  becomes `def_name="Campfire"` on load — same "migrate explicitly,
  never silently reinterpret" discipline as world.c's v3→v4 Tileset
  migration.
- Test suite: `tests/test_construction.c` rewritten around the
  `objdef_*` API with a hand-constructed in-memory "Campfire"
  ObjectDef (no filesystem dependency), covering afford/pay/refund,
  blueprint placement, labor accumulation, and completion — same
  coverage the old `BuildingKind`-based test had, now exercising the
  code path that's actually live.
- **Deliberately not shipped in this pass**: starter-project template
  content (example `.obj` files for Tree/Rock/Worker/Campfire in a new
  project). No project-templating infrastructure exists in the engine
  at all yet — this would be its own feature, not something to bolt on
  here. A fresh project's `ObjectDefRegistry` starts empty, exactly
  matching Phase 1's "a fresh project's Tileset starts empty" precedent.

**Part B — Resources (ResourceKind) ✅ DONE**
- `ResourceKind` (the fixed wood/stone 2-value enum) is retired
  entirely. `ResourceStore` (`simulation/simulation.h`) is now a
  dynamic named list (`ResourceEntry{name, amount}[]`, up to
  `RESOURCE_STORE_MAX_KINDS`) rather than a two-field struct — a
  project can have wood/stone, or gold/mana, or nothing at all, or
  twenty different named materials. The store doesn't declare its
  resource kinds ahead of time anywhere; it just grows to fit whatever
  names harvest/construction/scripts use, the same "starts empty,
  grows from use" pattern established for Tileset/ObjectDefs.
- `ResourceComponent.kind` (`ecs/components.h`) changed from
  `ResourceKind` to a plain `RESOURCE_NAME_MAX`-byte name string — same
  "store the name, resolve on demand" pattern already used by
  `ConstructionComponent.def_name`/`DefinitionComponent.def_name`.
- `objdef_get_build_spec()` (`core/object_def.h`) now outputs a
  resource name string instead of a `ResourceKind` — `object_def.h` no
  longer needs to include `simulation.h` at all as a result, one more
  small layering cleanup alongside Part A's atlas.h fix.
- `game/prefabs.c`'s `objdef_spawn_instance()` "drops" property mapping
  now accepts any resource name (previously hardcoded to only
  recognize the literal strings `"wood"`/`"stone"`).
- `scripting/lua_host.c`'s `dge.get_resource(kind)`/
  `dge.add_resource(kind, amount)` actually got *simpler* — the
  2-branch wood-or-stone dispatch is gone, replaced by a direct call
  into the now name-based `resource_store_get()`/`resource_store_add()`.
  These functions work for any resource name a project uses now,
  unchanged Lua-facing signature (it always just took a string).
- HUD (`ui/ui.c`) no longer shows a fixed `W:%d S:%d` — it iterates
  whatever the project's `ResourceStore` actually holds.
- Entity save format bumped to v9 (`ResourceComponent.kind` as a name
  block instead of a single enum byte); v8 and earlier migrate
  automatically (`0`→`"wood"`, `1`→`"stone"`, the only two values that
  enum ever had).
- Simulation save format (`sim.dge`) bumped to v2 (named resource list
  instead of fixed `int32 wood, int32 stone`); v1 migrates
  automatically into `"wood"`/`"stone"` entries. Same "migrate
  explicitly, never silently reinterpret" discipline as every other
  version bump in this codebase.
- Test suite: `tests/test_simulation.c` rewritten around the named
  `ResourceStore` API (dynamic growth, spend/refund atomicity, capacity
  limits, v1→v2 migration). `tests/test_construction.c` updated for the
  string-based `objdef_get_build_spec()`.
- **Deviation from the original plan, worth calling out explicitly**:
  the original Part B description above called for a `resources.def`
  file declaring a project's resource kinds ahead of time. That turned
  out to be unnecessary — since `ResourceStore` already grows to fit
  whatever names get used (mirroring how a fresh Tileset/ObjectDef
  registry also just starts empty and grows from authoring, not from a
  separate declaration step), a project's resource kinds are already
  fully defined by *usage* (an ObjectDef's `drops` property, a
  blueprint's `build_cost_kind` property, a script's
  `dge.add_resource()` call) with nowhere left for a `resources.def` to
  add value. No such file was built, and none is currently planned.

### Phase 3 — Level/Scene system ✅ DONE (Part A)
The direct answer to "why can't different levels have different map
shapes" — before this, a project WAS one World, exactly one world.dge,
forever. See `ENGINE_DESIGN.md` §6 for the full design reasoning; this
had to wait until Phase 1 (Tileset-as-data) and Phase 2 (ObjectDef
consolidation) landed, since a Level system built on the old
project-wide-fixed content types would have forced every level to share
identical terrain/objects rather than just sharing the same *defined*
palette.

**What actually shipped (Part A — the authoring-time concept, "which
map am I editing right now"):**
- `core/level.h`/`.c` — new `Level`/`LevelRegistry` types. A Level is a
  name plus a `world_path`/`entity_path` pair (its own `.dge` files, in
  exactly the v4/v9 formats `world_save()`/`registry_save()` already
  produce — a Level isn't a new file format, just a named pointer to
  one) plus a default spawn point (`spawn_x`/`spawn_y`/`entry_marker`,
  the latter reserved for Part B, unused so far).
- Every Level in a project shares the same `Tileset` and
  `ObjectDefRegistry` — this fell out for free, since both already live
  independently of any single World/Registry instance rather than
  needing new plumbing.
- `SimClock`/`ResourceStore`/`WeatherSystem` stay project-wide, NOT
  per-level — `sim.dge`/weather save data are unaffected by which Level
  is active. A colony's stockpile or a party's elapsed playtime doesn't
  reset when walking through a door to a different map.
- `levels/manifest.def` — flat key=value, same convention as
  `project.dge`/`.theme` files, listing every Level plus which one is
  active.
- **Legacy migration**: a project saved before Phase 3 (flat
  `world.dge`/`entities.dge` in its root, no `levels/` folder at all)
  gets its files renamed (not copied) into a bootstrapped "Level 1"'s
  paths the first time it's opened post-upgrade — same "migrate
  explicitly, never silently reinterpret" discipline as every other
  version bump in this codebase. A genuinely brand-new project also
  bootstraps a single default "Level 1" — the one deliberate exception
  to this consolidation's "start empty, honest" pattern (Tileset,
  ObjectDefRegistry): there has to be at least one Level for the World
  tab to have anything to point at, the same way `world_create()`
  itself is never optional even when its Tileset is.
- `ui/panel.c` — a level-switcher strip at the top of the World tab's
  sidebar: `[<] [Level Name (i/N)] [>] [+]`. `<`/`>` cycle levels
  (saving the one you're leaving, loading the one you're entering,
  re-fitting the camera and the panel's resize fields to the new
  Level's own dimensions). `+` creates a new Level — starts as a plain
  rectangle at the project's default grid size; shaping/resizing it
  further is a SHAPE-mode/Settings job once it's active, not something
  the add button needs to ask up front.
- `main.c` — `ENTER_EDITOR`'s world/registry loading sequence was
  extracted into a reusable `LOAD_ACTIVE_LEVEL()` macro, used both on
  first project entry and on every level switch, so the two paths can't
  quietly drift apart on what "loading a level" actually means. Fixed
  `WORLD_SAVE_PATH`/`ENTITY_SAVE_PATH` constants are now only used as
  legacy-migration source paths and initial fallback defaults — every
  live save/load call site resolves the active Level's own paths
  (`cur_world_path`/`cur_entity_path`, refreshed via
  `REFRESH_LEVEL_PATHS()` whenever the active Level changes).
- Test suite: `tests/test_level.c` — add/slugging, capacity limits,
  bounds-checked accessors, bootstrap, save/load round-trip, malformed-
  manifest clamping, and legacy-file migration (including the "already
  migrated, second call is a clean no-op" case).

**Deliberately not built in this pass (Part B):**
- `LevelTransitionComponent` — an entity walking through a door to
  change levels *during actual gameplay*. Part A is authoring-time only
  (switching which map you're editing); Part B is the runtime concept
  (a player moving between maps), which needs the Runtime build (Phase
  6 below) to mean much on its own.
- Level rename/delete/duplicate from the UI. The switcher strip can
  cycle and add; renaming or removing a Level isn't wired yet (would
  follow the same inline-rename pattern PAINT mode's Tileset list
  already established in Phase 1, applied here later).
- Per-level camera drive mode (`ENGINE_DESIGN.md` §4's
  `CAMERA_DRIVE_BOUNDED`/`FOLLOW`/`FIXED`) — the editor's camera is
  still exclusively free-pan/zoom; a Level doesn't yet carry or apply
  any camera-behavior data of its own.

### Phase 4 — Sidebar cleanup ✅ DONE
Now that Phase 1–2 remove the reasons the sidebar got cluttered:

**What actually shipped:**
- `main.c` — the real bug here, confirmed by reading the actual code
  rather than assuming the original plan's description was still
  accurate: `project.grid_w`/`project.grid_h` was **never** updated
  after a resize through either UI, and stayed wrong forever (reloading
  doesn't fix it either — `world_load()` only touches the `World`
  struct, never writes back into `Project`). This wasn't just cosmetic
  — Phase 3's `PANEL_ACTION_LEVEL_ADD` sizes a brand new Level from
  `project.grid_w/h`, so a stale value there meant new Levels could
  silently be created at the wrong size after any resize. A second,
  separate bug in the same code: Settings' resize handler called
  `panel_init()` to refresh the panel's pending fields, which also
  unconditionally reset `panel.visible = true` and wiped in-progress
  Tileset rename/scroll state — resizing via Settings while the sidebar
  was hidden would silently force it back open.
- Both bugs are fixed by one new `COMPLETE_WORLD_RESIZE(new_w, new_h)`
  macro that both `PANEL_ACTION_RESIZE` (the sidebar's quick +/-
  buttons) and `settings_tab.wants_resize` (Settings' precise
  text-field entry) now route through — "one owner" in the sense the
  original plan meant: not fewer UI entry points (both quick-nudge and
  precise-value resizing are genuinely useful, kept both), but exactly
  one place that defines what "resized" means for the rest of the
  program's state (`project.grid_w/h`, both UIs' pending fields, the
  camera, the spatial grid) — instead of two independently-diverging
  partial implementations.
- **Deliberately not done — reconsidered, not forgotten**: the original
  plan called for moving Weather controls out of the World tab into a
  Settings section gated by a not-yet-built `rules.def`. On inspection,
  weather is **already correctly gated** by `GenreProfile` (a `main.c`/
  `panel.c` change from before Phase 1) — it only appears in the World
  tab for `GENRE_SANDBOX_SIM` projects, which already satisfies the
  stated goal ("not hardcoded regardless of project type"). Physically
  relocating it to Settings on top of that would trade away *live*
  access to weather while looking at the world it affects, for no
  remaining problem it would actually solve — reconsidered as a UX
  regression rather than a cleanup, and left in the World tab. No
  resource display exists in the World sidebar at all (confirmed by
  reading `panel.c` — it holds a `ResourceStore*` parameter but never
  reads from it), so that half of the original bullet was already true
  before this phase started.

### Phase 5 — Win/lose/goal system
The smallest addition that turns a sandbox into an actual game.

- `rules.def` gains win/lose condition scripts (Lua, evaluated each
  tick when simulation is on), called once on trigger.
- A simple end-state screen (win/lose) drawn over the Play view.
- This is deliberately small in scope — the point is proving "project
  data can define what 'winning' means" works end to end, not building
  a complete objectives/scoring system.

### Phase 6 — Runtime build (not yet scoped in detail)
A real, even if crude, path from "finished project" to "standalone
runnable thing." Strip editor UI, load one project's data, run it. This
is the actual test of whether Phases 1–5 produced an engine or just a
cleaner editor. This is also where Level system Part B
(`LevelTransitionComponent`, a player actually walking between maps)
belongs — a runtime concept needs a runtime to run in.

### Phase 7+ — Not yet scoped
Multi-layer tiles, audio, in-game UI, per-level camera drive modes.
Each is a real, separate body of work. Deliberately left unplanned in
detail until Phases 1–6 are done and have reshaped what these actually
need to look like.

---

## 5. What stays untouched

Named explicitly so it's clear what's *not* being rebuilt:

- Project Manager hub (Recent / New / Open, topology + genre chosen at
  creation) — already correctly scoped, structurally sound.
- Shape Pane / `WorldShape` — pure boundary/topology editing, zero
  sprite or terrain awareness, already correctly isolated.
- ECS core (Registry, component arrays, systems dispatch).
- Spatial grid, A* pathfinder, renderer/atlas/camera, Lua host plumbing.
- Theme system (`ui/theme.c`, file-based palettes).

---

## 6. Current status

Phases 1–4 are complete (Tileset-as-data; PrefabKind/BuildingKind/
ResourceKind retired into ObjectDef; the Level/Scene system's Part A;
Sidebar cleanup — the resize state consolidation). Phase 5 (Win/lose/
goal system) is next — the smallest addition that turns a sandbox into
an actual game.
