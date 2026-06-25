#ifndef DGE_MINIMAP_H
#define DGE_MINIMAP_H

#include "../world/world.h"
#include "../ecs/registry.h"
#include "../renderer/camera.h"

/*  A flat, non-isometric top-down projection of the world —
    deliberately NOT using the iso math the main view does, because the
    point of a minimap is to show the whole canvas's shape at a glance,
    and a 2:1 iso diamond of the same data is harder to read at 160px
    than a plain square grid. Each tile becomes one screen-space quad;
    entities become small dots colored by their own RenderableComponent
    tint (so a worker reads orange here for the same reason it reads
    orange in the main view: it's the same color, just smaller).

    Fixed-size box, bottom-right corner of the viewport. Render between
    renderer_begin_ui() and renderer_end(), same slot as ui_render()/
    panel_render() — order relative to them doesn't matter, they don't
    overlap on screen. */

#define MINIMAP_SIZE 160

void minimap_render(const World *world, Registry *reg, const Camera *cam,
                     int viewport_w, int viewport_h);

#endif /* DGE_MINIMAP_H */
