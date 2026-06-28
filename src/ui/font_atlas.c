#include "font_atlas.h"

#include <string.h>
#include <stb_image.h>
#include <glad/glad.h>

#include "../renderer/renderer.h"
#include "../core/log.h"

static unsigned int s_tex_id = 0;
static int          s_tex_w  = 0;
static int          s_tex_h  = 0;
static bool         s_ready  = false;

bool font_atlas_ready(void) { return s_ready; }

bool font_atlas_load(const char *path) {
    int w, h, ch;
    stbi_set_flip_vertically_on_load(1); /* flip so GL v=0=bottom matches PNG row order */
    unsigned char *pixels = stbi_load(path, &w, &h, &ch, 4);
    if (!pixels) {
        LOG_WARN("font_atlas_load: could not load '%s': %s", path, stbi_failure_reason());
        return false;
    }

    /* Upload as a nearest-neighbour texture so pixel art stays sharp. */
    glGenTextures(1, &s_tex_id);
    glBindTexture(GL_TEXTURE_2D, s_tex_id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(pixels);
    s_tex_w = w;
    s_tex_h = h;
    s_ready = true;

    LOG_INFO("font_atlas_load: loaded '%s' (%dx%d)", path, w, h);
    return true;
}

void font_atlas_destroy(void) {
    if (s_tex_id) { glDeleteTextures(1, &s_tex_id); s_tex_id = 0; }
    s_ready = false;
}

/* ---- Glyph UV helpers ---- */

/* Each cell occupies this fraction of the atlas texture. */
static float cell_u(void) { return (float)FONT_ATLAS_CELL_W / (float)s_tex_w; }
static float cell_v(void) { return (float)FONT_ATLAS_CELL_H / (float)s_tex_h; }

/* Compute the UV rect for a given ASCII character.
   stbi flips on load so PNG row 0 (top) -> GL v=1 (top). UVs are straightforward. */
static void glyph_uv(int ch, float *u0, float *v0, float *u1, float *v1) {
    int idx = ch - FONT_ATLAS_FIRST_CH;
    if (idx < 0 || idx > (FONT_ATLAS_LAST_CH - FONT_ATLAS_FIRST_CH)) idx = 0;
    int col = idx % FONT_ATLAS_COLS;
    int row = idx / FONT_ATLAS_COLS;
    float cu = cell_u();
    float cv = cell_v();
    *u0 = (float)col * cu;
    *u1 = *u0 + cu;
    *v0 = 1.0f - (float)row * cv;           /* top of this glyph row */
    *v1 = 1.0f - (float)(row + 1) * cv;     /* bottom of this glyph row */
}

/* ---- Public API ---- */

float font_atlas_line_height(float scale) {
    return (float)FONT_ATLAS_CELL_H * scale;
}

float font_atlas_measure_width(const char *str, float scale) {
    return (float)strlen(str) * (float)FONT_ATLAS_CELL_W * scale;
}

void font_atlas_draw(float x, float y, float scale,
                     float r, float g, float b, float a,
                     const char *str) {
    if (!s_ready || !str || !*str) return;

    float gw = (float)FONT_ATLAS_CELL_W * scale;
    float gh = (float)FONT_ATLAS_CELL_H * scale;

    renderer_bind_texture(s_tex_id);

    float cx = x;
    for (const char *p = str; *p; p++) {
        int c = (unsigned char)*p;
        /* Clamp to printable range; space just advances cursor. */
        if (c >= FONT_ATLAS_FIRST_CH && c <= FONT_ATLAS_LAST_CH) {
            float u0, v0, u1, v1;
            glyph_uv(c, &u0, &v0, &u1, &v1);
            renderer_draw_quad_uv(cx, y, gw, gh, r, g, b, a, u0, v0, u1, v1);
        }
        cx += gw;
    }

    renderer_flush_texture();
}
