#include "settings_tab.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "../platform/input.h"
#include "../renderer/renderer.h"
#include "text.h"
#include "tabbar.h"
#include "layout.h"
#include "theme.h"

/*  Two-pane layout: a fixed-width left rail of section buttons, and a
    right-side detail pane that only ever draws the active section.

    Both settings_tab_update() and settings_tab_render() walk the SAME
    rail-button geometry from a shared helper (rail_button_rect) so the
    clickable area in update() can never drift from the drawn area in
    render() — the old single-column version computed y-offsets
    independently in each function, which is exactly the kind of thing
    that silently goes stale the next time someone edits one but not
    the other. Centralizing this once removes that whole class of bug,
    not just for this pass but for any future section that gets added. */

#define RAIL_W      160.0f
#define RAIL_PAD     10.0f
#define RAIL_BTN_H   34.0f
#define RAIL_GAP      4.0f

#define PAD        18.0f
#define FIELD_H    24.0f
#define BTN_H      26.0f
#define GAP         8.0f
#define COL_W     320.0f
#define SCALE_LBL  1.5f
#define SCALE_SM   1.3f
#define SCALE_HEAD 1.8f
#define CONTENT_Y  ((float)TABBAR_H)

static const char *SECTION_TITLES[SETTINGS_SEC_COUNT] = {
    "GENERAL",
    "APPEARANCE",
    "DISPLAY",
    "WORLD SHAPE",
    "PROJECT FILE",
};

static bool hittest(float x,float y,float w,float h,int mx,int my){
    return (float)mx>=x&&(float)mx<x+w&&(float)my>=y&&(float)my<y+h;
}
static void draw_border(float x,float y,float w,float h,float r,float g,float b){
    renderer_draw_quad(x,y,w,1,r,g,b,1); renderer_draw_quad(x,y+h-1,w,1,r,g,b,1);
    renderer_draw_quad(x,y,1,h,r,g,b,1); renderer_draw_quad(x+w-1,y,1,h,r,g,b,1);
}

static bool draw_btn(float x,float y,float w,float h,const char*l,
                     bool active, float ar, float ag, float ab, int mx,int my){
    bool hov=hittest(x,y,w,h,mx,my);
    bool clk=hov&&input_mouse_button_pressed(SDL_BUTTON_LEFT);
    float bg=active?(ar*0.4f):(hov?0.22f:0.13f);
    float gg=active?(ag*0.7f):(hov?0.60f:0.45f);
    float bb2=active?(ab*0.4f):(hov?0.24f:0.13f);
    renderer_draw_quad(x,y,w,h,bg,gg,bb2,1);
    draw_border(x,y,w,h, active?ar*0.5f:0.26f, active?ag*0.9f:0.26f, active?ab*0.5f:0.30f);
    float tw=text_measure_width(l,SCALE_LBL),th=text_line_height(SCALE_LBL);
    text_draw(x+(w-tw)*.5f,y+(h-th)*.5f,SCALE_LBL,1,1,1,1,l);
    return clk;
}

/* Draw a toggle button (on/off) AND apply the click — used from update().
   Mutates *value when clicked. */
static bool draw_toggle(float x, float y, float w, float h, const char *label,
                         bool *value, float ar, float ag, float ab, int mx, int my) {
    bool clicked = draw_btn(x, y, w, h, label, *value, ar, ag, ab, mx, my);
    if (clicked) *value = !(*value);
    float dot_r = h * 0.3f;
    float dot_x = x + w - dot_r * 2.0f - 4.0f;
    float dot_y = y + (h - dot_r) * 0.5f;
    if (*value)
        renderer_draw_quad(dot_x, dot_y, dot_r, dot_r, ar, ag, ab, 1.0f);
    else
        renderer_draw_quad(dot_x, dot_y, dot_r, dot_r, 0.28f, 0.28f, 0.30f, 1.0f);
    return clicked;
}

/* Pure-draw toggle for render() — never mutates, just shows current state.
   Same visual as draw_toggle but takes a plain bool instead of bool*, so
   there's no pointer for a render-time hittest to (incorrectly) write
   through. update() is the only place toggle state actually changes. */
static void draw_toggle_display(float x, float y, float w, float h, const char *label,
                                 bool value, float ar, float ag, float ab, int mx, int my) {
    draw_btn(x, y, w, h, label, value, ar, ag, ab, mx, my);
    float dot_r = h * 0.3f;
    float dot_x = x + w - dot_r * 2.0f - 4.0f;
    float dot_y = y + (h - dot_r) * 0.5f;
    if (value)
        renderer_draw_quad(dot_x, dot_y, dot_r, dot_r, ar, ag, ab, 1.0f);
    else
        renderer_draw_quad(dot_x, dot_y, dot_r, dot_r, 0.28f, 0.28f, 0.30f, 1.0f);
}

static void do_focus(SettingsTab*st,SettingsFocus f){
    switch(st->focus){
        case SETTINGS_FOCUS_NAME:   textinput_unfocus(&st->fi_name);   break;
        case SETTINGS_FOCUS_GRID_W: textinput_unfocus(&st->fi_grid_w); break;
        case SETTINGS_FOCUS_GRID_H: textinput_unfocus(&st->fi_grid_h); break;
        case SETTINGS_FOCUS_TILE_W: textinput_unfocus(&st->fi_tile_w); break;
        case SETTINGS_FOCUS_TILE_H: textinput_unfocus(&st->fi_tile_h); break;
        default: break;
    }
    st->focus=f;
    switch(f){
        case SETTINGS_FOCUS_NAME:   textinput_focus(&st->fi_name);   break;
        case SETTINGS_FOCUS_GRID_W: textinput_focus(&st->fi_grid_w); break;
        case SETTINGS_FOCUS_GRID_H: textinput_focus(&st->fi_grid_h); break;
        case SETTINGS_FOCUS_TILE_W: textinput_focus(&st->fi_tile_w); break;
        case SETTINGS_FOCUS_TILE_H: textinput_focus(&st->fi_tile_h); break;
        default: break;
    }
}

/* ---------------------------------------------------------------------
   Left rail — shared geometry for update() and render().
   Returns the screen rect for rail button `i`. Same function, same
   inputs, every call site — this is what keeps clicks and drawing in
   sync as sections are added/reordered in the future. */
static void rail_button_rect(int i, float *out_x, float *out_y,
                              float *out_w, float *out_h) {
    *out_x = 0.0f;
    *out_y = CONTENT_Y + RAIL_PAD + (float)i * (RAIL_BTN_H + RAIL_GAP);
    *out_w = RAIL_W;
    *out_h = RAIL_BTN_H;
}

/* Where the detail pane starts — same on every section. */
static float detail_pane_x(void) { return RAIL_W + PAD; }

/* ---------------------------------------------------------------------
   Per-section UPDATE handlers. Each owns its own local "cursor" y
   starting from CONTENT_Y+PAD in the detail pane — sections don't
   stack on top of each other any more, so each one's hittest math
   only has to agree with that same section's render function, not
   with four others above it. */

static void update_general(SettingsTab *st, Project *proj, int mx, int my) {
    float x = detail_pane_x(), y = CONTENT_Y + PAD + text_line_height(SCALE_HEAD) + GAP*2;
    float fw = COL_W;
    float half = (fw - GAP) * 0.5f;

    EditorSettings *s = st->settings;
    float ar, ag, ab;
    editor_settings_accent(s, &ar, &ag, &ab);

    /* Name field */
    if (hittest(x, y+text_line_height(SCALE_SM)+2, fw, FIELD_H, mx, my) &&
        input_mouse_button_pressed(SDL_BUTTON_LEFT)) do_focus(st, SETTINGS_FOCUS_NAME);
    if (st->focus == SETTINGS_FOCUS_NAME)
        textinput_update(&st->fi_name, x+4, y+text_line_height(SCALE_SM)+4, fw-8, SCALE_LBL);
    y += text_line_height(SCALE_SM)+2+FIELD_H+GAP;

    /* Grid W/H */
    if (hittest(x, y+text_line_height(SCALE_SM)+2, half, FIELD_H, mx, my) &&
        input_mouse_button_pressed(SDL_BUTTON_LEFT)) do_focus(st, SETTINGS_FOCUS_GRID_W);
    if (st->focus == SETTINGS_FOCUS_GRID_W)
        textinput_update(&st->fi_grid_w, x+4, y+text_line_height(SCALE_SM)+4, half-8, SCALE_LBL);
    if (hittest(x+half+GAP, y+text_line_height(SCALE_SM)+2, half, FIELD_H, mx, my) &&
        input_mouse_button_pressed(SDL_BUTTON_LEFT)) do_focus(st, SETTINGS_FOCUS_GRID_H);
    if (st->focus == SETTINGS_FOCUS_GRID_H)
        textinput_update(&st->fi_grid_h, x+half+GAP+4, y+text_line_height(SCALE_SM)+4, half-8, SCALE_LBL);
    y += text_line_height(SCALE_SM)+2+FIELD_H+GAP;

    /* Tile W/H */
    if (hittest(x, y+text_line_height(SCALE_SM)+2, half, FIELD_H, mx, my) &&
        input_mouse_button_pressed(SDL_BUTTON_LEFT)) do_focus(st, SETTINGS_FOCUS_TILE_W);
    if (st->focus == SETTINGS_FOCUS_TILE_W)
        textinput_update(&st->fi_tile_w, x+4, y+text_line_height(SCALE_SM)+4, half-8, SCALE_LBL);
    if (hittest(x+half+GAP, y+text_line_height(SCALE_SM)+2, half, FIELD_H, mx, my) &&
        input_mouse_button_pressed(SDL_BUTTON_LEFT)) do_focus(st, SETTINGS_FOCUS_TILE_H);
    if (st->focus == SETTINGS_FOCUS_TILE_H)
        textinput_update(&st->fi_tile_h, x+half+GAP+4, y+text_line_height(SCALE_SM)+4, half-8, SCALE_LBL);
    y += text_line_height(SCALE_SM)+2+FIELD_H+GAP+4;

    /* Apply buttons */
    if (draw_btn(x, y, 140, BTN_H, "APPLY GRID", false, ar, ag, ab, mx, my)) {
        int gw = atoi(textinput_get(&st->fi_grid_w));
        int gh = atoi(textinput_get(&st->fi_grid_h));
        if (gw >= PROJECT_GRID_MIN && gw <= PROJECT_GRID_MAX &&
            gh >= PROJECT_GRID_MIN && gh <= PROJECT_GRID_MAX) {
            st->pending_grid_w = gw; st->pending_grid_h = gh;
            st->wants_resize = true;
            proj->grid_w = gw; proj->grid_h = gh;
        } else {
            snprintf(st->status, sizeof st->status, "GRID %d-%d ONLY",
                     PROJECT_GRID_MIN, PROJECT_GRID_MAX);
        }
    }
    if (draw_btn(x+148, y, 140, BTN_H, "APPLY TILE", false, ar, ag, ab, mx, my)) {
        int tw = atoi(textinput_get(&st->fi_tile_w));
        int th = atoi(textinput_get(&st->fi_tile_h));
        if (tw >= 8 && tw <= 256 && th >= 4 && th <= 128) {
            st->pending_tile_w = tw; st->pending_tile_h = th;
            st->wants_tile_resize = true;
            proj->tile_w = tw; proj->tile_h = th;
        } else {
            snprintf(st->status, sizeof st->status, "TILE W 8-256 H 4-128");
        }
    }
}

static void update_appearance(SettingsTab *st, int mx, int my) {
    float x = detail_pane_x(), y = CONTENT_Y + PAD + text_line_height(SCALE_HEAD) + GAP*2;
    float fw = COL_W;
    EditorSettings *s = st->settings;

    y += text_line_height(SCALE_SM) + 4.0f; /* "THEME" label */

    /* Real .theme files found in themes/ — community themes show up
       here with zero recompilation, since this list is read from disk
       every frame the section is open rather than baked into an enum. */
    char paths[16][256];
    int n = theme_list_available(paths, 16);
    float tw = (fw - GAP * 3.0f) / 4.0f;
    int per_row = 4;
    for (int i = 0; i < n; i++) {
        int row = i / per_row, col = i % per_row;
        float bx = x + (float)col * (tw + GAP);
        float by = y + (float)row * (BTN_H + GAP);
        if (hittest(bx, by, tw, BTN_H, mx, my) &&
            input_mouse_button_pressed(SDL_BUTTON_LEFT)) {
            strncpy(s->theme_path, paths[i], sizeof s->theme_path - 1);
            s->theme_path[sizeof s->theme_path - 1] = '\0';
            theme_load(paths[i]);
            snprintf(st->status, sizeof st->status, "Theme: %s", theme_current()->name);
        }
    }
    int rows = (n + per_row - 1) / per_row;
    if (rows < 1) rows = 1;
    y += (float)rows * (BTN_H + GAP) + GAP;

    y += text_line_height(SCALE_SM) + 4.0f; /* "WORLD EDITOR PANEL" label */
    if (hittest(x, y, fw*0.48f, BTN_H, mx, my) &&
        input_mouse_button_pressed(SDL_BUTTON_LEFT))
        s->panel_mode = WORLD_PANEL_DOCKED;
    if (hittest(x+fw*0.5f, y, fw*0.48f, BTN_H, mx, my) &&
        input_mouse_button_pressed(SDL_BUTTON_LEFT))
        s->panel_mode = WORLD_PANEL_FLOATING;
}

static void update_display(SettingsTab *st, int mx, int my) {
    float x = detail_pane_x(), y = CONTENT_Y + PAD + text_line_height(SCALE_HEAD) + GAP*2;
    float fw = COL_W;
    EditorSettings *s = st->settings;
    float ar, ag, ab;
    editor_settings_accent(s, &ar, &ag, &ab);

    draw_toggle(x, y, fw*0.48f, BTN_H, "SHOW FPS", &s->show_fps, ar, ag, ab, mx, my);
    draw_toggle(x+fw*0.5f, y, fw*0.48f, BTN_H, "TILE COORDS", &s->show_tile_coords, ar, ag, ab, mx, my);
    y += BTN_H + GAP;
    draw_toggle(x, y, fw*0.48f, BTN_H, "MINIMAP", &s->show_minimap, ar, ag, ab, mx, my);
    draw_toggle(x+fw*0.5f, y, fw*0.48f, BTN_H, "GRID OVERLAY", &s->show_grid_overlay, ar, ag, ab, mx, my);
}

static void update_world_shape(SettingsTab *st, int mx, int my) {
    float x = detail_pane_x(), y = CONTENT_Y + PAD + text_line_height(SCALE_HEAD) + GAP*2;
    float fw = COL_W;
    EditorSettings *s = st->settings;
    float ar, ag, ab;
    editor_settings_accent(s, &ar, &ag, &ab);

    draw_toggle(x, y, fw, BTN_H, "ENABLE FREEFORM WORLD SHAPE",
                &s->world_shape_active, ar, ag, ab, mx, my);
}

static void update_project_file(SettingsTab *st, Project *proj, int mx, int my, bool *saved) {
    float x = detail_pane_x(), y = CONTENT_Y + PAD + text_line_height(SCALE_HEAD) + GAP*2;
    EditorSettings *s = st->settings;
    float ar, ag, ab;
    editor_settings_accent(s, &ar, &ag, &ab);

    if (draw_btn(x, y, 130, BTN_H, "SAVE PROJECT", false, ar, ag, ab, mx, my)) {
        strncpy(proj->name, textinput_get(&st->fi_name), PROJECT_NAME_MAX-1);
        project_save(proj);
        snprintf(st->status, sizeof st->status, "PROJECT SAVED.");
        *saved = true;
    }
    if (draw_btn(x+138, y, 160, BTN_H, "CLOSE PROJECT", false, ar, ag, ab, mx, my)) {
        strncpy(proj->name, textinput_get(&st->fi_name), PROJECT_NAME_MAX-1);
        project_save(proj);
        st->wants_close_project = true;
    }
}

/* ---------------------------------------------------------------------
   Per-section RENDER handlers. Mirror the y-cursor math in the update
   handlers above exactly (same starting y, same line-height additions)
   so the clickable area always matches what's drawn. */

static void render_general(const SettingsTab *st, const Project *proj, int mx, int my) {
    float x = detail_pane_x(), y = CONTENT_Y + PAD;
    float fw = COL_W;
    float half = (fw - GAP) * 0.5f;
    EditorSettings *s = st->settings;
    float ar, ag, ab;
    editor_settings_accent(s, &ar, &ag, &ab);

    text_draw(x, y, SCALE_HEAD, ar, ag, ab, 1, "GENERAL");
    y += text_line_height(SCALE_HEAD) + GAP*2;

    text_draw(x, y, SCALE_SM, 0.52f, 0.52f, 0.55f, 1, "PROJECT NAME");
    float fy = y + text_line_height(SCALE_SM) + 2;
    float bg = st->focus == SETTINGS_FOCUS_NAME ? 0.17f : 0.10f;
    renderer_draw_quad(x, fy, fw, FIELD_H, bg, bg, bg, 1);
    draw_border(x, fy, fw, FIELD_H,
        st->focus==SETTINGS_FOCUS_NAME?0.30f:0.22f,
        st->focus==SETTINGS_FOCUS_NAME?ar:0.28f,
        st->focus==SETTINGS_FOCUS_NAME?0.42f:0.22f);
    float th = text_line_height(SCALE_LBL);
    textinput_render(&st->fi_name, x+4, fy+(FIELD_H-th)*.5f, SCALE_LBL, 1,1,1,1);
    y += text_line_height(SCALE_SM)+2+FIELD_H+GAP;

    text_draw(x, y, SCALE_SM, 0.52f, 0.52f, 0.55f, 1, "GRID W");
    text_draw(x+half+GAP, y, SCALE_SM, 0.52f, 0.52f, 0.55f, 1, "GRID H");
    float fy2 = y + text_line_height(SCALE_SM) + 2;
    float bg2 = st->focus==SETTINGS_FOCUS_GRID_W?0.17f:0.10f;
    renderer_draw_quad(x, fy2, half, FIELD_H, bg2, bg2, bg2, 1);
    draw_border(x, fy2, half, FIELD_H,
        st->focus==SETTINGS_FOCUS_GRID_W?0.30f:0.22f,
        st->focus==SETTINGS_FOCUS_GRID_W?ar:0.28f,
        st->focus==SETTINGS_FOCUS_GRID_W?0.42f:0.22f);
    textinput_render(&st->fi_grid_w, x+4, fy2+(FIELD_H-th)*.5f, SCALE_LBL, 1,1,1,1);
    float bg3 = st->focus==SETTINGS_FOCUS_GRID_H?0.17f:0.10f;
    renderer_draw_quad(x+half+GAP, fy2, half, FIELD_H, bg3, bg3, bg3, 1);
    draw_border(x+half+GAP, fy2, half, FIELD_H,
        st->focus==SETTINGS_FOCUS_GRID_H?0.30f:0.22f,
        st->focus==SETTINGS_FOCUS_GRID_H?ag:0.28f,
        st->focus==SETTINGS_FOCUS_GRID_H?0.42f:0.22f);
    textinput_render(&st->fi_grid_h, x+half+GAP+4, fy2+(FIELD_H-th)*.5f, SCALE_LBL, 1,1,1,1);
    y += text_line_height(SCALE_SM)+2+FIELD_H+GAP;

    text_draw(x, y, SCALE_SM, 0.52f, 0.52f, 0.55f, 1, "TILE W (PX)");
    text_draw(x+half+GAP, y, SCALE_SM, 0.52f, 0.52f, 0.55f, 1, "TILE H (PX)");
    float fy3 = y + text_line_height(SCALE_SM) + 2;
    float bg4 = st->focus==SETTINGS_FOCUS_TILE_W?0.17f:0.10f;
    renderer_draw_quad(x, fy3, half, FIELD_H, bg4, bg4, bg4, 1);
    draw_border(x, fy3, half, FIELD_H,
        st->focus==SETTINGS_FOCUS_TILE_W?0.30f:0.22f,
        st->focus==SETTINGS_FOCUS_TILE_W?ar:0.28f,
        st->focus==SETTINGS_FOCUS_TILE_W?0.42f:0.22f);
    textinput_render(&st->fi_tile_w, x+4, fy3+(FIELD_H-th)*.5f, SCALE_LBL, 1,1,1,1);
    float bg5 = st->focus==SETTINGS_FOCUS_TILE_H?0.17f:0.10f;
    renderer_draw_quad(x+half+GAP, fy3, half, FIELD_H, bg5, bg5, bg5, 1);
    draw_border(x+half+GAP, fy3, half, FIELD_H,
        st->focus==SETTINGS_FOCUS_TILE_H?0.30f:0.22f,
        st->focus==SETTINGS_FOCUS_TILE_H?ag:0.28f,
        st->focus==SETTINGS_FOCUS_TILE_H?0.42f:0.22f);
    textinput_render(&st->fi_tile_h, x+half+GAP+4, fy3+(FIELD_H-th)*.5f, SCALE_LBL, 1,1,1,1);
    y += text_line_height(SCALE_SM)+2+FIELD_H+GAP+4;

    draw_btn(x, y, 140, BTN_H, "APPLY GRID", false, ar, ag, ab, mx, my);
    draw_btn(x+148, y, 140, BTN_H, "APPLY TILE", false, ar, ag, ab, mx, my);
    y += BTN_H + GAP*2;

    char cur[128];
    snprintf(cur, sizeof cur, "CURRENT: %s  %dx%d  TILE %dx%d",
             proj->name, proj->grid_w, proj->grid_h, proj->tile_w, proj->tile_h);
    text_draw(x, y, SCALE_SM, 0.40f, 0.40f, 0.45f, 1, cur);
    y += text_line_height(SCALE_SM) + GAP*2;

    /* Genre is a creation-time fork (see project.h's GenreProfile
       comment) — it decides which systems run at all, so it isn't
       something the editor lets you flip after the fact the way grid
       size or tile size can be. Shown here read-only so it's always
       obvious which fork of the engine a project is running, since
       that's what explains why the World panel doesn't show a Weather
       section, or why the HUD doesn't show resource counts, for a
       TACTICS or FREEFORM project. */
    text_draw(x, y, SCALE_SM, 0.52f, 0.52f, 0.55f, 1, "GAME TYPE (SET AT CREATION)");
    y += text_line_height(SCALE_SM) + 4;
    char glabel[64];
    snprintf(glabel, sizeof glabel, "%s", genre_profile_name(proj->genre));
    renderer_draw_quad(x, y, fw, BTN_H, 0.10f, 0.10f, 0.12f, 1.0f);
    draw_border(x, y, fw, BTN_H, 0.20f, ar*0.6f, 0.20f);
    text_draw(x + 8, y + (BTN_H - text_line_height(SCALE_LBL))*.5f,
              SCALE_LBL, ar, ag, ab, 1, glabel);
    y += BTN_H + 4;
    text_draw(x, y, SCALE_SM, 0.40f, 0.40f, 0.45f, 1, genre_profile_desc(proj->genre));
}

static void render_appearance(const SettingsTab *st, int mx, int my) {
    float x = detail_pane_x(), y = CONTENT_Y + PAD;
    float fw = COL_W;
    EditorSettings *s = st->settings;
    const Theme *th = theme_current();

    text_draw(x, y, SCALE_HEAD, th->accent_r, th->accent_g, th->accent_b, 1, "APPEARANCE");
    y += text_line_height(SCALE_HEAD) + GAP*2;

    text_draw(x, y, SCALE_SM, th->text_dim_r, th->text_dim_g, th->text_dim_b, 1, "THEME");
    y += text_line_height(SCALE_SM) + 4;

    /* Each .theme file on disk gets its own button, colored with that
       file's own accent — clicking a community theme shows you its
       actual palette before you even apply it, not a generic swatch. */
    char paths[16][256];
    int n = theme_list_available(paths, 16);
    float tw = (fw - GAP * 3.0f) / 4.0f;
    int per_row = 4;
    for (int i = 0; i < n; i++) {
        int row = i / per_row, col = i % per_row;
        float bx = x + (float)col * (tw + GAP);
        float by = y + (float)row * (BTN_H + GAP);
        bool active = (strcmp(s->theme_path, paths[i]) == 0);

        /* Peek the file's accent without fully loading it as current —
           cheap re-read since this list is tiny and only rendered while
           the APPEARANCE section is open. */
        Theme preview;
        FILE *f = fopen(paths[i], "r");
        const char *label = paths[i];
        float pr = th->accent_r, pg = th->accent_g, pb = th->accent_b;
        if (f) {
            /* Reuse theme_load's parsing by loading into the live
               theme only if this is the active one; otherwise just
               read the accent line directly for the preview swatch. */
            char line[256];
            memset(&preview, 0, sizeof preview);
            while (fgets(line, sizeof line, f)) {
                float r,g,b;
                if (sscanf(line, "accent=%f %f %f", &r,&g,&b) == 3) {
                    pr = r; pg = g; pb = b;
                }
            }
            fclose(f);
            /* Display just the filename, not the full themes/ path */
            const char *base = paths[i];
            for (const char *p = paths[i]; *p; p++) if (*p=='/'||*p=='\\') base = p+1;
            label = base;
        }
        draw_btn(bx, by, tw, BTN_H, label, active, pr, pg, pb, mx, my);
    }
    int rows = (n + per_row - 1) / per_row;
    if (rows < 1) rows = 1;
    y += (float)rows * (BTN_H + GAP) + GAP;

    if (n == 0) {
        text_draw(x, y, SCALE_SM, 0.45f, 0.45f, 0.48f, 1,
                  "No themes/ folder found — using built-in default.");
        y += text_line_height(SCALE_SM) + GAP;
    }

    text_draw(x, y, SCALE_SM, th->text_dim_r, th->text_dim_g, th->text_dim_b, 1, "WORLD EDITOR PANEL");
    y += text_line_height(SCALE_SM) + 4;
    draw_btn(x, y, fw*0.48f, BTN_H, "DOCKED",
             s->panel_mode==WORLD_PANEL_DOCKED, th->accent_r, th->accent_g, th->accent_b, mx, my);
    draw_btn(x+fw*0.5f, y, fw*0.48f, BTN_H, "FLOATING",
             s->panel_mode==WORLD_PANEL_FLOATING, th->accent_r, th->accent_g, th->accent_b, mx, my);
    y += BTN_H + GAP*2;

    text_draw(x, y, SCALE_SM, 0.40f, 0.40f, 0.45f, 1,
              "THEME COLORS ARE FIXED FOR NOW; A CONFIG-FILE BASED");
    y += text_line_height(SCALE_SM) + 2;
    text_draw(x, y, SCALE_SM, 0.40f, 0.40f, 0.45f, 1,
              "THEME SYSTEM IS PLANNED SO COMMUNITY THEMES CAN DROP IN.");
}

static void render_display(const SettingsTab *st, int mx, int my) {
    float x = detail_pane_x(), y = CONTENT_Y + PAD;
    float fw = COL_W;
    EditorSettings *s = st->settings;
    float ar, ag, ab;
    editor_settings_accent(s, &ar, &ag, &ab);

    text_draw(x, y, SCALE_HEAD, ar, ag, ab, 1, "DISPLAY");
    y += text_line_height(SCALE_HEAD) + GAP*2;

    draw_toggle_display(x, y, fw*0.48f, BTN_H, "SHOW FPS", s->show_fps, ar, ag, ab, mx, my);
    draw_toggle_display(x+fw*0.5f, y, fw*0.48f, BTN_H, "TILE COORDS", s->show_tile_coords, ar, ag, ab, mx, my);
    y += BTN_H + GAP;
    draw_toggle_display(x, y, fw*0.48f, BTN_H, "MINIMAP", s->show_minimap, ar, ag, ab, mx, my);
    draw_toggle_display(x+fw*0.5f, y, fw*0.48f, BTN_H, "GRID OVERLAY", s->show_grid_overlay, ar, ag, ab, mx, my);
}

static void render_world_shape(const SettingsTab *st, int mx, int my) {
    float x = detail_pane_x(), y = CONTENT_Y + PAD;
    float fw = COL_W;
    EditorSettings *s = st->settings;
    float ar, ag, ab;
    editor_settings_accent(s, &ar, &ag, &ab);

    text_draw(x, y, SCALE_HEAD, ar, ag, ab, 1, "WORLD SHAPE");
    y += text_line_height(SCALE_HEAD) + GAP*2;

    draw_toggle_display(x, y, fw, BTN_H, "ENABLE FREEFORM WORLD SHAPE",
                s->world_shape_active, ar, ag, ab, mx, my);
    y += BTN_H + 6.0f;

    if (s->world_shape_active) {
        text_draw(x, y, SCALE_SM, 0.60f, 0.60f, 0.62f, 1.0f,
                  "IN SHAPE MODE: RMB DISABLES TILES (CREATES HOLES)");
        y += text_line_height(SCALE_SM) + 2.0f;
        text_draw(x, y, SCALE_SM, 0.60f, 0.60f, 0.62f, 1.0f,
                  "LMB RE-ENABLES TILES");
    }
}

static void render_project_file(const SettingsTab *st, const Project *proj, int mx, int my) {
    float x = detail_pane_x(), y = CONTENT_Y + PAD;
    EditorSettings *s = st->settings;
    float ar, ag, ab;
    editor_settings_accent(s, &ar, &ag, &ab);

    text_draw(x, y, SCALE_HEAD, ar, ag, ab, 1, "PROJECT FILE");
    y += text_line_height(SCALE_HEAD) + GAP*2;

    draw_btn(x, y, 130, BTN_H, "SAVE PROJECT", false, ar, ag, ab, mx, my);
    draw_btn(x+138, y, 160, BTN_H, "CLOSE PROJECT", false, ar, ag, ab, mx, my);
    y += BTN_H + GAP*2;

    char cur[128];
    snprintf(cur, sizeof cur, "PROJECT: %s", proj->name);
    text_draw(x, y, SCALE_SM, 0.40f, 0.40f, 0.45f, 1, cur);
}

/* ---------------------------------------------------------------------
   Public API */

void settings_tab_init(SettingsTab*st, const Project*proj, EditorSettings *settings){
    memset(st,0,sizeof(*st));
    st->settings = settings;
    st->active_section = SETTINGS_SEC_FIRST;
    textinput_init(&st->fi_name,  PROJECT_NAME_MAX-1,false);
    textinput_init(&st->fi_grid_w,3,true);
    textinput_init(&st->fi_grid_h,3,true);
    textinput_init(&st->fi_tile_w,3,true);
    textinput_init(&st->fi_tile_h,3,true);
    char buf[16];
    textinput_set(&st->fi_name,proj->name);
    snprintf(buf,sizeof buf,"%d",proj->grid_w); textinput_set(&st->fi_grid_w,buf);
    snprintf(buf,sizeof buf,"%d",proj->grid_h); textinput_set(&st->fi_grid_h,buf);
    snprintf(buf,sizeof buf,"%d",proj->tile_w); textinput_set(&st->fi_tile_w,buf);
    snprintf(buf,sizeof buf,"%d",proj->tile_h); textinput_set(&st->fi_tile_h,buf);
    st->pending_grid_w=proj->grid_w; st->pending_grid_h=proj->grid_h;
    st->pending_tile_w=proj->tile_w; st->pending_tile_h=proj->tile_h;
}

bool settings_tab_update(SettingsTab*st,Project*proj,int vw,int vh){
    (void)vw;(void)vh;
    int mx,my; input_mouse_pos(&mx,&my);
    bool saved=false;

    /* Left rail clicks — switch active_section. Always live, regardless
       of which section is currently shown. */
    for (int i = 0; i < SETTINGS_SEC_COUNT; i++) {
        float bx, by, bw, bh;
        rail_button_rect(i, &bx, &by, &bw, &bh);
        if (hittest(bx, by, bw, bh, mx, my) &&
            input_mouse_button_pressed(SDL_BUTTON_LEFT)) {
            st->active_section = (SettingsSection)i;
        }
    }

    /* Only the active section's controls are live — sections you can't
       see can't be clicked through, which was never possible in the
       single-column layout anyway since everything was always visible,
       but is now an explicit property of the design rather than an
       accident of "whatever's on screen". */
    switch (st->active_section) {
        case SETTINGS_SEC_GENERAL:      update_general(st, proj, mx, my); break;
        case SETTINGS_SEC_APPEARANCE:   update_appearance(st, mx, my); break;
        case SETTINGS_SEC_DISPLAY:      update_display(st, mx, my); break;
        case SETTINGS_SEC_WORLD_SHAPE:  update_world_shape(st, mx, my); break;
        case SETTINGS_SEC_PROJECT_FILE: update_project_file(st, proj, mx, my, &saved); break;
        default: break;
    }

    return saved;
}

void settings_tab_render(const SettingsTab*st,const Project*proj,int vw,int vh){
    renderer_draw_quad(0,CONTENT_Y,(float)vw,(float)vh-CONTENT_Y,0.08f,0.08f,0.10f,1);
    int mx,my; input_mouse_pos(&mx,&my);

    EditorSettings *s = st->settings; (void)s;
    const Theme *th_ = theme_current();
    float ar = th_->accent_r, ag = th_->accent_g, ab = th_->accent_b;

    /* --- Left rail --- */
    renderer_draw_quad(0.0f, CONTENT_Y, RAIL_W, (float)vh - CONTENT_Y, 0.06f, 0.06f, 0.07f, 1.0f);
    renderer_draw_quad(RAIL_W - 1.0f, CONTENT_Y, 1.0f, (float)vh - CONTENT_Y, 0.20f, 0.20f, 0.24f, 1.0f);

    for (int i = 0; i < SETTINGS_SEC_COUNT; i++) {
        float bx, by, bw, bh;
        rail_button_rect(i, &bx, &by, &bw, &bh);
        bool active = (st->active_section == (SettingsSection)i);
        bool hover  = hittest(bx, by, bw, bh, mx, my);

        float bg = active ? 0.16f : (hover ? 0.11f : 0.06f);
        float gg = active ? ag*0.55f : (hover ? 0.13f : 0.06f);
        float bb = active ? 0.10f : (hover ? 0.13f : 0.07f);
        renderer_draw_quad(bx, by, bw - 4.0f, bh, bg, gg, bb, 1.0f);

        if (active)
            renderer_draw_quad(bx, by, 3.0f, bh, ar, ag, ab, 1.0f);

        float tw = text_measure_width(SECTION_TITLES[i], SCALE_SM);
        float th = text_line_height(SCALE_SM);
        float tx = bx + RAIL_PAD + (active ? 4.0f : 0.0f);
        float ty = by + (bh - th) * 0.5f;
        (void)tw;
        if (active)
            text_draw(tx, ty, SCALE_SM, 1.0f, 1.0f, 1.0f, 1.0f, SECTION_TITLES[i]);
        else
            text_draw(tx, ty, SCALE_SM, 0.55f, 0.55f, 0.58f, 1.0f, SECTION_TITLES[i]);
    }

    /* --- Detail pane (only the active section) --- */
    switch (st->active_section) {
        case SETTINGS_SEC_GENERAL:      render_general(st, proj, mx, my); break;
        case SETTINGS_SEC_APPEARANCE:   render_appearance(st, mx, my); break;
        case SETTINGS_SEC_DISPLAY:      render_display(st, mx, my); break;
        case SETTINGS_SEC_WORLD_SHAPE:  render_world_shape(st, mx, my); break;
        case SETTINGS_SEC_PROJECT_FILE: render_project_file(st, proj, mx, my); break;
        default: break;
    }

    /* Status message — fixed position, bottom of the detail pane,
       visible regardless of which section is active. */
    if (st->status[0])
        text_draw(detail_pane_x(), (float)vh-PAD-text_line_height(SCALE_SM),
                  SCALE_SM, ar, ag, ab, 1, st->status);
}

/* ---------------------------------------------------------------------
   Persistence — plain "key=value" text, same style as core/project.c's
   project.dge so anyone debugging one knows how to read the other.
   Lives outside any project folder since editor settings (theme,
   panel mode, display toggles) are a per-machine preference, not
   per-project data. */

#ifdef _WIN32
#  define DGE_SETTINGS_PATH_SEP "\\"
#else
#  define DGE_SETTINGS_PATH_SEP "/"
#endif

static const char *editor_settings_path(void) {
    static char buf[512];
    if (buf[0]) return buf;

    const char *home = getenv("HOME");
#ifdef _WIN32
    if (!home) home = getenv("USERPROFILE");
#endif
    if (home)
        snprintf(buf, sizeof buf, "%s" DGE_SETTINGS_PATH_SEP ".dgengine_editor.cfg", home);
    else
        snprintf(buf, sizeof buf, ".dgengine_editor.cfg");
    return buf;
}

bool editor_settings_save(const EditorSettings *s) {
    FILE *f = fopen(editor_settings_path(), "w");
    if (!f) return false;
    fprintf(f, "theme=%d\n",             (int)s->theme);
    fprintf(f, "theme_path=%s\n",        s->theme_path);
    fprintf(f, "panel_mode=%d\n",        (int)s->panel_mode);
    fprintf(f, "show_fps=%d\n",          s->show_fps ? 1 : 0);
    fprintf(f, "show_tile_coords=%d\n",  s->show_tile_coords ? 1 : 0);
    fprintf(f, "show_minimap=%d\n",      s->show_minimap ? 1 : 0);
    fprintf(f, "show_grid_overlay=%d\n", s->show_grid_overlay ? 1 : 0);
    fprintf(f, "world_shape_active=%d\n", s->world_shape_active ? 1 : 0);
    fprintf(f, "float_panel_x=%f\n",     (double)s->float_panel_x);
    fprintf(f, "float_panel_y=%f\n",     (double)s->float_panel_y);
    fclose(f);
    return true;
}

bool editor_settings_load(EditorSettings *s) {
    FILE *f = fopen(editor_settings_path(), "r");
    if (!f) return false;

    char line[128];
    while (fgets(line, sizeof line, f)) {
        size_t n = strlen(line);
        while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = '\0';
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = line;
        const char *val = eq + 1;

        if      (strcmp(key, "theme") == 0) {
            int v = atoi(val);
            if (v >= 0 && v < EDITOR_THEME_COUNT) s->theme = (EditorTheme)v;
        }
        else if (strcmp(key, "theme_path") == 0) {
            strncpy(s->theme_path, val, sizeof s->theme_path - 1);
        }
        else if (strcmp(key, "panel_mode") == 0) {
            int v = atoi(val);
            s->panel_mode = (v == WORLD_PANEL_FLOATING) ? WORLD_PANEL_FLOATING : WORLD_PANEL_DOCKED;
        }
        else if (strcmp(key, "show_fps") == 0)           s->show_fps = atoi(val) != 0;
        else if (strcmp(key, "show_tile_coords") == 0)   s->show_tile_coords = atoi(val) != 0;
        else if (strcmp(key, "show_minimap") == 0)       s->show_minimap = atoi(val) != 0;
        else if (strcmp(key, "show_grid_overlay") == 0)  s->show_grid_overlay = atoi(val) != 0;
        else if (strcmp(key, "world_shape_active") == 0) s->world_shape_active = atoi(val) != 0;
        else if (strcmp(key, "float_panel_x") == 0)      s->float_panel_x = (float)atof(val);
        else if (strcmp(key, "float_panel_y") == 0)      s->float_panel_y = (float)atof(val);
    }
    fclose(f);

    s->dragging_panel = false;
    s->drag_off_x = 0.0f;
    s->drag_off_y = 0.0f;
    return true;
}
