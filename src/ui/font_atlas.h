#ifndef DGE_FONT_ATLAS_H
#define DGE_FONT_ATLAS_H

/*  Texture-atlas font renderer.
    We generate assets/font.png with gen_font.py:
      - 16 columns x 6 rows grid
      - Each cell is FONT_ATLAS_CELL_W x FONT_ATLAS_CELL_H pixels
      - ASCII chars 32..126 packed left-to-right, top-to-bottom

    The atlas is loaded once, stays on the GPU, and text_draw() submits
    one UV-mapped quad per character — orders of magnitude fewer vertices
    than the old per-pixel bitmap approach.                              */

#include <stdbool.h>

/* Atlas layout constants — must match gen_font.py */
#define FONT_ATLAS_COLS      16
#define FONT_ATLAS_ROWS       6
#define FONT_ATLAS_CELL_W     6
#define FONT_ATLAS_CELL_H    10
#define FONT_ATLAS_FIRST_CH  32   /* ' ' */
#define FONT_ATLAS_LAST_CH  126   /* '~' */

/* Load the PNG from disk and upload to the GPU.
   path is relative to the executable's CWD (e.g. "assets/font.png").
   Returns false and leaves the atlas in an invalid state on failure;
   text_draw() will silently no-op rather than crash.                  */
bool font_atlas_load(const char *path);

/* Release the GPU texture. */
void font_atlas_destroy(void);

/* Render a string at pixel position (x, y) with the given tint colour.
   scale is a pixel multiplier: 1.0 = one-to-one atlas cell size,
   2.0 = double-size, etc.  Vertical advance equals
   font_atlas_line_height(scale).                                       */
void font_atlas_draw(float x, float y, float scale,
                     float r, float g, float b, float a,
                     const char *str);

/* Pixel width of str at the given scale. */
float font_atlas_measure_width(const char *str, float scale);

/* Height of one text line at the given scale. */
float font_atlas_line_height(float scale);

/* True once font_atlas_load() has succeeded. */
bool font_atlas_ready(void);

#endif /* DGE_FONT_ATLAS_H */
