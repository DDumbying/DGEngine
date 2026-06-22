#ifndef DGE_CAMERA_H
#define DGE_CAMERA_H

#include "../math/mat4.h"
#include "../math/vec2.h"

/*  Orthographic 2-D camera with pan and zoom.
    Designed for the isometric view — the projection is orthographic and the
    caller controls the tile-to-screen transform separately.  */
typedef struct {
    Vec2  position;   /* World-space centre of the view */
    float zoom;       /* 1.0 = default, higher = zoomed in */
    int   viewport_w;
    int   viewport_h;
} Camera;

void camera_init(Camera *c, int viewport_w, int viewport_h);

/* Rebuild the projection * view matrix every frame. */
Mat4 camera_view_projection(const Camera *c);

/* Pan by a world-space delta (pixels / zoom). */
void camera_pan(Camera *c, float dx, float dy);

/* Zoom towards/away from a screen-space point. */
void camera_zoom_at(Camera *c, float factor, float screen_x, float screen_y);

/* Convert screen coordinates to world coordinates. */
Vec2 camera_screen_to_world(const Camera *c, float sx, float sy);

#endif /* DGE_CAMERA_H */
