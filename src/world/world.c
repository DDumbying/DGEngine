#include "world.h"

#include <stdlib.h>
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
    LOG_INFO("World created (%dx%d, %zu tiles)", width, height,
             (size_t)width * (size_t)height);
    return true;
}

void world_destroy(World *w) {
    free(w->tiles);
    w->tiles  = NULL;
    w->width  = 0;
    w->height = 0;
}

const Tile *world_get_tile(const World *w, int x, int y) {
    if (x < 0 || y < 0 || x >= w->width || y >= w->height) return NULL;
    return &w->tiles[y * w->width + x];
}

Tile *world_get_tile_mut(World *w, int x, int y) {
    if (x < 0 || y < 0 || x >= w->width || y >= w->height) return NULL;
    return &w->tiles[y * w->width + x];
}

void world_set_tile(World *w, int x, int y, TerrainType type) {
    Tile *t = world_get_tile_mut(w, x, y);
    if (t) t->type = type;
}

/* ---------------------------------------------------------------------
   Generation */

void world_generate(World *w, unsigned int seed) {
    const float NOISE_SCALE = 1.0f / 6.0f; /* lower = bigger terrain blobs */

    for (int y = 0; y < w->height; y++) {
        for (int x = 0; x < w->width; x++) {
            float n = value_noise((float)x * NOISE_SCALE,
                                   (float)y * NOISE_SCALE, seed);

            TerrainType type;
            if      (n < 0.28f) type = TERRAIN_WATER;
            else if (n < 0.38f) type = TERRAIN_SAND;
            else if (n < 0.72f) type = TERRAIN_GRASS;
            else if (n < 0.85f) type = TERRAIN_DIRT;
            else                type = TERRAIN_STONE;

            Tile *t = &w->tiles[y * w->width + x];
            t->type    = type;
            t->height  = n;
            t->variant = (unsigned char)(hash2(x, y, seed + 9973u) & 0x3u);
        }
    }
    LOG_INFO("World generated (seed=%u)", seed);
}

/* ---------------------------------------------------------------------
   Rendering */

void world_render(const World *w) {
    for (int y = 0; y < w->height; y++) {
        for (int x = 0; x < w->width; x++) {
            const Tile *t = &w->tiles[y * w->width + x];
            TileColor c = tile_base_color(t->type);

            /* Per-tile brightness jitter from `variant` (0-3) so a field
               of the same terrain type doesn't render as one dead-flat
               color — this is what was missing before and made the
               ground look wrong. +-6% brightness, water excluded so it
               stays smooth like a real water surface. */
            if (t->type != TERRAIN_WATER) {
                float jitter = ((float)t->variant - 1.5f) * 0.04f;
                c.r += jitter; c.g += jitter; c.b += jitter;
            }

            renderer_draw_iso_tile(x, y, c.r, c.g, c.b, 1.0f);
        }
    }
}

/* ---------------------------------------------------------------------
   Save / load

   Format (little-endian, no padding — fields written individually so
   struct packing on different compilers can never bite us):

     char     magic[4]   = "DGEW"
     uint32   version    = 1
     uint32   width
     uint32   height
     then width*height tiles, each:
       uint8  type
       uint8  variant
       float  height
*/

#define DGEW_MAGIC   "DGEW"
#define DGEW_VERSION 1u

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
        unsigned char type    = (unsigned char)w->tiles[i].type;
        unsigned char variant = w->tiles[i].variant;
        float         height_f = w->tiles[i].height;
        ok &= fwrite(&type,     1, 1, f) == 1;
        ok &= fwrite(&variant,  1, 1, f) == 1;
        ok &= fwrite(&height_f, sizeof(height_f), 1, f) == 1;
    }

    fclose(f);
    if (!ok) {
        LOG_ERROR("world_save: write error writing '%s'", path);
        return false;
    }
    LOG_INFO("World saved -> '%s' (%dx%d)", path, w->width, w->height);
    return true;
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
    if (version != DGEW_VERSION) {
        LOG_ERROR("world_load: '%s' has unsupported version %u (expected %u)",
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
        unsigned char type, variant;
        float height_f;
        ok &= fread(&type,     1, 1, f) == 1;
        ok &= fread(&variant,  1, 1, f) == 1;
        ok &= fread(&height_f, sizeof(height_f), 1, f) == 1;
        if (ok) {
            loaded.tiles[i].type    = (TerrainType)type;
            loaded.tiles[i].variant = variant;
            loaded.tiles[i].height  = height_f;
        }
    }
    fclose(f);

    if (!ok) {
        LOG_ERROR("world_load: truncated/corrupt tile data in '%s'", path);
        world_destroy(&loaded);
        return false;
    }

    world_destroy(w);
    *w = loaded;
    LOG_INFO("World loaded <- '%s' (%dx%d)", path, w->width, w->height);
    return true;
}
