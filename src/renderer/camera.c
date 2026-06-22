#include "camera.h"
#include "../math/math_utils.h"

void camera_init(Camera *c, int vw, int vh) {
    c->position   = (Vec2){0.0f, 0.0f};
    c->zoom       = 1.0f;
    c->viewport_w = vw;
    c->viewport_h = vh;
}

Mat4 camera_view_projection(const Camera *c) {
    float half_w = (float)c->viewport_w * 0.5f / c->zoom;
    float half_h = (float)c->viewport_h * 0.5f / c->zoom;

    float left   = c->position.x - half_w;
    float right  = c->position.x + half_w;
    float bottom = c->position.y + half_h;   /* y-down screen space */
    float top    = c->position.y - half_h;

    return mat4_ortho(left, right, bottom, top, -1.0f, 1.0f);
}

void camera_pan(Camera *c, float dx, float dy) {
    c->position.x += dx / c->zoom;
    c->position.y += dy / c->zoom;
}

void camera_zoom_at(Camera *c, float factor,
                    float screen_x, float screen_y) {
    /* Keep the world point under the cursor fixed. */
    Vec2 before = camera_screen_to_world(c, screen_x, screen_y);

    c->zoom = dge_clampf(c->zoom * factor, 0.25f, 8.0f);

    Vec2 after = camera_screen_to_world(c, screen_x, screen_y);
    c->position.x += before.x - after.x;
    c->position.y += before.y - after.y;
}

Vec2 camera_screen_to_world(const Camera *c, float sx, float sy) {
    float half_w = (float)c->viewport_w * 0.5f;
    float half_h = (float)c->viewport_h * 0.5f;
    return (Vec2){
        c->position.x + (sx - half_w) / c->zoom,
        c->position.y + (sy - half_h) / c->zoom
    };
}
