#include "text.h"

#include <string.h>
#include "font.h"
#include "font_atlas.h"
#include "../renderer/renderer.h"

/* One extra column of spacing after each bitmap glyph cell so letters
   don't visually run together (legacy path only). */
#define GLYPH_ADVANCE_CELLS (FONT_GLYPH_W + 1)

/* ---- Atlas path ---- */

void text_draw(float x, float y, float scale,
               float r, float g, float b, float a,
               const char *str) {
    if (font_atlas_ready()) {
        font_atlas_draw(x, y, scale, r, g, b, a, str);
        return;
    }

    /* ---- Legacy pixel-bitmap path ---- */
    float cursor_x = x;
    for (const char *p = str; *p; p++) {
        const char *const *rows = font_get_glyph(*p);
        if (rows) {
            for (int row = 0; row < FONT_GLYPH_H; row++) {
                const char *line = rows[row];
                for (int col = 0; col < FONT_GLYPH_W; col++) {
                    if (line[col] == '#') {
                        renderer_draw_quad(cursor_x + (float)col * scale,
                                           y + (float)row * scale,
                                           scale, scale,
                                           r, g, b, a);
                    }
                }
            }
        }
        cursor_x += (float)GLYPH_ADVANCE_CELLS * scale;
    }
}

float text_measure_width(const char *str, float scale) {
    if (font_atlas_ready())
        return font_atlas_measure_width(str, scale);
    return (float)strlen(str) * (float)GLYPH_ADVANCE_CELLS * scale;
}

float text_line_height(float scale) {
    if (font_atlas_ready())
        return font_atlas_line_height(scale);
    return (float)FONT_GLYPH_H * scale;
}
