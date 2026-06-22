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

(Reordered from the original plan: Editor was Phase 6, moved up ahead of Simulation/AI so there's a way to hand-author test scenarios — paint terrain, place entities — instead of relying only on the procedural demo spawner while those systems are built.)
