#ifndef DGE_FONT_ATLAS_H
#define DGE_FONT_ATLAS_H

/*  Texture-atlas font renderer.

    Atlas: assets/font.png — 96×48 px, 16 cols × 6 rows.
    Cell:  6×8 px  (5×7 bitmap glyph, 1px right/bottom pad).
    Every pixel is 0 or 255 alpha — GL_NEAREST keeps it crisp at any scale.

    Typical scales used in the engine:
      1.0 →  6×8  px  (minimap labels, dense data)
      1.5 →  9×12 px  (panel body text, fields)
      2.0 → 12×16 px  (section labels)
      3.0 → 18×24 px  (title / project name)

    UV mapping (no stbi flip):
      PNG row 0 → GL v=0 (bottom of texture).
      v0 = row_idx * cv, v1 = (row_idx+1) * cv — no subtraction from 1. */

#include <stdbool.h>

#define FONT_ATLAS_COLS      16
#define FONT_ATLAS_ROWS       6
#define FONT_ATLAS_CELL_W     6
#define FONT_ATLAS_CELL_H     8
#define FONT_ATLAS_FIRST_CH  32
#define FONT_ATLAS_LAST_CH  126

bool  font_atlas_load(const char *path);
void  font_atlas_destroy(void);
void  font_atlas_draw(float x, float y, float scale,
                      float r, float g, float b, float a,
                      const char *str);
float font_atlas_measure_width(const char *str, float scale);
float font_atlas_line_height(float scale);
bool  font_atlas_ready(void);

#endif /* DGE_FONT_ATLAS_H */
