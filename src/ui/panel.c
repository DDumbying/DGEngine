#include "panel.h"

#include <stdio.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "../platform/input.h"
#include "../renderer/renderer.h"
#include "../renderer/atlas.h"
#include "../world/world.h"
#include "../world/tileset.h"
#include "../simulation/weather.h"
#include "text.h"
#include "theme.h"
#include "../game/prefabs.h"
#include "../simulation/construction.h"
#include "../core/log.h"

/* ---------------------------------------------------------------------
   A "button" here is just a screen-space rect plus a label — there's
   no widget tree, no retained hierarchy, nothing to register/unregister.
   Every frame panel_update() and panel_render() each rebuild the same
   layout from scratch (immediate-mode, same philosophy as renderer.c's
   batch quads).

   IMPORTANT — the bug this shape caused once already: panel_update()
   (hit-testing) and panel_render() (drawing) are two separate function
   bodies that each recompute the same y-position math independently.
   Nothing enforces that they agree. That's exactly what happened to
   the old PLACE-mode sprite picker: render() added an extra label's
   worth of y-offset before laying out thumbnails, update() didn't, and
   every click landed 14px off from what was drawn. The fix isn't just
   patching that one offset — it's a standing risk for every scrollable
   list in this file. Where it matters most (the Tileset palette,
   PLACE's sprite picker), row geometry is pinned to explicit shared
   constants (ROW_H, LIST_TOP, etc.) computed the same way in both
   functions, and scrolled-to-visible rows are kept simple (a rename in
   progress force-scrolls itself to the top of the list rather than
   needing its geometry recomputed from an arbitrary scroll offset) so
   there's less room for the two functions' math to quietly diverge. */

/* --- Layout constants shared between update and render --- */
#define THUMB_SZ   44
#define THUMB_GAP   4
#define THUMB_COLS  2
#define ROW_H      32   /* one Tileset row: 28px button + 4px gap, same
                            rhythm as every other button in this file */

/* Height of the bottom-anchored World/Save/Weather block.
   btn_h=28, gap=4 throughout. */
#define BOTTOM_SECTION_H \
    (18 + (28+4)*2 + 10 + 16 + (28+4)*2 + (28+4) + 10 + (28+4)*2 + 10 + 16 + (28+4) + (28+4)*2)

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
   letting it run past the button's edge. Returns the scale actually
   used, so the caller can vertically center against the real glyph
   height instead of the originally-requested one. */
static float fit_label(char *buf, size_t bufsize, const char *src, float scale, float max_w) {
    snprintf(buf, bufsize, "%s", src);
    if (text_measure_width(buf, scale) <= max_w) return scale;

    float shrunk = scale * (max_w / text_measure_width(buf, scale));
    if (shrunk < 1.0f) shrunk = 1.0f;
    if (text_measure_width(buf, shrunk) <= max_w) return shrunk;

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
    const Theme *th = theme_current();
    float bg_r = active ? th->accent_r * 0.42f : 0.16f;
    float bg_g = active ? th->accent_g * 0.58f : 0.16f;
    float bg_b = active ? th->accent_b * 0.42f : 0.18f;
    if (!enabled) { bg_r = 0.14f; bg_g = 0.14f; bg_b = 0.14f; }

    renderer_draw_quad((float)r.x, (float)r.y, (float)r.w, (float)r.h,
                        bg_r, bg_g, bg_b, 0.92f);

    float br = active ? 0.55f : 0.32f;
    renderer_draw_quad((float)r.x, (float)r.y, (float)r.w, 1.0f, br, br, br, 1.0f);
    renderer_draw_quad((float)r.x, (float)(r.y + r.h - 1), (float)r.w, 1.0f, br, br, br, 1.0f);

    char fit[64];
    float max_w = (float)r.w - 16.0f;
    float scale = fit_label(fit, sizeof(fit), label, 1.5f, max_w);
    float text_h = text_line_height(scale);
    float ty = (float)r.y + ((float)r.h - text_h) * 0.5f;
    float fg = enabled ? 1.0f : 0.5f;
    text_draw((float)r.x + 8.0f, ty, scale, fg, fg, fg, 1.0f, fit);
}

static void draw_box_border(Rect r, float br, float bg, float bb) {
    renderer_draw_quad((float)r.x, (float)r.y, (float)r.w, 1.0f, br, bg, bb, 1.0f);
    renderer_draw_quad((float)r.x, (float)(r.y + r.h - 1), (float)r.w, 1.0f, br, bg, bb, 1.0f);
    renderer_draw_quad((float)r.x, (float)r.y, 1.0f, (float)r.h, br, bg, bb, 1.0f);
    renderer_draw_quad((float)(r.x + r.w - 1), (float)r.y, 1.0f, (float)r.h, br, bg, bb, 1.0f);
}

/* Missing-texture checker — the same "this isn't defined" signal
   world_render() uses for undefined tiles, reused here so a Tileset
   slot with no sprite assigned reads identically in the palette as it
   does out in the actual world. */
static void draw_missing_swatch(Rect r) {
    renderer_draw_quad((float)r.x, (float)r.y, (float)r.w, (float)r.h, 0.85f, 0.10f, 0.85f, 1.0f);
    renderer_draw_quad((float)r.x, (float)r.y, (float)r.w * 0.5f, (float)r.h * 0.5f, 0.0f, 0.0f, 0.0f, 1.0f);
    renderer_draw_quad((float)r.x + (float)r.w * 0.5f, (float)r.y + (float)r.h * 0.5f,
                       (float)r.w * 0.5f, (float)r.h * 0.5f, 0.0f, 0.0f, 0.0f, 1.0f);
}

static void draw_toggle_tab(const Panel *p, int viewport_h) {
    Rect r = toggle_tab_rect(p, viewport_h);
    renderer_draw_quad((float)r.x, (float)r.y, (float)r.w, (float)r.h,
                        0.20f, 0.20f, 0.23f, 0.96f);
    draw_box_border(r, 0.45f, 0.45f, 0.45f);

    const char *glyph = p->visible ? "<" : ">";
    float scale = 1.6f;
    float tw = text_measure_width(glyph, scale);
    float th = text_line_height(scale);
    text_draw((float)r.x + ((float)r.w - tw) * 0.5f,
              (float)r.y + ((float)r.h - th) * 0.5f,
              scale, 0.85f, 0.9f, 1.0f, 1.0f, glyph);
}

void panel_init(Panel *p, int world_w, int world_h) {
    p->pending_w     = world_w;
    p->pending_h     = world_h;
    p->visible       = true;
    p->sprite_scroll = 0;
    p->hovered_sprite = -1;
    p->tileset_scroll = 0;
    p->renaming_slot  = -1;
    textinput_init(&p->rename_field, TILESET_NAME_MAX - 1, false);
    p->assigning_sprite_slot = -1;
    p->assign_sprite_scroll  = 0;
}

int panel_effective_width(const Panel *p) {
    return p->visible ? PANEL_WIDTH : 0;
}

/* ---------------------------------------------------------------------
   Shared sprite-thumbnail grid geometry.

   Both PLACE mode's stamp picker and PAINT mode's per-slot sprite
   assignment show the same kind of thing: a scrollable NxTHUMB_COLS
   grid of atlas thumbnails. This computes one thumbnail's rect from
   (start_x, start_y, scroll, atlas index) — the SAME formula update
   and render both call, instead of each spelling out the arithmetic
   separately and risking exactly the drift that caused the old
   PLACE-mode bug. */
static Rect thumb_rect(int start_x, int start_y, int scroll, int atlas_index) {
    int local = atlas_index - scroll * THUMB_COLS;
    int col = local % THUMB_COLS;
    int row = local / THUMB_COLS;
    return (Rect){
        start_x + col * (THUMB_SZ + THUMB_GAP),
        start_y + row * (THUMB_SZ + THUMB_GAP),
        THUMB_SZ, THUMB_SZ
    };
}

bool panel_update(Panel *p, Editor *ed, ResourceStore *resources,
                   WeatherSystem *weather,
                   ObjectDefRegistry *obj_registry, SpritesTab *sprites_tab,
                   const SpriteAtlas *atlas, World *world, GenreProfile genre,
                   const LevelRegistry *levels,
                   int viewport_w, int viewport_h, PanelAction *out_action) {
    (void)viewport_w;
    (void)sprites_tab;
    out_action->type = PANEL_ACTION_NONE;

    if (!input_keyboard_consumed() && input_key_pressed(SDL_SCANCODE_GRAVE)) {
        p->visible = !p->visible;
        LOG_INFO("Sidebar panel -> %s (` key)", p->visible ? "shown" : "hidden");
    }

    int mx, my;
    input_mouse_pos(&mx, &my);
    bool over_panel = point_in_panel(p, viewport_h, mx, my);
    bool lmb = input_mouse_button_pressed(SDL_BUTTON_LEFT);
    bool rmb = input_mouse_button_pressed(SDL_BUTTON_RIGHT);

    /* Committing/cancelling an in-progress Tileset rename has to run
       even on frames where the click lands outside the panel entirely
       (clicking into the world view should close the rename, same as
       clicking any other "away" target) — so this check happens before
       the early "!clicked" return below, not inside the PAINT branch
       further down. */
    if (p->renaming_slot >= 0 && !input_keyboard_consumed() &&
        input_key_pressed(SDL_SCANCODE_ESCAPE)) {
        textinput_unfocus(&p->rename_field);
        p->renaming_slot = -1;
    }

    if (!lmb && !rmb) return over_panel;

    if (lmb && rect_contains(toggle_tab_rect(p, viewport_h), mx, my)) {
        p->visible = !p->visible;
        LOG_INFO("Sidebar panel -> %s (tab click)", p->visible ? "shown" : "hidden");
        return true;
    }

    if (!p->visible) return false;

    int y = 34 + 28;
    const int margin = 8;
    const int btn_h = 28;
    const int gap = 4;
    const int btn_w = PANEL_WIDTH - margin * 2;

    /* --- Level switcher strip (Phase 3, Part A) ---
       [<] [Level Name (i/N)] [>] [+]  — cycles/adds Levels. main.c
       does the actual save-current/switch-active/load-new sequencing
       (see PANEL_ACTION_LEVEL_PREV/NEXT/ADD's handlers there); this
       panel only ever reports the click, the same "panel reports,
       main.c sequences the I/O" division of labor PANEL_ACTION_SAVE/
       LOAD already established. */
    {
        const int STRIP_H = 24;
        int nav_w = 20;
        int add_w = 20;
        int name_w = btn_w - nav_w * 2 - add_w - 6; /* 3 x 2px gaps */
        Rect prev_r = { margin,                          y, nav_w,  STRIP_H };
        Rect name_r = { margin + nav_w + 2,               y, name_w, STRIP_H };
        Rect next_r = { margin + nav_w + 2 + name_w + 2,  y, nav_w,  STRIP_H };
        Rect add_r  = { margin + btn_w - add_w,           y, add_w,  STRIP_H };
        (void)name_r;

        if (lmb && levels && levels->count > 1 && rect_contains(prev_r, mx, my)) {
            out_action->type = PANEL_ACTION_LEVEL_PREV;
            return true;
        }
        if (lmb && levels && levels->count > 1 && rect_contains(next_r, mx, my)) {
            out_action->type = PANEL_ACTION_LEVEL_NEXT;
            return true;
        }
        if (lmb && rect_contains(add_r, mx, my)) {
            out_action->type = PANEL_ACTION_LEVEL_ADD;
            return true;
        }
        y += STRIP_H + gap;
    }

    /* --- Mode buttons --- */
    static const EditorMode modes[] = { EDITOR_MODE_PAINT, EDITOR_MODE_PLACE, EDITOR_MODE_SELECT, EDITOR_MODE_SHAPE };
    if (lmb) {
        for (int i = 0; i < 4; i++) {
            Rect r = { margin, y, btn_w, btn_h };
            if (rect_contains(r, mx, my)) {
                /* Switching away from PAINT while a rename is open
                   commits it rather than silently discarding — the
                   same "click away = commit" rule as clicking anywhere
                   else outside the field. */
                if (p->renaming_slot >= 0) {
                    tileset_rename(&world->tileset, p->renaming_slot, textinput_get(&p->rename_field));
                    textinput_unfocus(&p->rename_field);
                    p->renaming_slot = -1;
                }
                p->assigning_sprite_slot = -1;
                ed->mode = modes[i];
                LOG_INFO("Editor mode -> %s (panel)", editor_mode_name(ed->mode));
                return true;
            }
            y += btn_h + gap;
        }
    } else {
        y += 4 * (btn_h + gap);
    }
    y += 10;

    /* --- Context section, mirrors editor.c's per-mode key handling --- */
    if (ed->mode == EDITOR_MODE_PAINT) {

        if (p->assigning_sprite_slot >= 0) {
            /* --- Sprite-assignment sub-picker: takes over the content
               area. Same grid geometry/behavior as PLACE mode's stamp
               picker below, via the shared thumb_rect() helper. --- */
            if (lmb) {
                int sdx, sdy; (void)sdx; (void)sdy;
                Rect back_r = { margin, y, btn_w, btn_h };
                if (rect_contains(back_r, mx, my)) {
                    p->assigning_sprite_slot = -1;
                    return true;
                }
            }
            y += btn_h + gap;

            int sdx, sdy;
            input_mouse_scroll(&sdx, &sdy);
            if (sdy) {
                p->assign_sprite_scroll -= sdy;
                if (p->assign_sprite_scroll < 0) p->assign_sprite_scroll = 0;
            }

            int total   = atlas ? atlas->sprite_count : 0;
            int thumb_x = margin + (btn_w - (THUMB_SZ * THUMB_COLS + THUMB_GAP)) / 2;

            if (lmb) {
                for (int i = p->assign_sprite_scroll * THUMB_COLS; i < total; i++) {
                    Rect cr = thumb_rect(thumb_x, y, p->assign_sprite_scroll, i);
                    if (cr.y + THUMB_SZ > viewport_h - (int)BOTTOM_SECTION_H - 10) break;
                    if (rect_contains(cr, mx, my)) {
                        tileset_set_sprite(&world->tileset, p->assigning_sprite_slot, i);
                        LOG_INFO("Tileset: slot %d -> sprite %d", p->assigning_sprite_slot, i);
                        p->assigning_sprite_slot = -1;
                        return true;
                    }
                }
            }
            int max_rows = (total + THUMB_COLS - 1) / THUMB_COLS;
            if (p->assign_sprite_scroll >= max_rows && max_rows > 0)
                p->assign_sprite_scroll = max_rows - 1;

            /* Nothing else in PAINT mode is reachable while this
               sub-picker is open — fall through to the shared bottom
               section below (World/Save/Weather stay clickable). */
        } else {
            /* --- Normal Tileset palette list --- */
            int sdy_scroll = 0;
            { int sdx, sdy; input_mouse_scroll(&sdx, &sdy); sdy_scroll = sdy; }
            if (sdy_scroll && p->renaming_slot < 0) {
                p->tileset_scroll -= sdy_scroll;
                if (p->tileset_scroll < 0) p->tileset_scroll = 0;
            }

            int list_y   = y;
            int total    = world->tileset.count + 1; /* +1 for "+ ADD TILE" */
            int start    = p->tileset_scroll;
            int max_list_y = viewport_h - (int)BOTTOM_SECTION_H - 10;

            /* Drive the rename field's keystrokes every frame it's
               open. Renaming force-scrolls itself to the top of the
               list (see the RMB/"+ ADD TILE" handlers below), so its
               row is always exactly at list_y — no need to search for
               where it scrolled to. Committing (Enter, or clicking
               anywhere outside the row) just clears renaming_slot and
               falls through — the SAME click that commits also gets
               to register on whatever it actually landed on below
               (see the long comment on that loop for why this matters:
               it used to be gated behind "only if we didn't just
               commit", which was the bug that made adding a second
               tile require two clicks). */
            if (p->renaming_slot >= 0) {
                Rect name_r = { margin + btn_h + 4, list_y, btn_w - btn_h - 4, btn_h };
                bool enter = textinput_update(&p->rename_field, (float)name_r.x, (float)name_r.y,
                                              (float)name_r.w, 1.4f);
                if (enter) {
                    tileset_rename(&world->tileset, p->renaming_slot, textinput_get(&p->rename_field));
                    textinput_unfocus(&p->rename_field);
                    p->renaming_slot = -1;
                } else if (lmb) {
                    Rect row_r = { margin, list_y, btn_w, btn_h };
                    if (!rect_contains(row_r, mx, my)) {
                        /* Click landed outside the row entirely (not
                           just outside the text field within it) —
                           commit, same as Enter. */
                        tileset_rename(&world->tileset, p->renaming_slot, textinput_get(&p->rename_field));
                        textinput_unfocus(&p->rename_field);
                        p->renaming_slot = -1;
                    }
                }
            }

            /* NOTE: this used to be `if (lmb && !rename_committed_this_frame)`.
               That guard was the actual bug behind "adding a second tile
               doesn't work" — when a rename was in progress and the user
               clicked "+ ADD TILE" to commit-and-add in one motion, the
               click's first job (landing outside the renaming row, so it
               commits the pending name) consumed the ENTIRE click, and
               this block never got a chance to also process what the
               click actually landed on. The user had to click "+ ADD
               TILE" a second time before anything happened — which reads
               exactly like "my second tile isn't being created" from the
               outside. A commit-by-clicking-away and the click's own
               target action are two different things that both need to
               happen from one physical click; skipping the second because
               the first happened is the bug. Now: the rename commit above
               (if any) has already run and cleared renaming_slot, so this
               loop just runs normally against the same click every time —
               there's no "already used up" click to protect against,
               because rect hit-testing below is naturally exclusive
               (a click can only land on one row's rect regardless of what
               state changed a moment earlier in the same frame). */
            if (lmb) {
                int idx = start;
                int ry  = list_y;
                while (idx < total && ry + btn_h <= max_list_y) {
                    bool is_add_row = (idx == world->tileset.count);
                    bool is_renaming_this_row = (idx == p->renaming_slot);

                    if (is_add_row) {
                        Rect row_r = { margin, ry, btn_w, btn_h };
                        if (rect_contains(row_r, mx, my)) {
                            char default_name[TILESET_NAME_MAX];
                            snprintf(default_name, sizeof default_name, "Tile %d", world->tileset.count + 1);
                            int new_idx = tileset_add_slot(&world->tileset, default_name);
                            if (new_idx >= 0) {
                                ed->brush = new_idx;
                                p->tileset_scroll = new_idx; /* force-scroll: new row lands at list_y */
                                p->renaming_slot  = new_idx;
                                textinput_set(&p->rename_field, default_name);
                                textinput_focus(&p->rename_field);
                                LOG_INFO("Tileset: added slot %d ('%s')", new_idx, default_name);
                            } else {
                                LOG_WARN("Tileset: full (max %d slots)", TILESET_MAX_SLOTS);
                            }
                            return true;
                        }
                    } else if (!is_renaming_this_row) {
                        Rect swatch_r = { margin, ry, btn_h, btn_h };
                        Rect name_r   = { margin + btn_h + 4, ry, btn_w - btn_h - 4, btn_h };
                        if (rect_contains(swatch_r, mx, my)) {
                            p->assigning_sprite_slot = idx;
                            p->assign_sprite_scroll  = 0;
                            return true;
                        }
                        if (rect_contains(name_r, mx, my)) {
                            ed->brush = idx;
                            LOG_INFO("Brush -> %s (panel)", tileset_name_for(&world->tileset, idx));
                            return true;
                        }
                    }
                    ry += ROW_H;
                    idx++;
                }
            }

            if (rmb) {
                int idx = start;
                int ry  = list_y;
                while (idx < world->tileset.count && ry + btn_h <= max_list_y) {
                    Rect row_r = { margin, ry, btn_w, btn_h };
                    if (rect_contains(row_r, mx, my)) {
                        p->tileset_scroll = idx; /* force-scroll: renaming row lands at list_y */
                        p->renaming_slot  = idx;
                        textinput_set(&p->rename_field, tileset_name_for(&world->tileset, idx));
                        textinput_focus(&p->rename_field);
                        return true;
                    }
                    ry += ROW_H;
                    idx++;
                }
            }

            int max_scroll = world->tileset.count; /* can scroll until "+ ADD TILE" is the top row */
            if (p->tileset_scroll > max_scroll) p->tileset_scroll = max_scroll;
        }
    } else if (ed->mode == EDITOR_MODE_PLACE) {
        /* --- ObjectDef list: one row per defined placeable object ---
           Phase 2 (ObjectDef consolidation) replaced the old raw
           atlas-sprite-grid picker (which special-cased four fixed
           slot indices to mean tree/rock/worker/campfire) with a list
           of the project's actual ObjectDefs — same shape as PAINT
           mode's Tileset palette list just above: click a row to
           select it as the current stamp, swatch shows the object's
           real sprite (or a missing-texture checker if its sprite
           name doesn't resolve). No "+ ADD" row here, unlike the
           Tileset list — defining a new object happens in the Objects
           tab, not from this picker; PLACE mode only ever *selects*
           from what's already defined there. */
        int sdy_scroll = 0;
        { int sdx, sdy; input_mouse_scroll(&sdx, &sdy); sdy_scroll = sdy; }
        if (sdy_scroll) {
            p->sprite_scroll -= sdy_scroll;
            if (p->sprite_scroll < 0) p->sprite_scroll = 0;
        }

        int list_y     = y;
        int total       = obj_registry ? obj_registry->count : 0;
        int start       = p->sprite_scroll;
        int max_list_y  = viewport_h - 120;

        if (lmb) {
            int idx = start;
            int ry  = list_y;
            while (idx < total && ry + btn_h <= max_list_y) {
                Rect row_r = { margin, ry, btn_w, btn_h };
                if (rect_contains(row_r, mx, my)) {
                    const ObjectDef *def = &obj_registry->defs[idx];
                    snprintf(ed->place_def_name, sizeof(ed->place_def_name), "%s", def->name);
                    ed->place_sprite_id = sprites_tab ? sprites_tab_find_id(sprites_tab, def->sprite) : SPRITE_NONE;
                    LOG_INFO("Object picker -> '%s' (panel)", def->name);
                    return true;
                }
                ry += ROW_H;
                idx++;
            }
        }
        int max_scroll = total > 0 ? total - 1 : 0;
        if (p->sprite_scroll > max_scroll) p->sprite_scroll = max_scroll;
    } else {
        y += 20;
    }

    /* --- World/Save/Weather: bottom-anchored so they don't collide with
       the sprite picker in PLACE mode. Count upward from viewport bottom. */
    y = viewport_h - BOTTOM_SECTION_H;
    if (y < 300) y = 300;

    Rect new_r   = { margin, y, btn_w, btn_h };          y += btn_h + gap;
    Rect regen_r = { margin, y, btn_w, btn_h };          y += btn_h + gap;
    y += 10;
    y += 16;

    Rect w_minus = { margin, y, 28, btn_h };
    Rect w_plus  = { margin + btn_w - 28, y, 28, btn_h };
    if (lmb && rect_contains(w_minus, mx, my)) { if (p->pending_w > 8)   p->pending_w -= 8; return true; }
    if (lmb && rect_contains(w_plus,  mx, my)) { if (p->pending_w < 256) p->pending_w += 8; return true; }
    y += btn_h + gap;

    Rect h_minus = { margin, y, 28, btn_h };
    Rect h_plus  = { margin + btn_w - 28, y, 28, btn_h };
    if (lmb && rect_contains(h_minus, mx, my)) { if (p->pending_h > 8)   p->pending_h -= 8; return true; }
    if (lmb && rect_contains(h_plus,  mx, my)) { if (p->pending_h < 256) p->pending_h += 8; return true; }
    y += btn_h + gap;

    Rect apply_r = { margin, y, btn_w, btn_h }; y += btn_h + gap;
    y += 10;
    Rect save_r = { margin, y, btn_w, btn_h }; y += btn_h + gap;
    Rect load_r = { margin, y, btn_w, btn_h }; y += btn_h + gap;

    if (lmb && rect_contains(new_r, mx, my))   { out_action->type = PANEL_ACTION_NEW;        return true; }
    if (lmb && rect_contains(regen_r, mx, my)) { out_action->type = PANEL_ACTION_REGENERATE; return true; }
    if (lmb && rect_contains(apply_r, mx, my)) { out_action->type = PANEL_ACTION_RESIZE;     return true; }
    if (lmb && rect_contains(save_r, mx, my))  { out_action->type = PANEL_ACTION_SAVE;       return true; }
    if (lmb && rect_contains(load_r, mx, my))  { out_action->type = PANEL_ACTION_LOAD;       return true; }

    if (genre == GENRE_SANDBOX_SIM) {
        y += 10;
        y += 16;
        Rect wx_toggle = { margin, y, btn_w, btn_h }; y += btn_h + gap;
        Rect wx_types[4];
        int half_w2 = (btn_w - gap) / 2;
        wx_types[0] = (Rect){ margin,              y, half_w2, btn_h };
        wx_types[1] = (Rect){ margin + half_w2 + gap, y, half_w2, btn_h };
        y += btn_h + gap;
        wx_types[2] = (Rect){ margin,              y, half_w2, btn_h };
        wx_types[3] = (Rect){ margin + half_w2 + gap, y, half_w2, btn_h };

        if (lmb && rect_contains(wx_toggle, mx, my)) {
            out_action->type = PANEL_ACTION_WEATHER_TOGGLE;
            return true;
        }
        static const WeatherType wx_vals[4] = {
            WEATHER_NONE, WEATHER_SUNNY, WEATHER_RAIN, WEATHER_SNOW
        };
        if (lmb) {
            for (int i = 0; i < 4; i++) {
                if (rect_contains(wx_types[i], mx, my)) {
                    out_action->type = PANEL_ACTION_WEATHER_SET;
                    out_action->weather_type = (int)wx_vals[i];
                    return true;
                }
            }
        }
    }

    (void)resources;
    (void)weather;
    (void)obj_registry;
    return over_panel;
}

void panel_render(const Panel *p, const Editor *ed, const ResourceStore *resources,
                   const WeatherSystem *weather, const ObjectDefRegistry *obj_registry,
                   const SpriteAtlas *atlas, const SpritesTab *sprites_tab,
                   const World *world, GenreProfile genre, const LevelRegistry *levels,
                   int viewport_w, int viewport_h) {
    (void)viewport_w;
    (void)resources;
    (void)obj_registry;

    draw_toggle_tab(p, viewport_h);
    if (!p->visible) return;

    renderer_draw_quad(0.0f, 34.0f, (float)PANEL_WIDTH, (float)viewport_h - 34.0f,
                        0.10f, 0.10f, 0.12f, 0.96f);
    renderer_draw_quad((float)PANEL_WIDTH - 1.0f, 34.0f, 1.0f, (float)viewport_h - 34.0f,
                        0.4f, 0.4f, 0.42f, 1.0f);

    text_draw(8.0f, 34.0f + 6.0f, 1.4f, 0.45f, 0.50f, 0.55f, 1.0f, "WORLD EDITOR");

    int y = 34 + 28;
    const int margin = 8;
    const int btn_h = 28;
    const int gap = 4;
    const int btn_w = PANEL_WIDTH - margin * 2;

    /* --- Level switcher strip --- must match panel_update()'s geometry
       (STRIP_H, nav_w, add_w, name_w, and every rect's x/y) exactly, or
       this is the same class of click-vs-drawn misalignment bug the
       Tileset/ObjectDef picker rewrites in Phase 1/2 already had to fix
       once. */
    {
        const int STRIP_H = 24;
        int nav_w = 20;
        int add_w = 20;
        int name_w = btn_w - nav_w * 2 - add_w - 6;
        int mx, my; input_mouse_pos(&mx, &my);

        Rect prev_r = { margin,                          y, nav_w,  STRIP_H };
        Rect name_r = { margin + nav_w + 2,               y, name_w, STRIP_H };
        Rect next_r = { margin + nav_w + 2 + name_w + 2,  y, nav_w,  STRIP_H };
        Rect add_r  = { margin + btn_w - add_w,           y, add_w,  STRIP_H };

        int  count  = levels ? levels->count : 0;
        int  active = levels ? levels->active_index : -1;
        bool can_nav = count > 1;

        draw_button(prev_r, "<", false, can_nav);
        draw_button(next_r, ">", false, can_nav);
        draw_button(add_r,  "+", false, true);

        const Theme *th = theme_current();
        renderer_draw_quad((float)name_r.x, (float)name_r.y, (float)name_r.w, (float)name_r.h,
                           0.13f, 0.13f, 0.15f, 0.92f);
        draw_box_border(name_r, th->border_r, th->border_g, th->border_b);

        char label[80];
        if (count == 0) {
            snprintf(label, sizeof label, "(no levels)");
        } else {
            const Level *lv = level_registry_get(levels, active);
            snprintf(label, sizeof label, "%s (%d/%d)", lv ? lv->name : "?", active + 1, count);
        }
        char fit[80];
        float scale = fit_label(fit, sizeof fit, label, 1.15f, (float)name_r.w - 6.0f);
        float th2 = text_line_height(scale);
        float tw = text_measure_width(fit, scale);
        text_draw((float)name_r.x + ((float)name_r.w - tw) * 0.5f,
                  (float)name_r.y + ((float)STRIP_H - th2) * 0.5f,
                  scale, 0.85f, 0.85f, 0.90f, 1.0f, fit);

        (void)mx; (void)my;
        y += STRIP_H + gap;
    }

    static const EditorMode modes[] = { EDITOR_MODE_PAINT, EDITOR_MODE_PLACE, EDITOR_MODE_SELECT, EDITOR_MODE_SHAPE };
    for (int i = 0; i < 4; i++) {
        Rect r = { margin, y, btn_w, btn_h };
        draw_button(r, editor_mode_name(modes[i]), ed->mode == modes[i], true);
        y += btn_h + gap;
    }
    y += 10;

    if (ed->mode == EDITOR_MODE_PAINT) {

        if (p->assigning_sprite_slot >= 0) {
            /* --- Sprite-assignment sub-picker --- */
            char hdr[48];
            snprintf(hdr, sizeof hdr, "SPRITE FOR: %s",
                     tileset_name_for(&world->tileset, p->assigning_sprite_slot));
            char fit[48];
            float scale = fit_label(fit, sizeof fit, hdr, 1.2f, (float)btn_w);
            text_draw((float)margin, (float)y + 2, scale, 0.85f, 0.85f, 0.90f, 1.0f, fit);

            Rect back_r = { margin, y, btn_w, btn_h };
            draw_button(back_r, "< BACK", false, true);
            y += btn_h + gap;

            int total   = atlas ? atlas->sprite_count : 0;
            int thumb_x = margin + (btn_w - (THUMB_SZ * THUMB_COLS + THUMB_GAP)) / 2;
            int mx2, my2; input_mouse_pos(&mx2, &my2);

            if (total == 0) {
                text_draw((float)margin, (float)y, 1.2f, 0.5f, 0.5f, 0.55f, 1.0f, "No atlas loaded");
            } else {
                renderer_bind_texture(atlas->texture.id);
                for (int i = p->assign_sprite_scroll * THUMB_COLS; i < total; i++) {
                    Rect cr = thumb_rect(thumb_x, y, p->assign_sprite_scroll, i);
                    if (cr.y + THUMB_SZ > viewport_h - (int)BOTTOM_SECTION_H - 10) break;
                    bool selected = (tileset_sprite_for(&world->tileset, p->assigning_sprite_slot) == i);
                    bool hover = rect_contains(cr, mx2, my2);
                    renderer_flush_texture();
                    float bg = selected ? 0.22f : (hover ? 0.18f : 0.11f);
                    renderer_draw_quad((float)cr.x, (float)cr.y, (float)cr.w, (float)cr.h, bg, bg, bg, 1.0f);
                    renderer_bind_texture(atlas->texture.id);
                    UVRect uv = atlas_get_uv(atlas, i);
                    float pad = 4.0f;
                    renderer_draw_quad_uv((float)cr.x + pad, (float)cr.y + pad,
                                          (float)cr.w - pad * 2.0f, (float)cr.h - pad * 2.0f,
                                          1.0f, 1.0f, 1.0f, 1.0f, uv.u0, uv.v0, uv.u1, uv.v1);
                }
                renderer_flush_texture();
                for (int i = p->assign_sprite_scroll * THUMB_COLS; i < total; i++) {
                    Rect cr = thumb_rect(thumb_x, y, p->assign_sprite_scroll, i);
                    if (cr.y + THUMB_SZ > viewport_h - (int)BOTTOM_SECTION_H - 10) break;
                    bool selected = (tileset_sprite_for(&world->tileset, p->assigning_sprite_slot) == i);
                    bool hover = rect_contains(cr, mx2, my2);
                    float br = selected ? 0.25f : (hover ? 0.35f : 0.20f);
                    float bg2= selected ? 0.85f : (hover ? 0.55f : 0.24f);
                    float bb = selected ? 0.40f : (hover ? 0.35f : 0.20f);
                    draw_box_border(cr, br, bg2, bb);
                }
            }
        } else {
            /* --- Normal Tileset palette list --- */
            int list_y   = y;
            int total    = world->tileset.count + 1;
            int start    = p->tileset_scroll;
            int max_list_y = viewport_h - (int)BOTTOM_SECTION_H - 10;

            if (world->tileset.count == 0) {
                char fit[64];
                float scale = fit_label(fit, sizeof(fit), "No tiles defined yet", 1.2f, (float)btn_w);
                text_draw((float)margin, (float)list_y, scale, 0.55f, 0.55f, 0.60f, 1.0f, fit);
            }

            int idx = start;
            int ry  = list_y;
            while (idx < total && ry + btn_h <= max_list_y) {
                bool is_add_row = (idx == world->tileset.count);
                bool is_renaming_this_row = (idx == p->renaming_slot);
                Rect row_r    = { margin, ry, btn_w, btn_h };
                Rect swatch_r = { margin, ry, btn_h, btn_h };
                Rect name_r   = { margin + btn_h + 4, ry, btn_w - btn_h - 4, btn_h };

                if (is_add_row) {
                    const Theme *th = theme_current();
                    renderer_draw_quad((float)row_r.x, (float)row_r.y, (float)row_r.w, (float)row_r.h,
                                       0.12f, 0.12f, 0.14f, 0.85f);
                    draw_box_border(row_r, th->accent_r * 0.5f, th->accent_g * 0.5f, th->accent_b * 0.5f);
                    char fit[32];
                    float scale = fit_label(fit, sizeof fit, "+ ADD TILE", 1.3f, (float)btn_w - 12.0f);
                    float th2 = text_line_height(scale);
                    text_draw((float)row_r.x + 8.0f, (float)row_r.y + ((float)btn_h - th2) * 0.5f,
                              scale, th->accent_r, th->accent_g, th->accent_b, 1.0f, fit);
                } else {
                    /* Swatch: real sprite if assigned, missing-texture
                       checker otherwise — same visual language as the
                       actual world so there's no ambiguity about what
                       "undefined" looks like. */
                    int sid = tileset_sprite_for(&world->tileset, idx);
                    if (sid >= 0 && atlas) {
                        renderer_bind_texture(atlas->texture.id);
                        UVRect uv = atlas_get_uv(atlas, sid);
                        renderer_draw_quad_uv((float)swatch_r.x + 1, (float)swatch_r.y + 1,
                                              (float)swatch_r.w - 2, (float)swatch_r.h - 2,
                                              1.0f, 1.0f, 1.0f, 1.0f, uv.u0, uv.v0, uv.u1, uv.v1);
                        renderer_flush_texture();
                        draw_box_border(swatch_r, 0.30f, 0.30f, 0.34f);
                    } else {
                        draw_missing_swatch(swatch_r);
                        draw_box_border(swatch_r, 0.30f, 0.30f, 0.34f);
                    }
                    /* Small walkability indicator, bottom-right corner
                       of the swatch — a filled dot when walkable, an
                       X when not, so the one piece of meaning the
                       engine actually reads from a slot is visible at
                       a glance without opening anything. */
                    bool walkable = world->tileset.slots[idx].walkable;
                    float dot_r = 4.0f;
                    float dx = (float)swatch_r.x + (float)swatch_r.w - dot_r - 2.0f;
                    float dy = (float)swatch_r.y + (float)swatch_r.h - dot_r - 2.0f;
                    if (walkable)
                        renderer_draw_quad(dx, dy, dot_r, dot_r, 0.35f, 0.85f, 0.40f, 1.0f);
                    else
                        renderer_draw_quad(dx, dy, dot_r, dot_r, 0.85f, 0.30f, 0.30f, 1.0f);

                    if (is_renaming_this_row) {
                        float rbg = 0.17f;
                        renderer_draw_quad((float)name_r.x, (float)name_r.y, (float)name_r.w, (float)name_r.h,
                                           rbg, rbg, rbg, 1.0f);
                        draw_box_border(name_r, 0.30f, 0.72f, 0.42f);
                        textinput_render(&p->rename_field, (float)name_r.x + 4, (float)name_r.y + (btn_h - (int)text_line_height(1.4f)) / 2.0f,
                                         1.4f, 1.0f, 1.0f, 1.0f, 1.0f);
                    } else {
                        bool active = (ed->brush == idx);
                        const Theme *th = theme_current();
                        float nr = active ? th->accent_r * 0.30f : 0.14f;
                        float ng = active ? th->accent_g * 0.30f : 0.14f;
                        float nb = active ? th->accent_b * 0.30f : 0.16f;
                        renderer_draw_quad((float)name_r.x, (float)name_r.y, (float)name_r.w, (float)name_r.h,
                                           nr, ng, nb, 0.92f);
                        draw_box_border(name_r, active ? 0.35f : 0.22f, active ? 0.85f : 0.22f, active ? 0.42f : 0.24f);
                        char fit[32];
                        float scale = fit_label(fit, sizeof fit, tileset_name_for(&world->tileset, idx), 1.3f, (float)name_r.w - 8.0f);
                        float th2 = text_line_height(scale);
                        text_draw((float)name_r.x + 4.0f, (float)name_r.y + ((float)btn_h - th2) * 0.5f,
                                  scale, 1.0f, 1.0f, 1.0f, 1.0f, fit);
                    }
                }
                ry += ROW_H;
                idx++;
            }

            if (world->tileset.count > 0) {
                char hint[64];
                snprintf(hint, sizeof hint, "click=paint  RMB=rename  swatch=sprite");
                char fit[64];
                float scale = fit_label(fit, sizeof fit, hint, 1.0f, (float)btn_w);
                if (ry + 12 <= max_list_y)
                    text_draw((float)margin, (float)ry + 2, scale, 0.38f, 0.38f, 0.42f, 1.0f, fit);
            }
        }
    } else if (ed->mode == EDITOR_MODE_PLACE) {
        /* --- ObjectDef list --- mirrors PAINT mode's Tileset palette
           rows (swatch + name, click to select) rather than the old
           raw sprite-atlas thumbnail grid. See panel_update()'s
           matching block for why. */
        int list_y     = y;
        int total       = obj_registry ? obj_registry->count : 0;
        int start       = p->sprite_scroll;
        int max_list_y  = viewport_h - 120;
        int mx2, my2; input_mouse_pos(&mx2, &my2);

        if (total == 0) {
            char fit[64];
            float scale = fit_label(fit, sizeof(fit),
                "No objects defined -- see Objects tab", 1.2f, (float)btn_w);
            text_draw((float)margin, (float)list_y, scale, 0.55f, 0.55f, 0.60f, 1.0f, fit);
        } else {
            int idx = start;
            int ry  = list_y;
            while (idx < total && ry + btn_h <= max_list_y) {
                const ObjectDef *def = &obj_registry->defs[idx];
                Rect row_r    = { margin, ry, btn_w, btn_h };
                Rect swatch_r = { margin, ry, btn_h, btn_h };
                Rect name_r   = { margin + btn_h + 4, ry, btn_w - btn_h - 4, btn_h };

                bool selected = (strcmp(ed->place_def_name, def->name) == 0);
                bool hover    = rect_contains(row_r, mx2, my2);

                int sid = sprites_tab ? sprites_tab_find_id(sprites_tab, def->sprite) : SPRITE_NONE;
                if (sid >= 0 && atlas) {
                    renderer_bind_texture(atlas->texture.id);
                    UVRect uv = atlas_get_uv(atlas, sid);
                    renderer_draw_quad_uv((float)swatch_r.x + 1, (float)swatch_r.y + 1,
                                          (float)swatch_r.w - 2, (float)swatch_r.h - 2,
                                          1.0f, 1.0f, 1.0f, 1.0f, uv.u0, uv.v0, uv.u1, uv.v1);
                    renderer_flush_texture();
                    draw_box_border(swatch_r, 0.30f, 0.30f, 0.34f);
                } else {
                    draw_missing_swatch(swatch_r);
                    draw_box_border(swatch_r, 0.30f, 0.30f, 0.34f);
                }

                /* Small buildable indicator, bottom-right corner of the
                   swatch — same idea as the Tileset walkable dot: the
                   one piece of behavior that changes how placing this
                   object works (instant vs. costed-and-built-over-time)
                   is visible at a glance. */
                if (objdef_is_buildable(def)) {
                    float dot_r = 4.0f;
                    float dx = (float)swatch_r.x + (float)swatch_r.w - dot_r - 2.0f;
                    float dy = (float)swatch_r.y + (float)swatch_r.h - dot_r - 2.0f;
                    renderer_draw_quad(dx, dy, dot_r, dot_r, 0.95f, 0.65f, 0.20f, 1.0f);
                }

                const Theme *th = theme_current();
                float nr = selected ? th->accent_r * 0.30f : (hover ? 0.16f : 0.14f);
                float ng = selected ? th->accent_g * 0.30f : (hover ? 0.16f : 0.14f);
                float nb = selected ? th->accent_b * 0.30f : (hover ? 0.18f : 0.16f);
                renderer_draw_quad((float)name_r.x, (float)name_r.y, (float)name_r.w, (float)name_r.h,
                                   nr, ng, nb, 0.92f);
                draw_box_border(name_r, selected ? 0.35f : 0.22f, selected ? 0.85f : 0.22f, selected ? 0.42f : 0.24f);
                char fit[32];
                float scale = fit_label(fit, sizeof fit, def->name, 1.3f, (float)name_r.w - 8.0f);
                float th2 = text_line_height(scale);
                text_draw((float)name_r.x + 4.0f, (float)name_r.y + ((float)btn_h - th2) * 0.5f,
                          scale, 1.0f, 1.0f, 1.0f, 1.0f, fit);

                ry += ROW_H;
                idx++;
            }

            if (start > 0 || ry < list_y + total * ROW_H) {
                char sc[40];
                snprintf(sc, sizeof sc, "%d / %d objects", start + 1, total);
                if (ry + 12 <= max_list_y)
                    text_draw((float)margin, (float)ry + 2, 1.0f, 0.40f, 0.40f, 0.44f, 1.0f, sc);
            }
        }
    } else if (ed->mode == EDITOR_MODE_SHAPE) {
        char fit[64];
        float scale = fit_label(fit, sizeof(fit),
            world->shape.active ? "Shape mask active" : "Pane will activate it",
            1.4f, (float)btn_w);
        text_draw((float)margin, (float)y, scale, 0.8f, 0.8f, 0.8f, 1.0f, fit);
        y += 20;
        char fit2[64];
        float scale2 = fit_label(fit2, sizeof(fit2),
            "Paint shape in the panel ->", 1.15f, (float)btn_w);
        text_draw((float)margin, (float)y, scale2, 0.6f, 0.85f, 0.6f, 1.0f, fit2);
        y += 16;
        char fit3[64];
        float scale3 = fit_label(fit3, sizeof(fit3),
            "docked to the right edge", 1.15f, (float)btn_w);
        text_draw((float)margin, (float)y, scale3, 0.6f, 0.6f, 0.6f, 1.0f, fit3);
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

    /* World/Save/Weather: bottom-anchored */
    y = viewport_h - BOTTOM_SECTION_H;
    if (y < 300) y = 300;

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

    if (genre == GENRE_SANDBOX_SIM) {
        {
            char wlbl[48];
            snprintf(wlbl, sizeof wlbl, "WEATHER");
            char fit[48]; float scale = fit_label(fit, sizeof fit, wlbl, 1.2f, (float)btn_w);
            text_draw((float)margin, (float)y, scale, 0.6f, 0.6f, 0.6f, 1.0f, fit);
        }
        y += 16;

        {
            Rect wx_toggle = { margin, y, btn_w, btn_h };
            char tog_lbl[48];
            snprintf(tog_lbl, sizeof tog_lbl, "AUTO: %s", weather->enabled ? "ON" : "OFF");
            draw_button(wx_toggle, tog_lbl, weather->enabled, true);
            y += btn_h + gap;
        }

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
    } else {
        char glbl[64];
        snprintf(glbl, sizeof glbl, "GAME TYPE: %s", genre_profile_name(genre));
        char fit[64]; float scale = fit_label(fit, sizeof fit, glbl, 1.2f, (float)btn_w);
        text_draw((float)margin, (float)y, scale, 0.55f, 0.55f, 0.60f, 1.0f, fit);
        y += 16;
        char fit2[80];
        float scale2 = fit_label(fit2, sizeof fit2, genre_profile_desc(genre), 1.0f, (float)btn_w);
        text_draw((float)margin, (float)y, scale2, 0.40f, 0.40f, 0.44f, 1.0f, fit2);
    }

    (void)viewport_w;
}
