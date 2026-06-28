#ifndef DGE_SYSTEMS_H
#define DGE_SYSTEMS_H

#include "registry.h"
#include "../renderer/atlas.h"
#include "../renderer/asset_library.h"

/* Draws every entity that has both Transform and Renderable.
   sprite_id resolution order:
     >= ASSET_ID_BASE  -> AssetLibrary (an imported standalone image)
     >= 0 (below that) -> SpriteAtlas (a packed grid sheet cell)
     -1                -> colored-box fallback
   assets may be NULL (e.g. a test harness with no asset library yet) --
   entities with an asset-range sprite_id just fall back to the colored
   box in that case, same degradation as a missing atlas already gets.
   Must be called between renderer_begin/renderer_end, after terrain. */
void system_render_entities(Registry *r, const SpriteAtlas *atlas, const AssetLibrary *assets);

#endif /* DGE_SYSTEMS_H */
