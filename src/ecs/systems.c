#include "systems.h"
#include "../renderer/renderer.h"

void system_render_entities(Registry *r) {
    for (Entity e = 0; e < (Entity)MAX_ENTITIES; e++) {
        if (!r->alive[e] || !r->has_transform[e] || !r->has_renderable[e])
            continue;

        TransformComponent  *t = &r->transform[e];
        RenderableComponent  *c = &r->renderable[e];

        renderer_draw_iso_sprite(t->x, t->y, c->w, c->h,
                                  c->r, c->g, c->b, c->a);
    }
}
