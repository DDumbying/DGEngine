#ifndef DGE_FONT_H
#define DGE_FONT_H

/*  The "dumbest possible" font: each glyph is a 5x7 grid of on/off
    cells, stored as 7 plain strings ('#' = lit, '.' = empty) so the
    bitmap is readable directly in source — no font file, no atlas
    texture, no bitmap-to-bytes encoding step. text.c turns each '#'
    into a renderer_draw_quad() call. That's the entire rendering path.

    This is a deliberate "good enough for a HUD" choice, not a real
    text-rendering system: no kerning, no anti-aliasing, uppercase
    only (lowercase input is upper-cased before lookup). The natural
    upgrade later is a real glyph atlas texture + the UV-textured-quad
    path in the renderer — worth doing the moment text gets large
    enough that per-pixel quads start mattering for the quad budget,
    or once lowercase / non-Latin text is needed. Not needed yet. */

#define FONT_GLYPH_W 5
#define FONT_GLYPH_H 7

/* Returns 7 row-strings (5 significant chars each) for `c`, or NULL if
   there's no glyph for it (caller should skip / advance the cursor
   without drawing — used for any character outside the supported set). */
const char *const *font_get_glyph(char c);

#endif /* DGE_FONT_H */
