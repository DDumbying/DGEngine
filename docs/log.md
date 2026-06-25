# Development Log

## Phase 1 — Foundation ✅

Window, input, timing, camera, isometric grid renderer.

- Logging (`LOG_INFO`, `LOG_WARN`, `LOG_ERROR`)
- Frame timing with delta-time (capped to prevent spiral-of-death)
- Keyboard and mouse input (pressed / held / released, scroll wheel)
- Orthographic 2D camera with pan and zoom-to-cursor
- Batch renderer — up to 4096 quads per draw call
- Isometric tile projection (standard 2:1 diamond grid)

---

## Phase 2 — World System ✅

Tilemap, terrain generation, save/load.

- Tilemap world (`World`) with 5 terrain types (grass, dirt, sand, water, stone)
- Deterministic value-noise terrain generation (same seed → same map)
- Per-tile shade variation so flat terrain doesn't render as one dead color
- Binary world save/load (`world.dge`)

---

## Phase 3 — ECS ✅

Entity manager, component storage, render system.

- Entity Component System (`Registry`): fixed-slot component arrays for Transform / Renderable / Health, no inheritance, no generics
- `EntityHandle` (index + generation counter) for references that need to survive across frames — the editor's selection is the first thing that needs this
- Demo entity spawner scatters trees (grass) and rocks (stone) across the generated world, sharing prefab definitions with the editor (`game/prefabs.c`)

---

## Phase 4 — Editor ✅

Terrain painting, object placement, entity selection/inspector, entity save/load.

- **Editor** (`editor/editor.c`): mouse-driven terrain painting, prefab placement/removal, entity selection with console-printed inspector output, mode-switching via TAB
- Binary entity save/load (`entities.dge`), independent of the world file
- A small standalone test suite (`make test`) covering the new generation-counter/handle logic, the save/load round trip, and the picking math — no display required

---

## Phase 5 — Simulation ✅

SimClock (game-time, pause/resume), ResourceStore (wood/stone), ResourceComponent, harvest system, entity depletion/destruction.

- `SimClock` — in-world time counter, fully decoupled from wall-clock delta-time; supports pause/resume and configurable speed multiplier
- `ResourceStore` — player-held resources (wood, stone); logged on every change; safe foundation for future UI or scripting layers
- `ResourceComponent` — marks entities as harvestable, specifying kind (wood/stone) and yield per hit; stored in ECS and persisted in save files
- Harvest system (`system_harvest_entity`) — one-shot action that reduces entity health by `yield_per_hit`, credits proportional resources, and automatically destroys and cleans up fully-depleted entities
- Trees now yield **wood**, rocks yield **stone** — both with `HealthComponent` so they can be damaged/depleted across multiple harvest actions
- Entity save format bumped to **version 2** to include `ResourceComponent`
- Inspector output (SELECT mode) now shows the `Resource` component and reminds you that H harvests the selected entity

---

## Phase 6 — AI ✅

Pathfinding, task system, autonomous agents.

- **A\* Pathfinder** — 4-directional pathfinding on grid terrain, navigating around obstacles (water)
- **Task System** — autonomous tasks for entities: `TASK_MOVE_TO` (walks to grid destination) and `TASK_HARVEST` (moves adjacent to resource and harvests it over time)
- **Smooth Interpolated Movement** — `MoveComponent` handles tile-to-tile movement progression and interpolates transform positions for smooth render frames
- **Worker Prefab** — `PREFAB_WORKER` spawned in PLACE mode with key 3, with built-in pathfinding, task execution, and rendering (distinct warm orange/red)
- **SELECT Mode Commands** — key M to issue move/harvest commands to selected workers on the hovered tile
- **Entity Save Version 3** — extended save format to persist entity `MoveComponent` and `TaskComponent` components (with backward compatibility)

---

## Phase 7 — Advanced ✅

Procedural generation, weather, modding support, performance.

- **Dynamic Weather System** — switches between `SUNNY`, `RAIN`, and `SNOW` over game-time periods
- **Weather Speed Multipliers** — rain slows workers down by 30%, and snow slows them down by 50% (Sunny runs at full speed)
- **Smooth Real-time Particles** — falling rain lines and drifting/swaying snowflakes render in world-space on top of the world, dynamically wrapping around the camera view
- **Procedural Initial Workers** — spawns 3 starting workers at valid walkable positions on startup

---

## Phase 8 — On-Screen UI ✅

Bitmap-font text rendering and a real HUD — the gap flagged twice in
Phase 4/6 comments ("no on-screen UI yet, no text renderer exists").

- **Bitmap font** (`ui/font.c`) — hand-drawn 5x7 glyphs (digits, A-Z,
  space, and basic punctuation) stored as plain '#'/'.' strings, no font
  file or texture atlas. Deliberately the simplest thing that works;
  the natural upgrade is a real glyph-atlas texture once lowercase or
  large amounts of text make per-pixel quads costly.
- **Text rendering** (`ui/text.c`) — turns a string into `renderer_draw_quad`
  calls via the font table. No new renderer machinery needed for this part.
- **Screen-space rendering** (`renderer_begin_ui`) — switches the batch's
  view-projection to a fixed pixel-space ortho (origin top-left, y-down,
  same convention as mouse coordinates) so HUD elements don't pan/zoom
  with the world camera. Flushes pending world-space quads first so draw
  order across the switch is correct.
- **HUD composition** (`ui/ui.c`) — resource counts, sim clock (with a
  visible PAUSED state), weather, current editor mode/brush/prefab,
  selected-entity indicator (using the real generation-checked
  `entity_handle_valid`, not just a non-null index), and a control-hint
  strip along the bottom.
- `editor_mode_name()` / `terrain_name()` promoted from `static` helpers
  inside `editor.c` to public API so the console log and the HUD read
  from the same source of truth instead of duplicating name tables.
- Fixed a latent const-correctness bug in `main.c`'s worker-spawn
  occupancy check (`Tile *` vs `world_get_tile`'s `const Tile *` return)
  surfaced by the rebuild — was a compiler warning, not a crash, but
  worth closing while touching this file.

---

## Post-Phase 8 — Save Completeness ✅

Closed the gap flagged in Phase 8's own writeup and the README's "Known
limitations": F5/F9 only ever persisted `world.dge` and `entities.dge` —
elapsed sim-time, wood/stone, and weather all silently reset on load.

- **`simulation_save`/`simulation_load`** (`simulation.c`) — persists
  `SimClock` (elapsed, speed, saved_speed) and `ResourceStore` (wood,
  stone) together in one `sim.dge` file, since they're declared in the
  same header and form one "session state" blob. Both `speed` and
  `saved_speed` are saved, not just `elapsed` — saving mid-pause and
  reloading now stays paused, and resuming afterward restores the
  actual pre-pause speed instead of snapping to 1x.
- **`weather_save`/`weather_load`** (`weather.c`) — persists only the
  authoritative state (`type`, `timer`, `duration`) to `weather.dge`.
  The particle array is ephemeral visual state, not gameplay state —
  same treatment registry.c already gives `MoveComponent`'s lerp
  progress (reset, not saved). `initialized_particles` comes back
  `false` so particles regenerate fresh on the next frame.
- Four independent save files now (`world.dge`, `entities.dge`,
  `sim.dge`, `weather.dge`) — same "each subsystem owns its own
  serialization" pattern as the rest of the engine, rather than
  threading `ResourceStore`/`WeatherSystem` through `registry_save`
  and creating a dependency from ecs/ (core) on simulation/ (gameplay).
- New tests: `tests/test_simulation.c` (round trip, including the
  paused/`saved_speed` case, and corrupt-file rejection) and
  `weather_save`/`load` assertions added to the existing
  `tests/test_weather.c`.
- Verified end-to-end on the actual binary (not just unit tests): F5
  under a real X session produces correctly-sized files with the
  expected log lines; F9 loads them back with matching values; no
  crashes either direction.
- Drive-by fixes while touching `main.c`: window title and boot log
  were still saying "Phase 7" despite Phase 8 (UI) being the most
  recently completed milestone; boot log now also states that F5/F9
  cover the full session state, not just world/entities.

---

## Post-Phase 8 — Construction ✅

The other half of the original Phase 5 plan, deferred at the time in favor
of landing a tested slice (interactions/resources/time) rather than four
systems at once. One buildable thing — `BUILDING_CAMPFIRE` — establishes
the full pattern: cost, placement, worker-built progress, completion.

- **`ecs/components.h`** — `ConstructionComponent` (kind, build_time_total,
  build_time_done, complete) and `BuildingKind`. `TaskKind` gained
  `TASK_BUILD`. Construction state lives on the same entity throughout —
  a blueprint becomes the finished building in place, never destroyed
  and replaced.
- **`ecs/registry.c`** — storage + accessors for the new component;
  entity save format bumped to v4 (additive mask bit, same backward-
  compatible scheme as v1→v2→v3 — old saves still load, they just never
  set the new bit).
- **`simulation/construction.c`** (new) — the building catalog
  (`building_name`/`cost_kind`/`cost_amount`/`build_time`, all switches
  over `BuildingKind` so a second building extends them, not a
  redesign), `construction_place_blueprint()`, `system_build_entity()`
  (continuous per-frame labor — no discrete "hit" to gate on a timer
  the way harvesting has one per chop), and `construction_render()`
  (world-space progress bars over incomplete blueprints).
- **`simulation/simulation.c`** — `ResourceStore` gained
  `has_wood`/`has_stone` and `try_spend_wood`/`try_spend_stone`,
  matching the existing per-type convention rather than introducing a
  `ResourceKind`-generic API that would couple `simulation.h` to `ecs/`.
  `try_spend_*` rejects outright on insufficient funds — no partial
  spend — unlike `add_wood`/`add_stone`'s unconditional clamp-at-0.
- **`renderer.c`** — added `renderer_grid_to_screen()`, exposing the
  forward direction of the iso projection (editor.c's picking already
  used the inverse) so construction.c's progress bars can position
  themselves without a third copy of that formula.
- **`editor.c`** — `editor_update()` now takes a `ResourceStore*`
  (a deliberate, explained exception to "editor.c doesn't touch
  ResourceStore" — see the comment on the signature). PLACE mode's `4`
  key selects the campfire blueprint instead of a free prefab; LMB pays
  cost and spawns it (refunding automatically if the registry is full
  or the player cancels via RMB before it's finished); SELECT mode's
  `M` command gained a third branch — hovering an incomplete blueprint
  assigns `TASK_BUILD` instead of move/harvest.
- **`ai/agent.c`** — `TASK_BUILD` handling mirrors `TASK_HARVEST`'s
  adjacency-check structure, but accumulates labor continuously instead
  of gating on a 1-second tick (no natural "hit" to discretize). Stays
  silent on completion, same as HARVEST's silent transition — the
  underlying system function already logs it once.
- **`ui.c`** — cost-preview line on the MODE row when a blueprint is
  selected in PLACE mode, colored red when unaffordable.
- New `tests/test_construction.c`: catalog accessors, afford/pay gating
  (including the no-partial-spend guarantee), blueprint component
  correctness, and the labor-accumulation/completion contract
  (returns true exactly once, clamps at total, safe no-op after
  completion and on invalid entities). All 7 test binaries pass clean
  under ASan/UBSan.
- Verified live on the actual binary: boots cleanly, keyboard-driven
  mode/blueprint selection confirmed via log output and a screenshot
  showing the cost-preview HUD line correctly colored red while
  unaffordable. Click-driven placement/worker-assignment could not be
  confirmed via this session's synthetic-input harness — mouse clicks
  never reliably registered through Xvfb+xdotool for *any* feature
  tested this session, including pre-existing ones, so this reads as
  an environment limitation rather than a code issue. Worth a quick
  manual playtest to be sure.

---

## Phase H — Text Input Widget ✅

First real text-entry primitive in the engine — everything from here on
(project names in Phase I, object/sprite names, file paths, property
values) types into a `TextInput` instead of each screen reinventing its
own key handling. Two real gaps had to close before the widget could
exist at all — and neither was where the original plan expected: `panel.c`'s
resize fields turned out to be `+`/`-` steppers, not typed fields, so
there was nothing there to replace, just a foundation to add underneath.

- **`platform/input.h/c`** — `SDL_TEXTINPUT` event handling, previously
  absent entirely (only scancode KEYDOWN/KEYUP was tracked — physical
  key identity, not the actual character a keyboard layout/IME
  produces). Added `input_text_input_activate()`/`_deactivate()` (thin
  wrapper over SDL's own global text-input mode) and
  `input_text_typed()`, returning whatever UTF-8 bytes arrived this
  frame. Concatenates across multiple `SDL_TEXTINPUT` events landing in
  the same frame instead of overwriting, so fast typing can't silently
  drop characters.
- **`ui/font.c`** — added a glyph for `_`, drawn on the bottom row so it
  reads as distinct from `-` (middle row) at a glance. Every naming
  convention in the roadmap (`tree_trunk`, `goblin_idle`, ...) depends
  on it, and it had no glyph at all before this — it would've rendered
  as a silent gap, indistinguishable from a typo.
- **`ui/textinput.h/c`** (new) — `TextInput` struct +
  `init`/`set`/`get`/`focus`/`unfocus`/`update`/`render`, the same
  two-call-per-frame shape as `panel_update()`/`panel_render()`.
  Character insertion, Backspace/Delete/Left/Right (hold-to-repeat past
  an initial delay), Home/End, click-to-reposition-cursor, numeric-only
  mode, a max-length cap, and a caret that blinks relative to
  focus-time (always visible the instant a field gains focus, never
  mid-fade of whatever phase happened to be active). Click-to-cursor
  and caret positioning use `text_measure_width("M", scale)` rather
  than reaching into `text.c`'s private per-char advance constant — one
  character's width *is* the fixed monospace advance in this font, no
  need to duplicate the number.
- Insertion rejects any character with no glyph
  (`font_get_glyph()` returns `NULL`) instead of silently accepting it
  and rendering an invisible gap. Lowercase letters pass this check
  (the font upper-cases for lookup) and are stored case-preserved in
  the buffer even though `text.c` currently displays everything
  uppercased — `text.c`'s display limitation isn't a reason to lose the
  real case from data that might end up as a filename later.
- No `main.c` changes — there's no screen to host a field in yet;
  Phase I's Project Manager is the first real consumer.
- Verified with a 35-assertion in-process test that injects synthetic
  SDL events straight into `input_process_event()`: insertion, splicing
  mid-buffer, backspace/delete-forward at both boundaries, Home/End,
  numeric-only filtering inline with valid digits, max-length clamping
  mid-insert, hold-to-repeat timing across real elapsed time,
  click-to-cursor math, focus/unfocus, `textinput_set()` clamping, and
  two `SDL_TEXTINPUT` events landing in one frame. All pass clean under
  ASan/UBSan. Not added as a `tests/` target, though — like
  `panel.c`/`ui.c`/`minimap.c`/`editor.c`, this is a screen-facing
  `src/ui/` module the project doesn't headlessly unit-test; verified
  the same way those are instead. Also confirmed visually on the live
  binary under Xvfb: typed text renders correctly (uppercased, as
  expected), the new `_` glyph is clearly distinct from `-`, and the
  caret draws in the right place.
- One thing to watch before Phase I wires a real field into a live
  screen: nothing currently arbitrates focus between a `TextInput` and
  the world editor's own scancode shortcuts (WASD pan, etc.).
  `textinput_update()` only acts while explicitly focused so this is
  harmless today, but whichever screen hosts the first real field
  should make sure camera-pan code doesn't also run while that field
  has focus.

---

## Roadmap

| Phase | Status | Description |
|-------|--------|-------------|
| 1 — Foundation | ✅ Done | Window, input, timing, camera, isometric grid renderer |
| 2 — World System | ✅ Done | Tilemap, terrain generation, save/load |
| 3 — ECS | ✅ Done | Entity manager, component storage, render system |
| 4 — Editor | ✅ Done | Terrain painting, object placement, entity selection/inspector, entity save/load |
| 5 — Simulation | ✅ Done | SimClock (game-time, pause/resume), ResourceStore (wood/stone), ResourceComponent, harvest system, entity depletion/destruction |
| 6 — AI | ✅ Done | Pathfinding, task system, autonomous agents |
| 7 — Advanced | ✅ Done | Procedural generation, weather, modding support, performance |
| 8 — On-Screen UI | ✅ Done | Bitmap font, text rendering, screen-space HUD |
| Post-8 — Save Completeness | ✅ Done | SimClock/ResourceStore/WeatherSystem now persist across F5/F9 |
| Post-8 — Construction | ✅ Done | Worker-built campfire: cost, blueprint placement, progress, completion |
| A+B — Authoring Canvas | ✅ Done | Blank 64×64 canvas, middle-mouse drag pan, zoom 0.05x–8x, tile coord HUD |
| C — Editor Panel | ✅ Done | Right-side editor panel: mode buttons, terrain brushes, prefab list, world actions |
| D — Minimap | ✅ Done | Flat top-down terrain colors, entity dots, camera frustum |
| E — Sprite Atlas | ✅ Done | stb_image, SpriteAtlas, textured entity rendering, assets/sprites.png spec |
| H — Text Input Widget | ✅ Done | `TextInput`: insertion, cursor movement, backspace/delete with hold-repeat, numeric-only mode, max-length, click-to-position, blinking caret |
| I — Project Manager   | ✅ Done | Launcher screen: recent list, New Project form, Open Project form, `project.dge` I/O, `~/.dgengine` MRU list |
| J — Tab Workspace     | ✅ Done | Top tab bar: World / Objects / Sprites / Scripts / Settings; `TabBar` drives `active_tab`; each tab's update/render is gated by it |
| K — Sprites Tab       | ✅ Done | Scrollable atlas grid, click-to-select cell, `TextInput` name field, `sprites.meta` load/save (`index=name` format) |
| L — Object Editor     | ✅ Done | `ObjectDef` / `ObjectDefRegistry`, `.obj` file format, Objects tab: list + inspector with name/sprite/property/behavior fields, Save/Delete |
| O — Settings Tab      | ✅ Done | Project name rename, grid resize (Apply Grid), tile pixel dimensions (Apply Tile), Save Project — feeds back into world/renderer via `wants_resize` flags |

(Reordered from the original plan: Editor was Phase 6, moved up ahead of
Simulation/AI so there's a way to hand-author test scenarios — paint
terrain, place entities — instead of relying only on the procedural demo
spawner while those systems are built. UI was added as Phase 8, outside
the original 7-phase plan, once the lack of on-screen feedback became
the most-flagged gap across multiple phases' own code comments.)
