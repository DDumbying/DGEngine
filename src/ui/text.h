#ifndef DGE_TEXT_H
#define DGE_TEXT_H

/*  Draws `str` with top-left corner at (x, y) in whatever coordinate
    space the renderer is currently set up for — call this between
    renderer_begin_ui() and renderer_end() for screen-space HUD text.
    `scale` is the pixel size of one font cell (5 wide unscaled glyph
    cells, so a whole glyph is roughly 5*scale wide, 7*scale tall).
    Characters with no glyph (font_get_glyph returns NULL) are skipped
    but still advance the cursor, so alignment doesn't break on an
    unsupported character. */
void text_draw(float x, float y, float scale,
               float r, float g, float b, float a,
               const char *str);

/* Pixel width `str` would occupy at the given scale — for right-aligning
   or centering a line before drawing it. */
float text_measure_width(const char *str, float scale);

/* Pixel height of a single line at the given scale (always FONT_GLYPH_H
   cells; no multi-line wrapping here — caller advances y between lines). */
float text_line_height(float scale);

#endif /* DGE_TEXT_H */
