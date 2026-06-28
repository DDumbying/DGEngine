#include "atlas.h"

#include <string.h>
#include <stdlib.h>
#include "../core/log.h"

/* ---- Programmatic fallback atlas ---------------------------------- */

/*  Each sprite gets a distinct solid color so you can tell types apart
    at a glance even without real art. Colors match the tints that
    renderer_draw_iso_sprite() already used so the visual change is
    "sprites now have a clear shape" not "everything changed color".

    Atlas texture layout: COLS cells wide, rows as needed, each cell
    CELL pixels square. Cell interiors are the sprite color with a 2px
    dark border so grid lines show at the sprite boundary.            */

#define FALLBACK_CELL   32
#define FALLBACK_COLS    4

typedef struct { unsigned char r, g, b, a; } RGBA;

static RGBA sprite_colors[5] = {
    { 60, 160,  60, 255 },   /* SPRITE_TREE               — green      */
    {140, 130, 120, 255 },   /* SPRITE_ROCK               — grey       */
    {230, 150,  50, 255 },   /* SPRITE_WORKER             — orange     */
    { 80,  80, 180, 255 },   /* SPRITE_CAMPFIRE_BLUEPRINT — blue-grey  */
    {250, 180,  60, 255 },   /* SPRITE_CAMPFIRE_COMPLETE  — warm gold  */
};
#define FALLBACK_SPRITE_COUNT 5

static void build_fallback_pixels(unsigned char **out_pixels,
                                   int *out_w, int *out_h) {
    int rows = (FALLBACK_SPRITE_COUNT + FALLBACK_COLS - 1) / FALLBACK_COLS;
    int tex_w = FALLBACK_COLS * FALLBACK_CELL;
    int tex_h = rows * FALLBACK_CELL;
    int total = tex_w * tex_h * 4;
    unsigned char *px = malloc((size_t)total);
    memset(px, 0, (size_t)total);

    for (int i = 0; i < FALLBACK_SPRITE_COUNT; i++) {
        int col = i % FALLBACK_COLS;
        int row = i / FALLBACK_COLS;
        RGBA c  = sprite_colors[i];
        RGBA border = { 20, 20, 20, 255 };

        for (int py = 0; py < FALLBACK_CELL; py++) {
            for (int px2 = 0; px2 < FALLBACK_CELL; px2++) {
                int tx = col * FALLBACK_CELL + px2;
                int ty = row * FALLBACK_CELL + py;
                int idx = (ty * tex_w + tx) * 4;
                bool is_border = px2 < 2 || px2 >= FALLBACK_CELL - 2 ||
                                 py  < 2 || py  >= FALLBACK_CELL - 2;
                RGBA color = is_border ? border : c;
                px[idx+0] = color.r;
                px[idx+1] = color.g;
                px[idx+2] = color.b;
                px[idx+3] = color.a;
            }
        }
    }
    *out_pixels = px;
    *out_w      = tex_w;
    *out_h      = tex_h;
}

static void compute_uv_grid(SpriteAtlas *a, int tex_w, int tex_h) {
    for (int i = 0; i < a->sprite_count; i++) {
        int col = i % a->cols;
        int row = i / a->cols;
        float u0 = (float)(col * a->cell_w)             / (float)tex_w;
        float v0 = (float)(row * a->cell_h)             / (float)tex_h;
        float u1 = (float)(col * a->cell_w + a->cell_w) / (float)tex_w;
        float v1 = (float)(row * a->cell_h + a->cell_h) / (float)tex_h;
        a->sprites[i] = (UVRect){ u0, v0, u1, v1 };
    }
}

bool atlas_create_fallback(SpriteAtlas *a) {
    memset(a, 0, sizeof(*a));
    a->cell_w       = FALLBACK_CELL;
    a->cell_h       = FALLBACK_CELL;
    a->cols         = FALLBACK_COLS;
    a->sprite_count = FALLBACK_SPRITE_COUNT;

    unsigned char *px;
    int tex_w, tex_h;
    build_fallback_pixels(&px, &tex_w, &tex_h);

    bool ok = texture_from_pixels(&a->texture, tex_w, tex_h, px);
    free(px);
    if (!ok) {
        LOG_ERROR("atlas_create_fallback: texture upload failed");
        return false;
    }
    compute_uv_grid(a, tex_w, tex_h);
    LOG_INFO("Fallback sprite atlas created (%d sprites, %dx%d px)",
             a->sprite_count, tex_w, tex_h);
    return true;
}

bool atlas_load(SpriteAtlas *a, const char *path,
                int cell_w, int cell_h, int cols) {
    memset(a, 0, sizeof(*a));
    if (!texture_load(&a->texture, path)) {
        LOG_WARN("atlas_load: '%s' not found, using fallback", path);
        return atlas_create_fallback(a);
    }
    a->cell_w = cell_w;
    a->cell_h = cell_h;
    a->cols   = cols;
    int rows  = a->texture.height / cell_h;
    a->sprite_count = rows * cols;
    if (a->sprite_count > ATLAS_MAX_SPRITES)
        a->sprite_count = ATLAS_MAX_SPRITES;
    compute_uv_grid(a, a->texture.width, a->texture.height);
    LOG_INFO("Sprite atlas loaded: '%s' (%d sprites)", path, a->sprite_count);
    return true;
}

void atlas_destroy(SpriteAtlas *a) {
    texture_destroy(&a->texture);
    memset(a, 0, sizeof(*a));
}

UVRect atlas_get_uv(const SpriteAtlas *a, SpriteId id) {
    if (id < 0 || id >= a->sprite_count)
        return (UVRect){ 0.0f, 0.0f, 1.0f, 1.0f };
    return a->sprites[id];
}

SpriteId sprite_id_for_prefab(PrefabKind k) {
    switch (k) {
        case PREFAB_TREE:   return SPRITE_TREE;
        case PREFAB_ROCK:   return SPRITE_ROCK;
        case PREFAB_WORKER: return SPRITE_WORKER;
        default:            return SPRITE_NONE;
    }
}

SpriteId sprite_id_for_building(BuildingKind k, bool complete) {
    switch (k) {
        case BUILDING_CAMPFIRE:
            return complete ? SPRITE_CAMPFIRE_COMPLETE : SPRITE_CAMPFIRE_BLUEPRINT;
        default:
            return SPRITE_NONE;
    }
}
