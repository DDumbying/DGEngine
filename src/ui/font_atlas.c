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

    /* No flip: PNG row 0 (top of image) → stored first in memory.
       GL reads memory bottom-up so PNG row 0 → GL v=0 (bottom).
       We handle the coordinate mapping in glyph_uv().             */
    stbi_set_flip_vertically_on_load(0);
    unsigned char *pixels = stbi_load(path, &w, &h, &ch, 4);
    if (!pixels) {
        LOG_WARN("font_atlas_load: could not load '%s': %s", path, stbi_failure_reason());
        return false;
    }

    if (s_tex_id) glDeleteTextures(1, &s_tex_id);
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

/* ---- UV helper ---- *
 *
 * stbi loaded without flip → PNG row 0 (top of image) is at memory offset 0.
 * OpenGL reads memory bottom-up, so:
 *   memory offset 0 → GL v = 0  (bottom of texture)
 *   memory offset H-1 → GL v = 1  (top of texture)
 *
 * Our atlas is generated top-to-bottom (glyph row 0 at PNG y=0).
 * So atlas cell row_idx occupies GL v range:
 *   v_low  = row_idx       * cv    ← PNG top of cell  = GL low v
 *   v_high = (row_idx + 1) * cv    ← PNG bottom of cell = GL higher v
 *
 * renderer_draw_quad_uv maps:  top-left  → (u0, v0)
 *                              bot-right → (u1, v1)
 *
 * On screen, "top" corresponds to LOWER v in GL (since GL v grows upward).
 * So: v0 (top of glyph on screen) = v_low, v1 (bottom) = v_high.
 *
 * Simple: NO subtraction from 1 needed. Just multiply row/col by cell fraction.
 */
static void glyph_uv(int c, float *u0, float *v0, float *u1, float *v1) {
    int idx = c - FONT_ATLAS_FIRST_CH;
    if (idx < 0 || idx > (FONT_ATLAS_LAST_CH - FONT_ATLAS_FIRST_CH)) idx = 0;

    int col_idx = idx % FONT_ATLAS_COLS;
    int row_idx = idx / FONT_ATLAS_COLS;

    float cu = (float)FONT_ATLAS_CELL_W / (float)s_tex_w;
    float cv = (float)FONT_ATLAS_CELL_H / (float)s_tex_h;

    *u0 = (float)col_idx * cu;
    *u1 = *u0 + cu;

    *v0 = (float)row_idx       * cv;   /* top of cell on screen = low GL v  */
    *v1 = (float)(row_idx + 1) * cv;   /* bottom = higher GL v              */
}

/* ---- Public API ---- */

float font_atlas_line_height(float scale) {
    return (float)FONT_ATLAS_CELL_H * scale;
}

float font_atlas_measure_width(const char *str, float scale) {
    /* CELL_W pixels per char + 1px inter-character gap at this scale */
    float adv = ((float)FONT_ATLAS_CELL_W + 1.0f) * scale;
    return (float)strlen(str) * adv;
}

void font_atlas_draw(float x, float y, float scale,
                     float r, float g, float b, float a,
                     const char *str) {
    if (!s_ready || !str || !*str) return;

    float gw  = (float)FONT_ATLAS_CELL_W * scale;
    float gh  = (float)FONT_ATLAS_CELL_H * scale;
    float adv = gw + scale;   /* advance = glyph width + 1px gap */

    renderer_bind_texture(s_tex_id);

    float cx = x;
    for (const char *p = str; *p; p++) {
        int c = (unsigned char)*p;
        if (c >= FONT_ATLAS_FIRST_CH && c <= FONT_ATLAS_LAST_CH) {
            float u0, v0, u1, v1;
            glyph_uv(c, &u0, &v0, &u1, &v1);
            renderer_draw_quad_uv(cx, y, gw, gh, r, g, b, a, u0, v0, u1, v1);
        }
        cx += adv;
    }

    renderer_flush_texture();
}
