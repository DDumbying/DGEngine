#ifndef DGE_SYSTEMS_H
#define DGE_SYSTEMS_H

#include "registry.h"
#include "../renderer/atlas.h"
#include "../renderer/asset_library.h"

/* Advances frame_timer/frame_index for every entity with an animated
   Renderable (frame_count > 1) — see the RenderableComponent comment
   in components.h for the frame_count/frame_fps contract. Call once
   per frame before system_render_entities(), with the same dt as
   everything else driven by dge_time_dt(). Entities using an
   AssetLibrary sprite_id (>= ASSET_ID_BASE) are skipped: those are
   whole standalone images, not a strip of atlas cells, so there's
   nothing to step through. */
void system_animate_entities(Registry *r, float dt);

/* Draws every entity that has both Transform and Renderable.
   sprite_id resolution order:
     >= ASSET_ID_BASE  -> AssetLibrary (an imported standalone image)
     >= 0 (below that) -> SpriteAtlas (a packed grid sheet cell)
     -1                -> colored-box fallback
   assets may be NULL (e.g. a test harness with no asset library yet) --
   entities with an asset-range sprite_id just fall back to the colored
   box in that case, same degradation as a missing atlas already gets.
   Animated entities (see system_animate_entities()) draw sprite_id +
   frame_index instead of sprite_id directly.
   Must be called between renderer_begin/renderer_end, after terrain. */
void system_render_entities(Registry *r, const SpriteAtlas *atlas, const AssetLibrary *assets);

#endif /* DGE_SYSTEMS_H */
