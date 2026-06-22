#ifndef DGE_RENDERER_H
#define DGE_RENDERER_H

#include <stdbool.h>
#include "camera.h"
#include "../math/vec2.h"

/*  Immediate-mode 2-D renderer.
    Every frame: renderer_begin -> draw calls -> renderer_end.
    All coordinates are in world space; the camera handles projection.  */

bool renderer_init(void);
void renderer_shutdown(void);

void renderer_begin(const Camera *c);
void renderer_end(void);

/* Draw a filled quad (world-space position + size). */
void renderer_draw_quad(float x, float y, float w, float h,
                        float r, float g, float b, float a);

/*  Isometric helpers.
    Tile size is set at init time via renderer_set_tile_size().
    Grid coords (gx, gy) → screen coords via standard 2:1 iso projection. */
void renderer_set_tile_size(float tile_w, float tile_h);
void renderer_get_tile_size(float *out_w, float *out_h);
void renderer_draw_iso_tile(int gx, int gy,
                            float r, float g, float b, float a);
void renderer_draw_iso_grid(int cols, int rows);

/*  Draws a w x h pixel box standing on tile (gx, gy) — same iso
    projection as renderer_draw_iso_tile, but the box's bottom edge sits
    at the tile's screen center and it extends upward, so it reads as
    "an object on the tile" rather than "the tile itself". gx/gy are
    float so entities can sit at fractional grid positions once movement
    interpolation exists (Phase 5). */
void renderer_draw_iso_sprite(float gx, float gy, float w, float h,
                              float r, float g, float b, float a);

void renderer_clear(float r, float g, float b);

#endif /* DGE_RENDERER_H */
