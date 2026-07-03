#include "world.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include "../renderer/renderer.h"
#include "../core/log.h"

/* ---------------------------------------------------------------------
   Deterministic value noise.
   Coarse random lattice + bilinear interpolation + smoothstep easing.
   Good enough to produce believable terrain blobs without pulling in
   a noise library. Same seed -> same map, always. */

static unsigned int hash2(int x, int y, unsigned int seed) {
    unsigned int h = (unsigned int)x * 374761393u + (unsigned int)y * 668265263u + seed * 2654435761u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static float lattice_value(int x, int y, unsigned int seed) {
    return (float)(hash2(x, y, seed) & 0xFFFFu) / 65535.0f;
}

static float smoothstep(float t) { return t * t * (3.0f - 2.0f * t); }

static float value_noise(float x, float y, unsigned int seed) {
    int x0 = (int)floorf(x), y0 = (int)floorf(y);
    int x1 = x0 + 1, y1 = y0 + 1;
    float tx = smoothstep(x - (float)x0);
    float ty = smoothstep(y - (float)y0);

    float v00 = lattice_value(x0, y0, seed);
    float v10 = lattice_value(x1, y0, seed);
    float v01 = lattice_value(x0, y1, seed);
    float v11 = lattice_value(x1, y1, seed);

    float a = v00 + (v10 - v00) * tx;
    float b = v01 + (v11 - v01) * tx;
    return a + (b - a) * ty;
}

/* ---------------------------------------------------------------------
   Lifecycle */

bool world_create(World *w, int width, int height) {
    if (width <= 0 || height <= 0) {
        LOG_ERROR("world_create: invalid size %dx%d", width, height);
        return false;
    }
    w->width  = width;
    w->height = height;
    w->tiles  = calloc((size_t)width * (size_t)height, sizeof(Tile));
    if (!w->tiles) {
        LOG_ERROR("world_create: allocation failed for %dx%d tiles", width, height);
        return false;
    }
    /* calloc zeroes to type=0, but 0 is a valid Tileset slot index —
       "undefined" has to be written explicitly as -1, or a freshly
       created World would silently claim every tile is "slot 0" even
       when the project's Tileset doesn't have a slot 0 yet. */
    for (int i = 0; i < width * height; i++) w->tiles[i].type = -1;

    tileset_init(&w->tileset);
    world_shape_init(&w->shape);
    LOG_INFO("World created (%dx%d, %zu tiles)", width, height,
             (size_t)width * (size_t)height);
    return true;
}

void world_destroy(World *w) {
    free(w->tiles);
    w->tiles  = NULL;
    w->width  = 0;
    w->height = 0;
    world_shape_destroy(&w->shape);
}

const Tile *world_get_tile(const World *w, int x, int y) {
    if (x < 0 || y < 0 || x >= w->width || y >= w->height) return NULL;
    return &w->tiles[y * w->width + x];
}

Tile *world_get_tile_mut(World *w, int x, int y) {
    if (x < 0 || y < 0 || x >= w->width || y >= w->height) return NULL;
    return &w->tiles[y * w->width + x];
}

void world_set_tile(World *w, int x, int y, int tileset_slot) {
    Tile *t = world_get_tile_mut(w, x, y);
    if (t) t->type = tileset_slot;
}

bool world_tile_playable(const World *w, int x, int y) {
    if (!world_get_tile(w, x, y)) return false;
    return world_shape_enabled(&w->shape, x, y);
}

bool world_tile_walkable(const World *w, const Tile *t) {
    if (!t) return false;
    return tileset_is_walkable(&w->tileset, t->type);
}

void world_clear(World *w) {
    /* Undefined (-1), not some hardcoded "default terrain" — see
       world.h's doc comment on why this doesn't pick a Tileset slot
       on the project's behalf. */
    for (int i = 0; i < w->width * w->height; i++) {
        w->tiles[i].type    = -1;
        w->tiles[i].variant = 0;
        w->tiles[i].height  = 0.0f;
    }
    LOG_INFO("World cleared (%dx%d)", w->width, w->height);
}

bool world_resize(World *w, int new_w, int new_h) {
    if (new_w <= 0 || new_h <= 0) {
        LOG_ERROR("world_resize: invalid size %dx%d", new_w, new_h);
        return false;
    }
    Tile *new_tiles = calloc((size_t)new_w * (size_t)new_h, sizeof(Tile));
    if (!new_tiles) {
        LOG_ERROR("world_resize: allocation failed for %dx%d", new_w, new_h);
        return false;
    }
    /* New area starts undefined (-1), same reasoning as world_clear(). */
    for (int i = 0; i < new_w * new_h; i++) new_tiles[i].type = -1;

    int copy_w = (new_w < w->width)  ? new_w : w->width;
    int copy_h = (new_h < w->height) ? new_h : w->height;
    for (int y = 0; y < copy_h; y++)
        for (int x = 0; x < copy_w; x++)
            new_tiles[y * new_w + x] = w->tiles[y * w->width + x];
    free(w->tiles);
    w->tiles  = new_tiles;
    int old_w = w->width, old_h = w->height;
    w->width  = new_w;
    w->height = new_h;

    /* Resize the shape mask too, preserving overlap by coordinate (not
       by raw byte — world_shape_resize() alone would misalign bits
       whenever width changes, since bit index = y*width+x). Newly
       exposed tiles default to enabled, matching world_resize()'s
       "new area is usable" behaviour for ordinary tiles above. */
    if (w->shape.active) {
        unsigned char *old_mask = w->shape.mask;
        int old_active_w = w->shape.width, old_active_h = w->shape.height;
        w->shape.mask = NULL;
        w->shape.width = w->shape.height = 0;
        w->shape.active = false;
        if (world_shape_activate(&w->shape, new_w, new_h)) {
            int copy_sw = (new_w < old_active_w) ? new_w : old_active_w;
            int copy_sh = (new_h < old_active_h) ? new_h : old_active_h;
            for (int y = 0; y < copy_sh; y++) {
                for (int x = 0; x < copy_sw; x++) {
                    int old_bit = y * old_active_w + x;
                    bool enabled = (old_mask[old_bit / 8] >> (old_bit % 8)) & 1;
                    world_shape_set(&w->shape, x, y, enabled);
                }
            }
        }
        free(old_mask);
    }

    LOG_INFO("World resized to %dx%d", new_w, new_h);
    (void)old_w; (void)old_h;
    return true;
}

/* ---------------------------------------------------------------------
   Generation

   world_generate() is the "Regenerate" button's noise-terrain fill —
   distinct from world_generator.c's world_topology_generate(), which
   only ever touches WorldShape (the boundary), never Tile.type. This
   function is the other half: given the project's own Tileset, scatter
   its slots across the grid using blobby noise instead of static, so a
   freshly-defined multi-tile project has something to look at instead
   of one flat undefined canvas.

   It used to bucket noise into five hardcoded named bands (water/sand/
   grass/dirt/stone). That's exactly the kind of engine-presumes-meaning
   mistake this whole pass exists to remove — the engine doesn't know
   what any of a project's slots mean, so it can't decide "high noise
   should be stone". Instead it buckets noise proportionally across
   however many slots the project has actually defined: same smooth
   blob shapes as before, but the mapping from noise value to slot
   index is just "which Nth of the range am I in", with no assumption
   about what any given slot represents. */

void world_generate(World *w, unsigned int seed) {
    const float NOISE_SCALE = 1.0f / 6.0f; /* lower = bigger terrain blobs */

    if (w->tileset.count <= 0) {
        LOG_WARN("world_generate: Tileset is empty, nothing to distribute — "
                 "define at least one tile type first");
        return;
    }

    for (int y = 0; y < w->height; y++) {
        for (int x = 0; x < w->width; x++) {
            float n = value_noise((float)x * NOISE_SCALE,
                                   (float)y * NOISE_SCALE, seed);

            int slot = (int)(n * (float)w->tileset.count);
            if (slot >= w->tileset.count) slot = w->tileset.count - 1;
            if (slot < 0) slot = 0;

            Tile *t = &w->tiles[y * w->width + x];
            t->type    = slot;
            t->height  = n;
            t->variant = (unsigned char)(hash2(x, y, seed + 9973u) & 0x3u);
        }
    }
    LOG_INFO("World generated (seed=%u, %d tile types)", seed, w->tileset.count);
}

/* ---------------------------------------------------------------------
   Rendering */

/* Small checker pattern for undefined tiles / tiles whose Tileset slot
   has no sprite assigned yet — the same "this texture is missing"
   signal every real engine uses instead of silently guessing a color.
   Drawn as two overlapping flat-color triangles-worth of the iso
   diamond rather than an actual checker texture, since this needs to
   work with zero image files loaded (a project can be entirely
   undefined and still render something legible, not a crash or a
   solid magenta wall). */
static void draw_missing_tile(int x, int y) {
    renderer_draw_iso_tile(x, y, 0.85f, 0.10f, 0.85f, 1.0f);
}

void world_render(const World *w, const SpriteAtlas *atlas) {
    bool any_textured = false;
    if (atlas) {
        for (int i = 0; i < w->tileset.count; i++)
            if (w->tileset.slots[i].sprite_id >= 0) { any_textured = true; break; }
    }

    if (any_textured) {
        renderer_bind_texture(atlas->texture.id);
        for (int y = 0; y < w->height; y++) {
            for (int x = 0; x < w->width; x++) {
                if (!world_shape_enabled(&w->shape, x, y)) continue;
                const Tile *t = &w->tiles[y * w->width + x];
                int sid = tileset_sprite_for(&w->tileset, t->type);
                if (sid < 0) continue; /* handled in the missing-tile pass below */
                UVRect uv = atlas_get_uv(atlas, sid);
                float tint = 0.94f + (float)t->variant * 0.02f;
                renderer_draw_iso_tile_uv(x, y, tint, tint, tint, 1.0f, uv);
            }
        }
        renderer_flush_texture();
    }

    /* Missing-texture pass: undefined tiles (type == -1), tiles whose
       slot has no sprite, and tiles referencing a since-removed slot
       all land here — one honest "this isn't defined" signal instead
       of three different silent guesses. */
    for (int y = 0; y < w->height; y++) {
        for (int x = 0; x < w->width; x++) {
            if (!world_shape_enabled(&w->shape, x, y)) continue;
            const Tile *t = &w->tiles[y * w->width + x];
            int sid = tileset_sprite_for(&w->tileset, t->type);
            if (sid >= 0) continue; /* already drawn in the textured pass */
            draw_missing_tile(x, y);
        }
    }
}

/* ---------------------------------------------------------------------
   Save / load

   Format (little-endian, no padding — fields written individually so
   struct packing on different compilers can never bite us):

     char     magic[4]   = "DGEW"
     uint32   version    = 4
     uint32   width
     uint32   height
     then width*height tiles, each:
       int32  type       (Tileset slot index, or -1)
       uint8  variant
       float  height
     -- version 2 adds, after the tile data --
     uint8    shape_active   (0 or 1)
     if shape_active:
       uint32 shape_w, shape_h
       then ceil(shape_w*shape_h/8) mask bytes
     -- version 3 added, after the shape section, now RETIRED --
     int32    sprite_map.sprite_id[5]   (old hardcoded 5-terrain array)
     -- version 4 replaces the version-3 section with --
     uint32   tileset.count
     then count times:
       uint8  name_len
       char   name[name_len]     (not NUL-terminated on disk)
       int32  sprite_id
       uint8  walkable (0 or 1)

   Version 1 files have none of the shape section and load with the
   shape left inactive (the old all-enabled rectangular behaviour).
   Version 1/2 files have no tileset data at all — loads as an empty
   Tileset (count 0), same as a freshly created World; every tile's
   old TerrainType byte is discarded rather than reinterpreted, since
   there's no longer a fixed 5-terrain meaning to reinterpret it as.
   Version 3 files get their old 5-slot sprite_map MIGRATED into a
   5-slot Tileset ("Grass"/"Dirt"/"Sand"/"Water"/"Stone", walkable=true
   for everything except the old water slot) so existing projects don't
   lose their sprite assignments across this upgrade — see
   migrate_v3_sprite_map() below. Tile.type values from a v3 file map
   directly onto the migrated Tileset's slot indices unchanged, since
   the old TerrainType enum values (0-4) become this Tileset's slot
   indices (0-4) in exactly the same order.
*/

#define DGEW_MAGIC   "DGEW"
#define DGEW_VERSION 4u

bool world_save(const World *w, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        LOG_ERROR("world_save: could not open '%s' for writing", path);
        return false;
    }

    unsigned int version = DGEW_VERSION;
    unsigned int width   = (unsigned int)w->width;
    unsigned int height  = (unsigned int)w->height;

    bool ok = true;
    ok &= fwrite(DGEW_MAGIC, 1, 4, f) == 4;
    ok &= fwrite(&version, sizeof(version), 1, f) == 1;
    ok &= fwrite(&width,   sizeof(width),   1, f) == 1;
    ok &= fwrite(&height,  sizeof(height),  1, f) == 1;

    for (int i = 0; ok && i < w->width * w->height; i++) {
        int32_t       type    = (int32_t)w->tiles[i].type;
        unsigned char variant = w->tiles[i].variant;
        float         height_f = w->tiles[i].height;
        ok &= fwrite(&type,     sizeof(type),     1, f) == 1;
        ok &= fwrite(&variant,  1, 1, f) == 1;
        ok &= fwrite(&height_f, sizeof(height_f), 1, f) == 1;
    }

    unsigned char shape_active = w->shape.active ? 1 : 0;
    ok &= fwrite(&shape_active, 1, 1, f) == 1;
    if (ok && shape_active) {
        unsigned int sw = (unsigned int)w->shape.width;
        unsigned int sh = (unsigned int)w->shape.height;
        int mask_bytes = (w->shape.width * w->shape.height + 7) / 8;
        ok &= fwrite(&sw, sizeof(sw), 1, f) == 1;
        ok &= fwrite(&sh, sizeof(sh), 1, f) == 1;
        ok &= fwrite(w->shape.mask, 1, (size_t)mask_bytes, f) == (size_t)mask_bytes;
    }

    unsigned int tcount = (unsigned int)w->tileset.count;
    ok &= fwrite(&tcount, sizeof(tcount), 1, f) == 1;
    for (int i = 0; ok && i < w->tileset.count; i++) {
        const TileSlot *s = &w->tileset.slots[i];
        /* Manual bounded length instead of strnlen() — strnlen is a
           POSIX/GNU extension, not standard C11, and pulling in
           _POSIX_C_SOURCE just for one call site isn't worth it when
           the loop is three lines. */
        unsigned char name_len = 0;
        while (name_len < (unsigned char)(TILESET_NAME_MAX - 1) && s->name[name_len]) name_len++;
        int32_t        sprite_id = (int32_t)s->sprite_id;
        unsigned char  walkable  = s->walkable ? 1 : 0;
        ok &= fwrite(&name_len, 1, 1, f) == 1;
        ok &= fwrite(s->name, 1, name_len, f) == name_len;
        ok &= fwrite(&sprite_id, sizeof(sprite_id), 1, f) == 1;
        ok &= fwrite(&walkable, 1, 1, f) == 1;
    }

    fclose(f);
    if (!ok) {
        LOG_ERROR("world_save: write error writing '%s'", path);
        return false;
    }
    LOG_INFO("World saved -> '%s' (%dx%d, %d tile types)", path, w->width, w->height, w->tileset.count);
    return true;
}

/* Migrates a version-3 file's old fixed 5-slot sprite_map (indexed by
   the old TerrainType enum: GRASS=0, DIRT=1, SAND=2, WATER=3, STONE=4)
   into an equivalent 5-slot Tileset, so projects saved before this
   change keep their sprite assignments and their tiles keep meaning
   what they used to mean. New projects created after this change never
   go through this path — they start with an empty Tileset, as
   intended. */
static void migrate_v3_sprite_map(Tileset *ts, const int32_t sprite_id[5]) {
    static const char *names[5]     = { "Grass", "Dirt", "Sand", "Water", "Stone" };
    static const bool  walkable[5]  = { true,    true,   true,   false,  true    };
    tileset_init(ts);
    for (int i = 0; i < 5; i++) {
        int idx = tileset_add_slot(ts, names[i]);
        if (idx < 0) break; /* TILESET_MAX_SLOTS should never be this small, but be safe */
        tileset_set_sprite(ts, idx, (int)sprite_id[i]);
        tileset_set_walkable(ts, idx, walkable[i]);
    }
    LOG_INFO("world_load: migrated version-3 sprite_map into a 5-slot Tileset");
}

bool world_load(World *w, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        LOG_ERROR("world_load: could not open '%s'", path);
        return false;
    }

    char magic[4];
    unsigned int version, width, height;
    bool ok = true;
    ok &= fread(magic, 1, 4, f) == 4;
    ok &= fread(&version, sizeof(version), 1, f) == 1;
    ok &= fread(&width,   sizeof(width),   1, f) == 1;
    ok &= fread(&height,  sizeof(height),  1, f) == 1;

    if (!ok || magic[0] != 'D' || magic[1] != 'G' || magic[2] != 'E' || magic[3] != 'W') {
        LOG_ERROR("world_load: '%s' is not a valid DGEW world file", path);
        fclose(f);
        return false;
    }
    if (version < 1u || version > DGEW_VERSION) {
        LOG_ERROR("world_load: '%s' has unsupported version %u (expected 1-%u)",
                   path, version, DGEW_VERSION);
        fclose(f);
        return false;
    }
    if (width == 0 || height == 0 || width > 100000u || height > 100000u) {
        LOG_ERROR("world_load: '%s' has implausible dimensions %ux%u", path, width, height);
        fclose(f);
        return false;
    }

    World loaded;
    if (!world_create(&loaded, (int)width, (int)height)) {
        fclose(f);
        return false;
    }

    for (int i = 0; ok && i < loaded.width * loaded.height; i++) {
        unsigned char variant;
        float height_f;
        if (version >= 4u) {
            int32_t type;
            ok &= fread(&type, sizeof(type), 1, f) == 1;
            ok &= fread(&variant,  1, 1, f) == 1;
            ok &= fread(&height_f, sizeof(height_f), 1, f) == 1;
            if (ok) loaded.tiles[i].type = (int)type;
        } else {
            /* v1-v3 stored type as a single byte (the old TerrainType
               enum, 0-4). Read it as-is; if this turns out to be a v3
               file, migrate_v3_sprite_map() below re-defines the
               Tileset with exactly 5 slots in the same order, so these
               raw byte values keep meaning the same thing post-load.
               v1/v2 files have no Tileset at all, so their tiles will
               end up referencing undefined slots — rendered as the
               missing-texture checker, which is an honest reflection
               of "this project never defined what its terrain means"
               rather than silently reinstating the old hardcoded
               grass/dirt/sand/water/stone colors. */
            unsigned char type_byte;
            ok &= fread(&type_byte, 1, 1, f) == 1;
            ok &= fread(&variant,  1, 1, f) == 1;
            ok &= fread(&height_f, sizeof(height_f), 1, f) == 1;
            if (ok) loaded.tiles[i].type = (int)type_byte;
        }
        if (ok) {
            loaded.tiles[i].variant = variant;
            loaded.tiles[i].height  = height_f;
        }
    }
    if (!ok) {
        fclose(f);
        LOG_ERROR("world_load: truncated/corrupt tile data in '%s'", path);
        world_destroy(&loaded);
        return false;
    }

    if (version >= 2u) {
        unsigned char shape_active = 0;
        bool shape_ok = fread(&shape_active, 1, 1, f) == 1;
        if (shape_ok && shape_active) {
            unsigned int sw = 0, sh = 0;
            shape_ok &= fread(&sw, sizeof(sw), 1, f) == 1;
            shape_ok &= fread(&sh, sizeof(sh), 1, f) == 1;
            if (shape_ok && sw == (unsigned int)loaded.width && sh == (unsigned int)loaded.height
                && world_shape_activate(&loaded.shape, (int)sw, (int)sh)) {
                int mask_bytes = ((int)sw * (int)sh + 7) / 8;
                shape_ok &= fread(loaded.shape.mask, 1, (size_t)mask_bytes, f) == (size_t)mask_bytes;
            } else {
                shape_ok = false;
            }
            if (!shape_ok)
                LOG_WARN("world_load: '%s' shape section unreadable — ignoring (full rect)", path);
        }
    }

    if (version == 3u) {
        int32_t old_sprite_map[5] = { -1, -1, -1, -1, -1 };
        bool sm_ok = true;
        for (int i = 0; sm_ok && i < 5; i++)
            sm_ok &= fread(&old_sprite_map[i], sizeof(int32_t), 1, f) == 1;
        if (sm_ok) migrate_v3_sprite_map(&loaded.tileset, old_sprite_map);
        else LOG_WARN("world_load: '%s' v3 sprite_map unreadable — Tileset left empty", path);
    } else if (version >= 4u) {
        unsigned int tcount = 0;
        bool ts_ok = fread(&tcount, sizeof(tcount), 1, f) == 1;
        if (ts_ok && tcount <= (unsigned int)TILESET_MAX_SLOTS) {
            for (unsigned int i = 0; ts_ok && i < tcount; i++) {
                unsigned char name_len = 0;
                char name_buf[TILESET_NAME_MAX];
                memset(name_buf, 0, sizeof name_buf);
                int32_t sprite_id = -1;
                unsigned char walkable = 0;
                ts_ok &= fread(&name_len, 1, 1, f) == 1;
                if (ts_ok && name_len >= TILESET_NAME_MAX) ts_ok = false;
                if (ts_ok) ts_ok &= fread(name_buf, 1, name_len, f) == name_len;
                ts_ok &= fread(&sprite_id, sizeof(sprite_id), 1, f) == 1;
                ts_ok &= fread(&walkable,  1, 1, f) == 1;
                if (ts_ok) {
                    int idx = tileset_add_slot(&loaded.tileset, name_buf);
                    if (idx >= 0) {
                        tileset_set_sprite(&loaded.tileset, idx, (int)sprite_id);
                        tileset_set_walkable(&loaded.tileset, idx, walkable != 0);
                    }
                }
            }
        } else {
            ts_ok = false;
        }
        if (!ts_ok)
            LOG_WARN("world_load: '%s' Tileset section unreadable — left empty", path);
    }
    fclose(f);

    world_destroy(w);
    *w = loaded;
    LOG_INFO("World loaded <- '%s' (%dx%d, %d tile types)", path, w->width, w->height, w->tileset.count);
    return true;
}
