#include "systems.h"
#include "../renderer/renderer.h"
#include "../renderer/atlas.h"

void system_render_entities(Registry *r, const SpriteAtlas *atlas, const AssetLibrary *assets) {
    for (Entity e = 0; e < (Entity)MAX_ENTITIES; e++) {
        if (!r->alive[e] || !r->has_transform[e] || !r->has_renderable[e])
            continue;

        const TransformComponent  *t = &r->transform[e];
        const RenderableComponent *c = &r->renderable[e];

        const Texture *asset_tex = assets ? asset_library_get_texture(assets, c->sprite_id) : NULL;
        if (asset_tex) {
            /* Imported standalone image -- full 0..1 UV of its own
               texture, no grid involved. */
            UVRect full = { 0.0f, 0.0f, 1.0f, 1.0f };
            renderer_draw_iso_sprite_textured(t->x, t->y, c->w, c->h,
                                              c->r, c->g, c->b, c->a,
                                              asset_tex, full);
        } else if (atlas && c->sprite_id >= 0) {
            UVRect uv = atlas_get_uv(atlas, c->sprite_id);
            renderer_draw_iso_sprite_textured(t->x, t->y, c->w, c->h,
                                              c->r, c->g, c->b, c->a,
                                              &atlas->texture, uv);
        } else {
            renderer_draw_iso_sprite(t->x, t->y, c->w, c->h,
                                     c->r, c->g, c->b, c->a);
        }
    }
}
