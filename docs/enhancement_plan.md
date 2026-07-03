# DGEngine Enhancement Plan

This document outlines the architectural upgrades required to evolve DGEngine from a functional learning project into a robust, scalable engine capable of powering simulation/strategy games (like RimWorld or Factorio).

## ✅ Phase 1: UI Layout Engine (Immediate Mode) — DONE
**The Problem:** The current UI relies on hardcoded pixel math (e.g., `y += 24 + GAP`) across `sprites_tab.c`, `objects_tab.c`, and `panel.c`. This makes adding new features (like sprite functions) extremely tedious and error-prone.
**The Solution:** Build a stateful immediate-mode UI layout primitive (`src/ui/layout.h`).
*   **Features:**
    *   `ui_layout_begin()` to initialize a cursor block.
    *   `ui_layout_button()`, `ui_layout_textinput()`, `ui_layout_label()` which automatically advance the `Y` cursor.
    *   Built-in `hittest` and focus arbitration within the layout functions to remove boilerplate from the tab logic.
*   **Goal:** Refactor `objects_tab.c` and `sprites_tab.c` to use this layout engine, allowing us to easily add the new Sprite Function editing sections.

## ✅ Phase 2: Input Focus Arbitration — DONE
**The Problem:** Keyboard input (like WASD panning) bleeds through when typing in a `TextInput` field, causing the camera to pan while naming a project.
**The Solution:** Create an input consumption layer. If the UI claims focus (e.g., a text field is active), the engine should intercept and consume the keyboard events so they never reach the game world's camera or simulation systems.

## ✅ Phase 3: Texture-Based Font Rendering — DONE
**The Problem:** The current HUD uses a hand-drawn 5x7 bitmap font, drawn pixel-by-pixel as quads. This generates an enormous amount of vertices for basic text and limits us to uppercase only.
**The Solution:** Generate a standard glyph-atlas texture (PNG) and load it via `stb_image`. Change `text_draw()` to use UV-mapped quads (1 quad per character instead of 1 quad per pixel) to drastically improve performance and support lowercase characters.

## Phase 4: Spatial Partitioning (The Spatial Grid)
**The Problem:** Querying entities (like a worker looking for a tree, or the player clicking on an entity) currently requires an `O(N)` linear scan over all `MAX_ENTITIES`.
**The Solution:** Implement a Spatial Hash or Grid overlay. Each `Tile` (or a `Chunk` of tiles) should track a list of `EntityHandle`s currently inside it. This changes lookups from `O(N)` to `O(1)`.

## Phase 5: ECS Memory Layout Refactor
**The Problem:** The current `Registry` uses massive parallel arrays (`bool has_transform[MAX_ENTITIES]; TransformComponent transform[MAX_ENTITIES]`). This causes severe CPU cache misses and wastes memory.
**The Solution:** Implement a Dense/Sparse array pattern. Components should be packed tightly in memory so system loops run blazingly fast, allowing the engine to scale past the 4,096 entity limit to 100,000+ entities.

## Phase 6: The Command Pattern (Editor Decoupling)
**The Problem:** The `editor.c` directly modifies the `World` and `Registry` memory. This prevents features like "Undo" and makes networking impossible.
**The Solution:** Editor interactions should dispatch a `Command` (e.g., `PlaceBlueprintCmd`). The main loop processes these commands sequentially, validating and applying them to the game state.
