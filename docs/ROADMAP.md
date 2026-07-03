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

Cleaning up enums doesn't get to "ships a real game" on its own. These
don't exist at all yet:

- Win/lose/goal system — a sandbox with no objective isn't a game.
- Multi-layer tiles — one terrain type per cell, no floor/wall/roof,
  no elevation. Most isometric genres need stacked layers.
- Level/scene concept — one World per project, forever.
- Export/build — no path from project to standalone runnable artifact.
- In-game UI layer distinct from the editor's immediate-mode panels.
- Audio, anywhere.

These are named here so they're not silently forgotten, not because all
of them are happening in the next pass.

---

## 4. Sequencing

Ordered around proving the architecture by finishing one real, complete,
shippable game — not around finishing every engine feature first. Later
items will reshape themselves once earlier ones exist, so this list
intentionally doesn't plan past Phase 4 in detail yet.

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

**Part B — Resources (ResourceKind) — not yet started**
- `ResourceKind`'s fixed wood/stone becomes project data: a
  `resources.def` listing whatever resource kinds a project wants (a
  tactics game defines zero; a farming game defines
  `seed`/`crop`/`gold`).
- `ResourceStore` (`simulation/simulation.h`) becomes a dynamic map
  instead of a two-field struct.
- Touches real surface area beyond Part A: `ResourceComponent`,
  `harvest.c`'s wood/stone dispatch, the sim save format
  (`sim.dge`), the HUD's `W:%d S:%d` readout, and the Lua API's
  `dge.get_resource(kind)`/`dge.add_resource(kind, amount)` (currently
  hardcoded to accept only the strings `"wood"`/`"stone"`).
- Explicitly scoped as its own pass rather than bundled into Part A —
  attempting both at once risked finishing neither to the same
  standard as Phase 1.

### Phase 3 — Sidebar cleanup
Now that Phase 1–2 remove the reasons the sidebar got cluttered:

- World-editing sidebar shows exactly: mode switch, the active mode's
  palette/options, world management (new/resize/save/load). Nothing
  about weather, resources, or simulation tuning.
- Weather/resource controls move to Settings, under a section that's
  only shown when the active project's `rules.def` says simulation is
  on — not hardcoded into the World tab regardless of project type.
- Collapse the two competing resize owners (Panel's +/- buttons and
  Settings' grid fields currently do the same `world_resize()`
  independently, with `project.grid_w/h` going stale after either path)
  into one owner.

### Phase 4 — Win/lose/goal system
The smallest addition that turns a sandbox into an actual game.

- `rules.def` gains win/lose condition scripts (Lua, evaluated each
  tick when simulation is on), called once on trigger.
- A simple end-state screen (win/lose) drawn over the Play view.
- This is deliberately small in scope — the point is proving "project
  data can define what 'winning' means" works end to end, not building
  a complete objectives/scoring system.

### Phase 5 — Runtime build (not yet scoped in detail)
A real, even if crude, path from "finished project" to "standalone
runnable thing." Strip editor UI, load one project's data, run it. This
is the actual test of whether Phases 1–4 produced an engine or just a
cleaner editor.

### Phase 6+ — Not yet scoped
Multi-layer tiles, scenes/levels, audio, in-game UI. Each is a real,
separate body of work. Deliberately left unplanned in detail until
Phases 1–5 are done and have reshaped what these actually need to look
like.

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

## 6. Starting point

Phase 1 (Tileset-as-data) starts next, building directly on top of the
existing `TileSpriteMap`/sprite-atlas/save-format work already in the
codebase rather than replacing it — this is the same data those systems
already half-implement, finished properly instead of staying a
five-enum special case.
