#include "minimap.h"

#include <math.h>
#include "../renderer/renderer.h"

#define MINIMAP_MARGIN 10.0f

/* Box position helper — bottom-right corner of the viewport, kept in
   one place so the click-free render and any future "click minimap to
   jump camera there" feature compute the same rect. */
static void minimap_box(int viewport_w, int viewport_h, float *out_x, float *out_y) {
    *out_x = (float)viewport_w - MINIMAP_SIZE - MINIMAP_MARGIN;
    *out_y = (float)viewport_h - MINIMAP_SIZE - MINIMAP_MARGIN;
}

void minimap_render(const World *world, Registry *reg, const Camera *cam,
                     int viewport_w, int viewport_h) {
    if (world->width <= 0 || world->height <= 0) return;

    float box_x, box_y;
    minimap_box(viewport_w, viewport_h, &box_x, &box_y);

    /* Backing frame, slightly larger than the tile area so there's a
       visible border even over light terrain (sand/water). */
    renderer_draw_quad(box_x - 2.0f, box_y - 2.0f,
                        (float)MINIMAP_SIZE + 4.0f, (float)MINIMAP_SIZE + 4.0f,
                        0.05f, 0.05f, 0.06f, 0.9f);

    float cell_w = (float)MINIMAP_SIZE / (float)world->width;
    float cell_h = (float)MINIMAP_SIZE / (float)world->height;

    /* One quad per tile — simple and correct, same "good enough until
       it's a bottleneck" call the rest of this engine makes elsewhere.
       Worth revisiting (e.g. bake to a texture, redraw only on terrain
       change) only if canvas sizes grow enough for this to show up in
       a profile; at the current 256x256 resize ceiling that's 65536
       quads, well within what renderer.c's auto-flushing batch (see
       MAX_QUADS in renderer.c) handles, just a few extra flushes. */
    for (int gy = 0; gy < world->height; gy++) {
        for (int gx = 0; gx < world->width; gx++) {
            const Tile *t = world_get_tile(world, gx, gy);
            if (!t) continue;
            TileColor c = tile_base_color(t->type);
            renderer_draw_quad(box_x + (float)gx * cell_w,
                                box_y + (float)gy * cell_h,
                                cell_w, cell_h,
                                c.r, c.g, c.b, 1.0f);
        }
    }

    /* Entity dots — colored by each entity's own RenderableComponent
       tint, so "what color is a worker on the minimap" never needs a
       second answer independent of "what color is a worker in the
       main view". 2x2px floor so dots stay visible at small canvas
       scales without needing their own minimum-size special case. */
    float dot_w = cell_w > 2.0f ? cell_w : 2.0f;
    float dot_h = cell_h > 2.0f ? cell_h : 2.0f;
    for (Entity e = 0; e < (Entity)MAX_ENTITIES; e++) {
        if (!entity_alive(reg, e)) continue;
        const TransformComponent  *t  = entity_get_transform(reg, e);
        const RenderableComponent *rd = entity_get_renderable(reg, e);
        if (!t || !rd) continue;
        float dx = box_x + t->x * cell_w - dot_w * 0.5f;
        float dy = box_y + t->y * cell_h - dot_h * 0.5f;
        renderer_draw_quad(dx, dy, dot_w, dot_h, rd->r, rd->g, rd->b, 1.0f);
    }

    /* Camera frustum outline — the visible world-space viewport's four
       screen corners, inverse-projected through renderer_world_to_grid
       (the iso math the main view actually uses), then mapped onto the
       minimap with the same flat cell_w/cell_h scale as the tiles
       above. Four 1px-thick quads (no line primitive exists here) form
       the rectangle outline. */
    float gx0 = 1e9f, gy0 = 1e9f, gx1 = -1e9f, gy1 = -1e9f;
    float corners_sx[4] = { 0.0f, (float)viewport_w, 0.0f, (float)viewport_w };
    float corners_sy[4] = { 0.0f, 0.0f, (float)viewport_h, (float)viewport_h };
    for (int i = 0; i < 4; i++) {
        Vec2 w = camera_screen_to_world(cam, corners_sx[i], corners_sy[i]);
        float gx, gy;
        renderer_world_to_grid(w.x, w.y, &gx, &gy);
        if (gx < gx0) gx0 = gx;
        if (gx > gx1) gx1 = gx;
        if (gy < gy0) gy0 = gy;
        if (gy > gy1) gy1 = gy;
    }
    /* Clamp to the world bounds so panning far past the edge doesn't
       draw the frustum rectangle outside the minimap box. */
    if (gx0 < 0.0f) gx0 = 0.0f;
    if (gy0 < 0.0f) gy0 = 0.0f;
    if (gx1 > (float)world->width)  gx1 = (float)world->width;
    if (gy1 > (float)world->height) gy1 = (float)world->height;

    float rx = box_x + gx0 * cell_w;
    float ry = box_y + gy0 * cell_h;
    float rw = (gx1 - gx0) * cell_w;
    float rh = (gy1 - gy0) * cell_h;
    if (rw > 0.0f && rh > 0.0f) {
        const float lr = 1.0f, lg = 1.0f, lb = 1.0f, la = 0.9f;
        renderer_draw_quad(rx,            ry,            rw,   1.0f, lr, lg, lb, la);
        renderer_draw_quad(rx,            ry + rh - 1.0f, rw,   1.0f, lr, lg, lb, la);
        renderer_draw_quad(rx,            ry,            1.0f, rh,   lr, lg, lb, la);
        renderer_draw_quad(rx + rw - 1.0f, ry,            1.0f, rh,   lr, lg, lb, la);
    }
}
