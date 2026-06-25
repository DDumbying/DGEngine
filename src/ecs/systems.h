#ifndef DGE_SYSTEMS_H
#define DGE_SYSTEMS_H

#include "registry.h"
#include "../renderer/atlas.h"

/* Draws every entity that has both Transform and Renderable.
   Entities with sprite_id >= 0 are drawn textured from the atlas;
   entities with sprite_id == -1 fall back to the colored-box path.
   Must be called between renderer_begin/renderer_end, after terrain. */
void system_render_entities(Registry *r, const SpriteAtlas *atlas);

#endif /* DGE_SYSTEMS_H */
