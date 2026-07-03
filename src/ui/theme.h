#ifndef DGE_THEME_H
#define DGE_THEME_H

/*  Theme — a single global color palette, loaded once from a flat
    key=value text file in themes/ and read everywhere through one
    accessor: theme_current().

    WHY A FILE + GLOBAL INSTEAD OF A STRUCT PASSED AROUND:
    The previous attempt at theming (EditorSettings.theme, an enum +
    editor_settings_accent()) only worked inside settings_tab.c because
    nothing else's function signature had a place to receive the color
    from — tabbar_render(), panel_render(), sprites_tab_render(), etc.
    were all written before theming existed and each just hardcodes its
    own green/gray literals. Threading an EditorSettings* (or even just
    three floats) through every one of those signatures is the "patch"
    fix: it works, but it means every future UI file a contributor adds
    has to remember to accept and forward the same parameter, forever.

    A theme file + global accessor sidesteps that entirely. Any .c file
    that wants the current accent color calls theme_current()->accent_r
    — no signature change anywhere, no parameter to forget to thread
    through. Switching themes calls theme_load() with a different path;
    every screen picks up the new colors on its next render because
    they're all reading the same static struct, not a copy.

    This also makes the engine's theming genuinely extensible for an
    open-source project: a community theme is just a new .theme text
    file dropped in themes/, no recompilation, no enum to extend.       */

#include <stdbool.h>

typedef struct {
    char name[64];           /* display name, from the file's own header or filename */

    /* Accent — buttons, active states, highlighted borders */
    float accent_r, accent_g, accent_b;

    /* Background fills */
    float bg_r, bg_g, bg_b;             /* main content background */
    float panel_bg_r, panel_bg_g, panel_bg_b;   /* sidebar/pane background */

    /* Borders */
    float border_r, border_g, border_b;         /* inactive border */
    float border_active_r, border_active_g, border_active_b; /* focused/hovered border */

    /* Text */
    float text_r, text_g, text_b;               /* primary text */
    float text_dim_r, text_dim_g, text_dim_b;   /* secondary/label text */

    /* Status colors */
    float warn_r, warn_g, warn_b;
    float error_r, error_g, error_b;
} Theme;

/* Returns the currently active theme. Always valid — falls back to the
   built-in default if nothing has been loaded yet, so callers never
   need a NULL check. */
const Theme *theme_current(void);

/* Load a .theme file and make it the active theme. Returns false (and
   leaves the current theme unchanged) if the file can't be read — the
   engine should never end up with no theme at all just because a file
   went missing. */
bool theme_load(const char *path);

/* Reset to the engine's built-in default palette (the green accent
   theme that shipped before the file-based system existed). Useful as
   a "Reset to Default" action and as the bootstrap before the first
   theme_load() call. */
void theme_reset_default(void);

/* List .theme files found in the themes/ directory (relative to the
   project's cwd, same convention as assets/sprites.png etc). Writes
   up to max_count paths into out_paths, returns the number found. */
int theme_list_available(char out_paths[][256], int max_count);

#endif /* DGE_THEME_H */
