#include "systems.h"
#include "../renderer/renderer.h"
#include "../renderer/atlas.h"

void system_animate_entities(Registry *r, float dt) {
    for (Entity e = 0; e < (Entity)MAX_ENTITIES; e++) {
        if (!r->alive[e] || !r->has_renderable[e]) continue;

        RenderableComponent *c = &r->renderable[e];
        if (c->frame_count <= 1) continue;                  /* static sprite */
        if (c->sprite_id < 0 || c->sprite_id >= ASSET_ID_BASE) continue; /* not an atlas strip */
        if (c->frame_fps <= 0.0f) continue;                  /* misconfigured -- don't divide by zero */

        c->frame_timer += dt;
        float frame_len = 1.0f / c->frame_fps;
        while (c->frame_timer >= frame_len) {
            c->frame_timer -= frame_len;
            c->frame_index = (c->frame_index + 1) % c->frame_count;
        }
    }
}

void system_render_entities(Registry *r, const SpriteAtlas *atlas, const AssetLibrary *assets) {
    for (Entity e = 0; e < (Entity)MAX_ENTITIES; e++) {
        if (!r->alive[e] || !r->has_transform[e] || !r->has_renderable[e])
            continue;

        const TransformComponent  *t = &r->transform[e];
        const RenderableComponent *c = &r->renderable[e];

        /* Asset-library sprites never animate (see frame_count comment
           in components.h), so the lookup below intentionally uses the
           raw sprite_id, not the animated one computed further down. */
        const Texture *asset_tex = assets ? asset_library_get_texture(assets, c->sprite_id) : NULL;
        if (asset_tex) {
            /* Imported standalone image -- full 0..1 UV of its own
               texture, no grid involved. */
            UVRect full = { 0.0f, 0.0f, 1.0f, 1.0f };
            renderer_draw_iso_sprite_textured(t->x, t->y, c->w, c->h,
                                              c->r, c->g, c->b, c->a,
                                              asset_tex, full);
        } else if (atlas && c->sprite_id >= 0) {
            int effective_id = (c->frame_count > 1) ? c->sprite_id + c->frame_index : c->sprite_id;
            UVRect uv = atlas_get_uv(atlas, effective_id);
            renderer_draw_iso_sprite_textured(t->x, t->y, c->w, c->h,
                                              c->r, c->g, c->b, c->a,
                                              &atlas->texture, uv);
        } else {
            renderer_draw_iso_sprite(t->x, t->y, c->w, c->h,
                                     c->r, c->g, c->b, c->a);
        }
    }
}
