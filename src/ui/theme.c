#include "theme.h"

#include <stdio.h>
#include <string.h>
#include <dirent.h>

#include "../core/log.h"

/* -----------------------------------------------------------------------
   The one and only Theme instance. Every other .c file reads this
   through theme_current() — nobody else gets a pointer to mutate it,
   which is what keeps "every screen agrees on the current theme" true
   without any synchronization: there's only one copy to disagree with. */

static Theme s_theme;
static bool  s_initialized = false;
static char  s_engine_root[512] = ".";

void theme_set_engine_root(const char *path) {
    if (path) {
        strncpy(s_engine_root, path, sizeof(s_engine_root) - 1);
        s_engine_root[sizeof(s_engine_root) - 1] = '\0';
    }
}

void theme_reset_default(void) {
    memset(&s_theme, 0, sizeof s_theme);
    strncpy(s_theme.name, "Default (Green)", sizeof s_theme.name - 1);

    s_theme.accent_r = 0.30f; s_theme.accent_g = 0.78f; s_theme.accent_b = 0.48f;

    s_theme.bg_r = 0.08f; s_theme.bg_g = 0.08f; s_theme.bg_b = 0.10f;
    s_theme.panel_bg_r = 0.09f; s_theme.panel_bg_g = 0.09f; s_theme.panel_bg_b = 0.11f;

    s_theme.border_r = 0.22f; s_theme.border_g = 0.22f; s_theme.border_b = 0.26f;
    s_theme.border_active_r = 0.30f; s_theme.border_active_g = 0.85f; s_theme.border_active_b = 0.50f;

    s_theme.text_r = 0.90f; s_theme.text_g = 0.90f; s_theme.text_b = 0.90f;
    s_theme.text_dim_r = 0.52f; s_theme.text_dim_g = 0.52f; s_theme.text_dim_b = 0.55f;

    s_theme.warn_r  = 0.90f; s_theme.warn_g  = 0.70f; s_theme.warn_b  = 0.20f;
    s_theme.error_r = 0.90f; s_theme.error_g = 0.30f; s_theme.error_b = 0.30f;

    s_initialized = true;
}

const Theme *theme_current(void) {
    if (!s_initialized) theme_reset_default();
    return &s_theme;
}

/* Parse one "key=r g b" or "key=value" line. Color keys take three
   space-separated floats; `name=` takes the rest of the line verbatim. */
static void parse_line(Theme *t, const char *line) {
    char key[64];
    float r, g, b;

    if (sscanf(line, "name=%63[^\n]", key) == 1) {
        strncpy(t->name, key, sizeof t->name - 1);
        t->name[sizeof t->name - 1] = '\0';
        return;
    }

    #define TRY_COLOR(k, fr, fg, fb) \
        if (sscanf(line, k "=%f %f %f", &r, &g, &b) == 3) { \
            t->fr = r; t->fg = g; t->fb = b; return; \
        }

    TRY_COLOR("accent",        accent_r,        accent_g,        accent_b)
    TRY_COLOR("bg",             bg_r,             bg_g,             bg_b)
    TRY_COLOR("panel_bg",       panel_bg_r,       panel_bg_g,       panel_bg_b)
    TRY_COLOR("border",         border_r,         border_g,         border_b)
    TRY_COLOR("border_active",  border_active_r,  border_active_g,  border_active_b)
    TRY_COLOR("text",           text_r,           text_g,           text_b)
    TRY_COLOR("text_dim",       text_dim_r,       text_dim_g,       text_dim_b)
    TRY_COLOR("warn",           warn_r,           warn_g,           warn_b)
    TRY_COLOR("error",          error_r,          error_g,          error_b)

    #undef TRY_COLOR
}

bool theme_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        LOG_WARN("theme_load: could not open '%s', keeping current theme", path);
        return false;
    }

    /* Parse into a scratch copy first — a malformed file partway
       through shouldn't leave the live theme half-old, half-new. */
    Theme scratch;
    memset(&scratch, 0, sizeof scratch);
    /* Start from defaults so a theme file only needs to override the
       colors it cares about changing — a community theme that's "just
       a different accent" doesn't need to respeicfy every background
       and text color too. */
    if (s_initialized) scratch = s_theme;
    else { theme_reset_default(); scratch = s_theme; }

    /* Filename (without extension) as the fallback display name, used
       if the file doesn't have its own `name=` line. */
    const char *base = path;
    for (const char *p = path; *p; p++) if (*p == '/' || *p == '\\') base = p + 1;
    strncpy(scratch.name, base, sizeof scratch.name - 1);
    char *dot = strrchr(scratch.name, '.');
    if (dot) *dot = '\0';

    char line[256];
    while (fgets(line, sizeof line, f))
        parse_line(&scratch, line);
    fclose(f);

    s_theme = scratch;
    s_initialized = true;
    LOG_INFO("Theme loaded: '%s' (%s)", s_theme.name, path);
    return true;
}

int theme_list_available(char out_paths[][256], int max_count) {
    int count = 0;
    char themes_dir[1024];
    snprintf(themes_dir, sizeof(themes_dir), "%s/themes", s_engine_root);
    
    DIR *d = opendir(themes_dir);
    if (!d) return 0;

    struct dirent *entry;
    while (count < max_count && (entry = readdir(d)) != NULL) {
        const char *name = entry->d_name;
        size_t len = strlen(name);
        if (len > 6 && strcmp(name + len - 6, ".theme") == 0) {
            snprintf(out_paths[count], 256, "%s/themes/%s", s_engine_root, name);
            count++;
        }
    }
    closedir(d);
    return count;
}
