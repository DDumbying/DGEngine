# DGEngine

> **DGEngine (Dumbest Game Engine)** — an isometric simulation engine built from scratch in C using SDL2 and OpenGL.

Built because understanding a thing properly means building it yourself.
This is not competing with Unity or Unreal. It is a focused tool for one specific genre: strategy, management, sandbox, and world-simulation games (think RimWorld, Factorio, Banished).

## Philosophy

> Understand everything. Abstract only when necessary.

Every system is written to be readable. No magic. No hidden layers.
If something breaks, you should be able to trace it to a single function.

> i actually want to build something cool, even if most reasources keep making me question my self if i am a dumb by making something like that?, But i mean, this is the main reason i made this org! to make the Dumbest things ever!

## Current State

All 8 phases are complete, plus save-completeness and construction passes after Phase 8. See [`log.md`](./docs/log.md) for the full development log.

The engine features a full A\* pathfinding system, autonomous worker agents, a task system (move, harvest, build), smooth movement interpolation, v4 entity serialization, a dynamic weather system, a worker-built construction system (place a blueprint, assign a worker, watch it build), and an on-screen HUD (bitmap-font text, no texture dependency) showing resources, sim time, weather, editor mode, and a cost preview when placing a blueprint. F5/F9 persist the full session state — world, entities, sim clock, resources, and weather — across four independent save files.

Controls:

| Input | Action |
|-------|--------|
| `WASD` / Arrow keys | Pan camera |
| Scroll wheel | Zoom (anchored to cursor position) |
| `TAB` | Cycle editor mode: PAINT → PLACE → SELECT |
| `1`–`5` *(PAINT mode)* | Pick terrain brush: grass/dirt/sand/water/stone |
| `1`–`3` *(PLACE mode)* | Pick prefab: tree/rock/worker (free, instant) |
| `4` *(PLACE mode)* | Pick the campfire blueprint (costs resources, needs a worker to build) |
| Left-click (drag) *(PAINT)* | Paint brush terrain onto hovered tile |
| Left-click *(PLACE)* | Place current prefab/blueprint on hovered tile |
| Right-click *(PLACE)* | Remove entity on hovered tile (refunds cost if it's an unfinished blueprint) |
| Left-click *(SELECT)* | Select entity on hovered tile, print info to log |
| `Delete` / `Backspace` *(SELECT)* | Delete the selected entity |
| `H` *(SELECT mode)* | Harvest selected entity (credits wood/stone, destroys if depleted) |
| `M` *(SELECT mode)* | Command selected worker to move to / harvest / build the hovered tile |
| `P` | Pause / resume simulation clock (rendering keeps running) |
| `F5` | Save world, entities, sim clock, resources, and weather |
| `F9` | Load world, entities, sim clock, resources, and weather |
| `R` | Regenerate world + entities with a new random seed |
| `ESC` | Quit |

### Known limitations (by design, for now)

- **HUD font is uppercase-only, hand-drawn 5x7 bitmap, no texture atlas.**
  Good enough for resource counts and mode labels; the natural upgrade is
  a real glyph-atlas texture once lowercase or large text volumes make
  per-pixel quads (one `renderer_draw_quad` per lit font pixel) costly
  against the 4096-quad batch budget. Not a bottleneck yet.
- **No Dear ImGui**, despite this engine's earlier README mentioning it for
  the editor phase. Real ImGui is C++; this codebase is plain C11 with zero
  non-vendored dependencies, and pulling in a large C++ UI library would
  mean a mixed-language build and a UI you can't trace end to end —
  directly against this project's stated philosophy. Worth revisiting only
  if hand-rolled UI genuinely becomes the bottleneck.
- **Entity picking is a linear scan** over `MAX_ENTITIES` on each click
  (not per-frame). Fine at current entity counts; revisit with a spatial
  index if Simulation/AI phases push entity counts high enough to matter.


## Building

Dependencies: `gcc`, `SDL2`, `OpenGL`.

```sh
# Install SDL2 on Debian/Ubuntu
sudo apt install libsdl2-dev

# Build
make

# Debug build (with AddressSanitizer + UBSan)
make debug

# Run unit tests (no display required)
make test

# Run
./bin/dgengine
```

GLAD is vendored in `external/glad/`. No other external setup needed.

## Technology

| Category | Choice |
|----------|--------|
| Language | C11 |
| Windowing / Input | SDL2 |
| Rendering | OpenGL 3.3 Core |
| GL loader | GLAD (vendored) |
| Math | Hand-written (vec2, vec3, mat4) |
| Build | GNU Make + gcc |

stb_image will be added once textures land (no fixed phase yet — whenever
the engine actually needs to draw something that isn't a flat-colored
quad). ImGui is *not* planned anymore: Phase 4 (Editor) shipped without
it — see "Known limitations" above for why — and nothing since has made
a strong enough case to reconsider.

## Note

Since I like documenting my thoughts and progress whenever I can, this repository may not always receive constant commits or visible development updates.

For now, this is everything I currently have for the project.
I will be documenting the entire learning and development journey [here](./docs/log.md), Or in the main Org website from [here]( https://ddumbying.vercel.app) including research notes, architecture ideas, rendering concepts, experiments, and everything I study along the way.

So even if the repository appears inactive at times, work and learning may still be happening behind the scenes.

**See other Projects from here**:

- [Dchess - Dumb Chess](https://github.com/DDumbying/Dchess).
- [BTorrent - A BitTorrent Client built for fun (learn)](https://github.com/DDumbying/BTorrent).

