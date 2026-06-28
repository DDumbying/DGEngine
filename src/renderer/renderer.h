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

/*  Switches to a fixed screen-space projection (origin top-left, y-down
    pixel coordinates — same convention as input_mouse_pos) for HUD/UI
    drawing, ignoring the world camera's pan and zoom entirely. Flushes
    whatever was already batched under the previous (world) projection
    first, so draw order is preserved. Call after all world-space
    drawing for the frame, before renderer_end(). There's no matching
    "end_ui" — the next renderer_begin() resets things for the next
    frame, and nothing currently needs to switch back mid-frame. */
void renderer_begin_ui(int viewport_w, int viewport_h);

/* Draw a filled quad (world-space position + size). */
void renderer_draw_quad(float x, float y, float w, float h,
                        float r, float g, float b, float a);

/* Draw a UV-textured quad — batched, no flush. Caller must bind the
   texture with renderer_bind_texture() before batching glyphs and
   call renderer_flush_texture() after the last glyph in that batch.
   uv0/uv1 are the top-left / bottom-right UV coordinates [0..1].     */
void renderer_bind_texture(unsigned int tex_id);
void renderer_draw_quad_uv(float x, float y, float w, float h,
                           float r, float g, float b, float a,
                           float u0, float v0, float u1, float v1);
void renderer_flush_texture(void);

/*  Isometric helpers.
    Tile size is set at init time via renderer_set_tile_size().
    Grid coords (gx, gy) → screen coords via standard 2:1 iso projection. */
void renderer_set_tile_size(float tile_w, float tile_h);
void renderer_get_tile_size(float *out_w, float *out_h);
void renderer_draw_iso_tile(int gx, int gy,
                            float r, float g, float b, float a);
void renderer_draw_iso_grid(int cols, int rows);

/*  Converts grid-tile coordinates to the same world-space (cx, cy) that
    renderer_draw_iso_tile/iso_sprite compute internally — the forward
    direction of the same projection editor.c's pick_tile() already
    inverts for mouse picking. Exposed so other modules can position
    their own world-space drawables (e.g. construction.c's progress
    bars, floating above a blueprint's tile) without a third copy of
    this formula. */
void renderer_grid_to_screen(float gx, float gy, float *out_x, float *out_y);

/*  Inverse of the iso projection above: world-space (wx, wy) — the same
    space camera_screen_to_world() returns — back to fractional grid
    coordinates. editor.c's pick_tile() had its own copy of this math
    before minimap.c (Phase D) needed the same inverse to compute which
    grid rect the camera currently sees; exposing it here means there's
    now exactly one place this formula lives, instead of two drifting
    copies. Returns fractional coordinates; round/floor as the caller
    needs (editor.c picks a tile so it rounds-to-nearest, minimap.c
    wants a bounding box so it floors/ceils instead). */
void renderer_world_to_grid(float wx, float wy, float *out_gx, float *out_gy);

/*  Draws a w x h pixel box standing on tile (gx, gy) — same iso
    projection as renderer_draw_iso_tile, but the box's bottom edge sits
    at the tile's screen center and it extends upward, so it reads as
    "an object on the tile" rather than "the tile itself". gx/gy are
    float so entities can sit at fractional grid positions once movement
    interpolation exists (Phase 5). */
void renderer_draw_iso_sprite(float gx, float gy, float w, float h,
                              float r, float g, float b, float a);

/*  Textured variant of renderer_draw_iso_sprite. Flushes the current
    batch, binds the atlas texture, draws the sprite using the given
    UV rect, then restores color-only mode so subsequent draw_quad /
    draw_iso_tile calls are unaffected. The tint color (r,g,b,a) is
    multiplied with the texture sample — pass 1,1,1,1 for no tint.

    Callers get UVRects from atlas_get_uv() (see renderer/atlas.h).  */
#include "atlas.h"
void renderer_draw_iso_sprite_textured(float gx, float gy, float w, float h,
                                        float r, float g, float b, float a,
                                        const Texture *tex, UVRect uv);

void renderer_clear(float r, float g, float b);

#endif /* DGE_RENDERER_H */
