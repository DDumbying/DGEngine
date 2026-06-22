#ifndef DGE_SYSTEMS_H
#define DGE_SYSTEMS_H

#include "registry.h"

/* Draws every entity that has both Transform and Renderable.
   Must be called between renderer_begin/renderer_end, after the world
   terrain has been drawn (so entities render on top of the ground). */
void system_render_entities(Registry *r);

#endif /* DGE_SYSTEMS_H */
