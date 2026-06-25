#include "panel.h"

#include <stdio.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "../platform/input.h"
#include "../renderer/renderer.h"
#include "../simulation/weather.h"
#include "text.h"
#include "../game/prefabs.h"
#include "../simulation/construction.h"
#include "../core/log.h"

/* ---------------------------------------------------------------------
   A "button" here is just a screen-space rect plus a label — there's
   no widget tree, no retained hierarchy, nothing to register/unregister.
   Every frame panel_update() and panel_render() each rebuild the same
   layout from scratch (immediate-mode, same philosophy as renderer.c's
   batch quads). The only state that survives between frames is the two
   pending canvas-size ints and the visibility flag in Panel. */

typedef struct {
    int x, y, w, h;
} Rect;

static bool rect_contains(Rect r, int px, int py) {
    return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
}

/* Small pill-shaped tab, vertically centered on the screen edge — sits
   flush against the panel's right edge when open (so it reads as part
   of the sidebar), or against the screen's left edge when closed (so
   it's still reachable with the sidebar gone). Same rect is used for
   both hit-testing (panel_update) and drawing (panel_render). */
#define TOGGLE_TAB_W 16
#define TOGGLE_TAB_H 56
static Rect toggle_tab_rect(const Panel *p, int viewport_h) {
    int x = p->visible ? PANEL_WIDTH : 0;
    int y = 34 + (viewport_h - 34) / 2 - TOGGLE_TAB_H / 2;
    return (Rect){ x, y, TOGGLE_TAB_W, TOGGLE_TAB_H };
}

static bool point_in_panel(const Panel *p, int viewport_h, int px, int py) {
    if (py < 34) return false;
    Rect tab = toggle_tab_rect(p, viewport_h);
    if (rect_contains(tab, px, py)) return true;
    if (!p->visible) return false;
    Rect panel_rect = { 0, 34, PANEL_WIDTH, viewport_h - 34 };
    return rect_contains(panel_rect, px, py);
}

/* Shrinks scale (down to a 1.0 floor) until str fits max_w; if it still
   doesn't fit at the floor, truncates with a trailing ".." instead of
   letting it run past the button's edge. This is the fix for labels
   that are themselves dynamic — building name + cost, "Selected: #N"
   — and can't be pre-wrapped by hand the way a fixed label like
   "REGENERATE" can be sized to fit once and left alone. Returns the
   scale actually used, so the caller can vertically center against
   the real glyph height instead of the originally-requested one. */
static float fit_label(char *buf, size_t bufsize, const char *src, float scale, float max_w) {
    snprintf(buf, bufsize, "%s", src);
    if (text_measure_width(buf, scale) <= max_w) return scale;

    float shrunk = scale * (max_w / text_measure_width(buf, scale));
    if (shrunk < 1.0f) shrunk = 1.0f;
    if (text_measure_width(buf, shrunk) <= max_w) return shrunk;

    /* Even at the floor scale it overflows — truncate with ".." instead. */
    size_t len = strlen(buf);
    while (len > 1 && text_measure_width(buf, 1.0f) > max_w) {
        len--;
        buf[len] = '\0';
        if (len > 2) { buf[len - 1] = '.'; buf[len - 2] = '.'; }
    }
    return 1.0f;
}

/* Shared button draw: filled rect + border (brighter if active) +
   left-padded, width-fitted label. Hit-testing is a separate pass in
   panel_update() using the same Rect math so the clickable area and
   the drawn area can never drift apart. */
static void draw_button(Rect r, const char *label, bool active, bool enabled) {
    float bg_r = active ? 0.30f : 0.16f;
    float bg_g = active ? 0.45f : 0.16f;
    float bg_b = active ? 0.30f : 0.18f;
    if (!enabled) { bg_r = 0.14f; bg_g = 0.14f; bg_b = 0.14f; }

    renderer_draw_quad((float)r.x, (float)r.y, (float)r.w, (float)r.h,
                        bg_r, bg_g, bg_b, 0.92f);

    /* 1px border so adjacent buttons read as separate controls rather
       than a single painted strip. */
    float br = active ? 0.55f : 0.32f;
    renderer_draw_quad((float)r.x, (float)r.y, (float)r.w, 1.0f, br, br, br, 1.0f);
    renderer_draw_quad((float)r.x, (float)(r.y + r.h - 1), (float)r.w, 1.0f, br, br, br, 1.0f);

    char fit[64];
    float max_w = (float)r.w - 16.0f; /* 8px padding each side */
    float scale = fit_label(fit, sizeof(fit), label, 1.5f, max_w);
    float text_h = text_line_height(scale);
    float ty = (float)r.y + ((float)r.h - text_h) * 0.5f;
    float fg = enabled ? 1.0f : 0.5f;
    text_draw((float)r.x + 8.0f, ty, scale, fg, fg, fg, 1.0f, fit);
}

/* Small colored swatch used for terrain brushes — the color itself
   doubles as the preview, same idea as tile_base_color() already
   driving the world's own rendering, so the swatch matches what
   painting that brush will actually look like. */
static void draw_swatch_button(Rect r, const char *label, TileColor color, bool active) {
    renderer_draw_quad((float)r.x, (float)r.y, (float)r.w, (float)r.h,
                        active ? 0.30f : 0.16f, active ? 0.30f : 0.16f, active ? 0.30f : 0.16f, 0.92f);
    float br = active ? 0.55f : 0.32f;
    renderer_draw_quad((float)r.x, (float)r.y, (float)r.w, 1.0f, br, br, br, 1.0f);
    renderer_draw_quad((float)r.x, (float)(r.y + r.h - 1), (float)r.w, 1.0f, br, br, br, 1.0f);

    /* Swatch square on the left, label to its right. */
    float sw = (float)r.h - 8.0f;
    renderer_draw_quad((float)r.x + 6.0f, (float)r.y + 4.0f, sw, sw,
                        color.r, color.g, color.b, 1.0f);

    char fit[32];
    float text_x = (float)r.x + 12.0f + sw;
    float max_w = (float)(r.x + r.w) - text_x - 6.0f;
    float scale = fit_label(fit, sizeof(fit), label, 1.5f, max_w);
    float text_h = text_line_height(scale);
    float ty = (float)r.y + ((float)r.h - text_h) * 0.5f;
    text_draw(text_x, ty, scale, 1.0f, 1.0f, 1.0f, 1.0f, fit);
}

static void draw_toggle_tab(const Panel *p, int viewport_h) {
    Rect r = toggle_tab_rect(p, viewport_h);
    renderer_draw_quad((float)r.x, (float)r.y, (float)r.w, (float)r.h,
                        0.20f, 0.20f, 0.23f, 0.96f);
    float br = 0.45f;
    renderer_draw_quad((float)r.x, (float)r.y, (float)r.w, 1.0f, br, br, br, 1.0f);
    renderer_draw_quad((float)r.x, (float)(r.y + r.h - 1), (float)r.w, 1.0f, br, br, br, 1.0f);
    renderer_draw_quad((float)r.x, (float)r.y, 1.0f, (float)r.h, br, br, br, 1.0f);
    renderer_draw_quad((float)(r.x + r.w - 1), (float)r.y, 1.0f, (float)r.h, br, br, br, 1.0f);

    /* "<" when open (click to collapse), ">" when closed (click to
       reopen) — drawn as a single glyph rather than text_draw's normal
       string path since one character needs no width-fitting logic. */
    const char *glyph = p->visible ? "<" : ">";
    float scale = 1.6f;
    float tw = text_measure_width(glyph, scale);
    float th = text_line_height(scale);
    text_draw((float)r.x + ((float)r.w - tw) * 0.5f,
              (float)r.y + ((float)r.h - th) * 0.5f,
              scale, 0.85f, 0.9f, 1.0f, 1.0f, glyph);
}

void panel_init(Panel *p, int world_w, int world_h) {
    p->pending_w = world_w;
    p->pending_h = world_h;
    p->visible   = true;
}

int panel_effective_width(const Panel *p) {
    return p->visible ? PANEL_WIDTH : 0;
}

bool panel_update(Panel *p, Editor *ed, ResourceStore *resources,
                   WeatherSystem *weather,
                   ObjectDefRegistry *obj_registry, SpritesTab *sprites_tab,
                   int viewport_w, int viewport_h, PanelAction *out_action) {
    (void)viewport_w;
    out_action->type = PANEL_ACTION_NONE;

    /* Keyboard toggle — works regardless of mouse position, so the
       panel can be brought back even if it was hidden mid-pan with the
       cursor somewhere over the world. */
    if (input_key_pressed(SDL_SCANCODE_GRAVE)) {
        p->visible = !p->visible;
        LOG_INFO("Sidebar panel -> %s (` key)", p->visible ? "shown" : "hidden");
    }

    int mx, my;
    input_mouse_pos(&mx, &my);
    bool over_panel = point_in_panel(p, viewport_h, mx, my);
    bool clicked = input_mouse_button_pressed(SDL_BUTTON_LEFT);

    if (!clicked) return over_panel;

    /* Toggle tab is interactive whether the panel is open or closed —
       check it before anything else so it always wins a click. */
    if (rect_contains(toggle_tab_rect(p, viewport_h), mx, my)) {
        p->visible = !p->visible;
        LOG_INFO("Sidebar panel -> %s (tab click)", p->visible ? "shown" : "hidden");
        return true;
    }

    if (!p->visible) return false; /* nothing else to hit while hidden */

    int y = 34 + 28;
    const int margin = 8;
    const int btn_h = 28;
    const int gap = 4;
    const int btn_w = PANEL_WIDTH - margin * 2;

    /* --- Mode buttons --- */
    static const EditorMode modes[] = { EDITOR_MODE_PAINT, EDITOR_MODE_PLACE, EDITOR_MODE_SELECT };
    for (int i = 0; i < 3; i++) {
        Rect r = { margin, y, btn_w, btn_h };
        if (rect_contains(r, mx, my)) {
            ed->mode = modes[i];
            LOG_INFO("Editor mode -> %s (panel)", editor_mode_name(ed->mode));
            return true;
        }
        y += btn_h + gap;
    }
    y += 10;

    /* --- Context section, mirrors editor.c's per-mode key handling --- */
    if (ed->mode == EDITOR_MODE_PAINT) {
        for (int i = 0; i < TERRAIN_COUNT; i++) {
            Rect r = { margin, y, btn_w, btn_h };
            if (rect_contains(r, mx, my)) {
                ed->brush = (TerrainType)i;
                LOG_INFO("Brush -> %s (panel)", terrain_name(ed->brush));
                return true;
            }
            y += btn_h + gap;
        }
    } else if (ed->mode == EDITOR_MODE_PLACE) {
        static const PrefabKind prefabs[] = { PREFAB_TREE, PREFAB_ROCK, PREFAB_WORKER };
        for (int i = 0; i < 3; i++) {
            Rect r = { margin, y, btn_w, btn_h };
            if (rect_contains(r, mx, my)) {
                ed->prefab = prefabs[i];
                ed->placing_building = false;
                LOG_INFO("Prefab -> %s (panel)", prefab_name(ed->prefab));
                return true;
            }
            y += btn_h + gap;
        }
        Rect r = { margin, y, btn_w, btn_h };
        if (rect_contains(r, mx, my)) {
            ed->building = BUILDING_CAMPFIRE;
            ed->placing_building = true;
            ed->placing_custom = false;
            LOG_INFO("Blueprint -> %s (panel)", building_name(ed->building));
            return true;
        }
        y += btn_h + gap;

        /* --- Custom objects (Phase L->World) ---
           One button per ObjectDef the player has actually defined in
           the Objects tab — this list is empty (and draws nothing) on
           a fresh project, which is the honest state: there's nothing
           to place until something has been defined. */
        for (int i = 0; i < obj_registry->count; i++) {
            const ObjectDef *def = &obj_registry->defs[i];
            Rect cr = { margin, y, btn_w, btn_h };
            if (rect_contains(cr, mx, my)) {
                ed->placing_building = false;
                ed->placing_custom   = true;
                snprintf(ed->place_def_name, sizeof(ed->place_def_name), "%s", def->name);
                ed->place_sprite_id = sprites_tab_find_id(sprites_tab, def->sprite);
                LOG_INFO("Object -> %s (panel)", def->name);
                return true;
            }
            y += btn_h + gap;
        }
    } else {
        /* SELECT — informational only, no buttons yet; see render(). */
    }
    y += 18;

    /* --- World section --- */
    Rect new_r   = { margin, y, btn_w, btn_h };          y += btn_h + gap;
    Rect regen_r = { margin, y, btn_w, btn_h };          y += btn_h + gap;
    y += 10;

    /* Canvas size row: [-] [  64  ] [+] for width, same for height,
       then an Apply button that actually resizes the World. Splitting
       "edit pending size" from "commit" means clicking +/- repeatedly
       can't thrash world_resize()'s reallocation on every click. */
    Rect w_minus = { margin, y, 28, btn_h };
    Rect w_plus  = { margin + btn_w - 28, y, 28, btn_h };
    if (rect_contains(w_minus, mx, my)) { if (p->pending_w > 8)   p->pending_w -= 8; return true; }
    if (rect_contains(w_plus,  mx, my)) { if (p->pending_w < 256) p->pending_w += 8; return true; }
    y += btn_h + gap;

    Rect h_minus = { margin, y, 28, btn_h };
    Rect h_plus  = { margin + btn_w - 28, y, 28, btn_h };
    if (rect_contains(h_minus, mx, my)) { if (p->pending_h > 8)   p->pending_h -= 8; return true; }
    if (rect_contains(h_plus,  mx, my)) { if (p->pending_h < 256) p->pending_h += 8; return true; }
    y += btn_h + gap;

    Rect apply_r = { margin, y, btn_w, btn_h }; y += btn_h + gap;
    y += 10;
    Rect save_r = { margin, y, btn_w, btn_h }; y += btn_h + gap;
    Rect load_r = { margin, y, btn_w, btn_h }; y += btn_h + gap;

    if (rect_contains(new_r, mx, my))   { out_action->type = PANEL_ACTION_NEW;        return true; }
    if (rect_contains(regen_r, mx, my)) { out_action->type = PANEL_ACTION_REGENERATE; return true; }
    if (rect_contains(apply_r, mx, my)) { out_action->type = PANEL_ACTION_RESIZE;     return true; }
    if (rect_contains(save_r, mx, my))  { out_action->type = PANEL_ACTION_SAVE;       return true; }
    if (rect_contains(load_r, mx, my))  { out_action->type = PANEL_ACTION_LOAD;       return true; }

    /* Weather section */
    y += 10;
    /* Toggle auto-cycle button */
    Rect wx_toggle = { margin, y, btn_w, btn_h }; y += btn_h + gap;
    /* Weather type buttons: NONE / SUNNY / RAIN / SNOW */
    Rect wx_types[4];
    int half_w2 = (btn_w - gap) / 2;
    wx_types[0] = (Rect){ margin,              y, half_w2, btn_h };
    wx_types[1] = (Rect){ margin + half_w2 + gap, y, half_w2, btn_h };
    y += btn_h + gap;
    wx_types[2] = (Rect){ margin,              y, half_w2, btn_h };
    wx_types[3] = (Rect){ margin + half_w2 + gap, y, half_w2, btn_h };

    if (rect_contains(wx_toggle, mx, my)) {
        out_action->type = PANEL_ACTION_WEATHER_TOGGLE;
        return true;
    }
    static const WeatherType wx_vals[4] = {
        WEATHER_NONE, WEATHER_SUNNY, WEATHER_RAIN, WEATHER_SNOW
    };
    for (int i = 0; i < 4; i++) {
        if (rect_contains(wx_types[i], mx, my)) {
            out_action->type = PANEL_ACTION_WEATHER_SET;
            out_action->weather_type = (int)wx_vals[i];
            return true;
        }
    }

    (void)resources;
    (void)weather;
    return over_panel;
}

void panel_render(const Panel *p, const Editor *ed, const ResourceStore *resources,
                   const WeatherSystem *weather, const ObjectDefRegistry *obj_registry,
                   int viewport_w, int viewport_h) {
    (void)viewport_w;
    (void)resources;

    draw_toggle_tab(p, viewport_h);
    if (!p->visible) return;

    /* Sidebar background, starts below tab bar, drawn first so everything else
       layers on top of it. */
    renderer_draw_quad(0.0f, 34.0f, (float)PANEL_WIDTH, (float)viewport_h - 34.0f,
                        0.10f, 0.10f, 0.12f, 0.96f);
    /* 1px separating line on the right edge. */
    renderer_draw_quad((float)PANEL_WIDTH - 1.0f, 34.0f, 1.0f, (float)viewport_h - 34.0f,
                        0.4f, 0.4f, 0.42f, 1.0f);

    text_draw(8.0f, 34.0f + 6.0f, 1.4f, 0.45f, 0.50f, 0.55f, 1.0f, "WORLD EDITOR");

    int y = 34 + 28;
    const int margin = 8;
    const int btn_h = 28;
    const int gap = 4;
    const int btn_w = PANEL_WIDTH - margin * 2;

    static const EditorMode modes[] = { EDITOR_MODE_PAINT, EDITOR_MODE_PLACE, EDITOR_MODE_SELECT };
    for (int i = 0; i < 3; i++) {
        Rect r = { margin, y, btn_w, btn_h };
        draw_button(r, editor_mode_name(modes[i]), ed->mode == modes[i], true);
        y += btn_h + gap;
    }
    y += 10;

    if (ed->mode == EDITOR_MODE_PAINT) {
        for (int i = 0; i < TERRAIN_COUNT; i++) {
            Rect r = { margin, y, btn_w, btn_h };
            draw_swatch_button(r, terrain_name((TerrainType)i),
                                tile_base_color((TerrainType)i), ed->brush == (TerrainType)i);
            y += btn_h + gap;
        }
    } else if (ed->mode == EDITOR_MODE_PLACE) {
        static const PrefabKind prefabs[] = { PREFAB_TREE, PREFAB_ROCK, PREFAB_WORKER };
        for (int i = 0; i < 3; i++) {
            Rect r = { margin, y, btn_w, btn_h };
            bool active = !ed->placing_building && !ed->placing_custom && ed->prefab == prefabs[i];
            draw_button(r, prefab_name(prefabs[i]), active, true);
            y += btn_h + gap;
        }
        Rect r = { margin, y, btn_w, btn_h };
        char label[64];
        snprintf(label, sizeof(label), "%s (%d %s)", building_name(BUILDING_CAMPFIRE),
                  building_cost_amount(BUILDING_CAMPFIRE),
                  building_cost_kind(BUILDING_CAMPFIRE) == RESOURCE_WOOD ? "wood" : "stone");
        draw_button(r, label, ed->placing_building, true);
        y += btn_h + gap;

        for (int i = 0; i < obj_registry->count; i++) {
            const ObjectDef *def = &obj_registry->defs[i];
            Rect cr = { margin, y, btn_w, btn_h };
            bool active = ed->placing_custom && strcmp(ed->place_def_name, def->name) == 0;
            draw_button(cr, def->name, active, true);
            y += btn_h + gap;
        }
        if (obj_registry->count == 0) {
            char fit[64];
            float scale = fit_label(fit, sizeof(fit), "No objects defined yet -- see Objects tab",
                                     1.1f, (float)btn_w);
            text_draw((float)margin, (float)y, scale, 0.5f, 0.5f, 0.55f, 1.0f, fit);
            y += 18;
        }
    } else {
        char label[64];
        if (ed->selected.index != ENTITY_NULL)
            snprintf(label, sizeof(label), "Selected: #%u", ed->selected.index);
        else
            snprintf(label, sizeof(label), "Nothing selected");
        char fit[64];
        float scale = fit_label(fit, sizeof(fit), label, 1.4f, (float)btn_w);
        text_draw((float)margin, (float)y, scale, 0.8f, 0.8f, 0.8f, 1.0f, fit);
        y += 20;
        char fit2[64];
        float scale2 = fit_label(fit2, sizeof(fit2), "H harvest  Del delete  M command", 1.2f, (float)btn_w);
        text_draw((float)margin, (float)y, scale2, 0.6f, 0.6f, 0.6f, 1.0f, fit2);
    }
    y += 18;

    Rect new_r   = { margin, y, btn_w, btn_h };          y += btn_h + gap;
    Rect regen_r = { margin, y, btn_w, btn_h };          y += btn_h + gap;
    draw_button(new_r,   "New (clear)",   false, true);
    draw_button(regen_r, "Regenerate",    false, true);
    y += 10;

    {
        char fit[32];
        float scale = fit_label(fit, sizeof(fit), "Canvas size", 1.2f, (float)btn_w);
        text_draw((float)margin, (float)y, scale, 0.6f, 0.6f, 0.6f, 1.0f, fit);
    }
    y += 16;

    Rect w_minus = { margin, y, 28, btn_h };
    Rect w_plus  = { margin + btn_w - 28, y, 28, btn_h };
    draw_button(w_minus, "-", false, true);
    draw_button(w_plus,  "+", false, true);
    {
        char wbuf[32];
        snprintf(wbuf, sizeof(wbuf), "W: %d", p->pending_w);
        char fit[32];
        float max_w = (float)(w_plus.x - w_minus.x - w_minus.w) - 4.0f;
        float scale = fit_label(fit, sizeof(fit), wbuf, 1.4f, max_w);
        float tw = text_measure_width(fit, scale);
        text_draw((float)margin + (float)btn_w * 0.5f - tw * 0.5f,
                  (float)y + ((float)btn_h - text_line_height(scale)) * 0.5f,
                  scale, 1.0f, 1.0f, 1.0f, 1.0f, fit);
    }
    y += btn_h + gap;

    Rect h_minus = { margin, y, 28, btn_h };
    Rect h_plus  = { margin + btn_w - 28, y, 28, btn_h };
    draw_button(h_minus, "-", false, true);
    draw_button(h_plus,  "+", false, true);
    {
        char hbuf[32];
        snprintf(hbuf, sizeof(hbuf), "H: %d", p->pending_h);
        char fit[32];
        float max_w = (float)(h_plus.x - h_minus.x - h_minus.w) - 4.0f;
        float scale = fit_label(fit, sizeof(fit), hbuf, 1.4f, max_w);
        float tw = text_measure_width(fit, scale);
        text_draw((float)margin + (float)btn_w * 0.5f - tw * 0.5f,
                  (float)y + ((float)btn_h - text_line_height(scale)) * 0.5f,
                  scale, 1.0f, 1.0f, 1.0f, 1.0f, fit);
    }
    y += btn_h + gap;

    Rect apply_r = { margin, y, btn_w, btn_h }; y += btn_h + gap;
    draw_button(apply_r, "Apply resize", false, true);
    y += 10;

    Rect save_r = { margin, y, btn_w, btn_h }; y += btn_h + gap;
    Rect load_r = { margin, y, btn_w, btn_h }; y += btn_h + gap;
    draw_button(save_r, "Save (F5)", false, true);
    draw_button(load_r, "Load (F9)", false, true);

    y += 10;
    /* Weather section label */
    {
        char wlbl[48];
        snprintf(wlbl, sizeof wlbl, "WEATHER");
        char fit[48]; float scale = fit_label(fit, sizeof fit, wlbl, 1.2f, (float)btn_w);
        text_draw((float)margin, (float)y, scale, 0.6f, 0.6f, 0.6f, 1.0f, fit);
    }
    y += 16;

    /* Toggle auto-cycle button */
    {
        Rect wx_toggle = { margin, y, btn_w, btn_h };
        char tog_lbl[48];
        snprintf(tog_lbl, sizeof tog_lbl, "AUTO: %s", weather->enabled ? "ON" : "OFF");
        draw_button(wx_toggle, tog_lbl, weather->enabled, true);
        y += btn_h + gap;
    }

    /* Weather type buttons 2x2 */
    int half_w2 = (btn_w - gap) / 2;
    static const WeatherType wx_vals[4] = {
        WEATHER_NONE, WEATHER_SUNNY, WEATHER_RAIN, WEATHER_SNOW
    };
    static const char *wx_labels[4] = { "NONE", "SUNNY", "RAIN", "SNOW" };
    for (int row = 0; row < 2; row++) {
        for (int col = 0; col < 2; col++) {
            int i = row * 2 + col;
            int bx = margin + col * (half_w2 + gap);
            Rect wr = { bx, y, half_w2, btn_h };
            draw_button(wr, wx_labels[i], weather->type == wx_vals[i], true);
        }
        y += btn_h + gap;
    }

    (void)viewport_w; (void)resources;
}
