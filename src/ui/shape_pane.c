#include "shape_pane.h"

#include <SDL2/SDL.h>
#include <stdio.h>

#include "../platform/input.h"
#include "../renderer/renderer.h"
#include "text.h"
#include "menubar.h"

#define CONTENT_Y     ((float)TOP_BAR_H)
#define HEADER_H       28.0f
#define MIN_CELL_PX     4.0f
#define MAX_CELL_PX    48.0f
#define DEFAULT_CELL_PX 16.0f
#define ZOOM_STEP       1.15f

static bool hittest(float x, float y, float w, float h, int mx, int my) {
    return (float)mx >= x && (float)mx < x + w &&
           (float)my >= y && (float)my < y + h;
}

static void draw_border(float x, float y, float w, float h,
                         float r, float g, float b) {
    renderer_draw_quad(x,       y,       w,    1.0f, r, g, b, 1.0f);
    renderer_draw_quad(x,       y+h-1,   w,    1.0f, r, g, b, 1.0f);
    renderer_draw_quad(x,       y,       1.0f, h,    r, g, b, 1.0f);
    renderer_draw_quad(x+w-1,   y,       1.0f, h,    r, g, b, 1.0f);
}

void shape_pane_init(ShapePane *sp) {
    sp->pan_x = 0.0f;
    sp->pan_y = 0.0f;
    sp->cell_px = DEFAULT_CELL_PX;
    sp->panning = false;
    sp->pan_start_mx = sp->pan_start_my = 0;
    sp->pan_start_x = sp->pan_start_y = 0.0f;
    sp->rect_dragging = false;
    sp->rect_x0 = sp->rect_y0 = 0;
    sp->hover_valid = false;
    sp->hover_gx = sp->hover_gy = 0;
}

void shape_pane_fit_to_world(ShapePane *sp, const World *world, int viewport_h) {
    if (world->width <= 0 || world->height <= 0) return;

    float grid_h = (float)viewport_h - CONTENT_Y - HEADER_H - 16.0f;
    float grid_w = (float)SHAPE_PANE_W - 16.0f;

    float cell_for_w = grid_w / (float)world->width;
    float cell_for_h = grid_h / (float)world->height;
    float cell = cell_for_w < cell_for_h ? cell_for_w : cell_for_h;

    if (cell < MIN_CELL_PX) cell = MIN_CELL_PX;
    if (cell > MAX_CELL_PX) cell = MAX_CELL_PX;

    sp->cell_px = cell;
    sp->pan_x = 0.0f;
    sp->pan_y = 0.0f;
}

/* Pane rect — same helper used by both update (hit-testing) and render
   (drawing), so the clickable area can never drift from the drawn area. */
static void pane_rect(int viewport_w, int viewport_h,
                       float *out_x, float *out_y, float *out_w, float *out_h) {
    *out_x = (float)viewport_w - (float)SHAPE_PANE_W;
    *out_y = CONTENT_Y;
    *out_w = (float)SHAPE_PANE_W;
    *out_h = (float)viewport_h - CONTENT_Y;
}

/* Grid drawing area — pane rect minus the header strip and a small margin */
static void grid_rect(int viewport_w, int viewport_h,
                       float *out_x, float *out_y, float *out_w, float *out_h) {
    float px, py, pw, ph;
    pane_rect(viewport_w, viewport_h, &px, &py, &pw, &ph);
    *out_x = px + 8.0f;
    *out_y = py + HEADER_H + 8.0f;
    *out_w = pw - 16.0f;
    *out_h = ph - HEADER_H - 16.0f;
}

/* Converts a screen-space (mx,my) within the grid area to fractional
   world-tile coordinates, accounting for pan_x/pan_y and cell_px zoom.
   Returns false if outside the grid drawing area entirely. */
static bool screen_to_cell(const ShapePane *sp, int viewport_w, int viewport_h,
                            int mx, int my, int *out_gx, int *out_gy) {
    float gx0, gy0, gw, gh;
    grid_rect(viewport_w, viewport_h, &gx0, &gy0, &gw, &gh);
    if (!hittest(gx0, gy0, gw, gh, mx, my)) return false;

    float local_x = (float)mx - gx0;
    float local_y = (float)my - gy0;

    int gx = (int)(sp->pan_x + local_x / sp->cell_px);
    int gy = (int)(sp->pan_y + local_y / sp->cell_px);

    *out_gx = gx;
    *out_gy = gy;
    return true;
}

bool shape_pane_update(ShapePane *sp, World *world, int viewport_w, int viewport_h) {
    int mx, my;
    input_mouse_pos(&mx, &my);

    float px, py, pw, ph;
    pane_rect(viewport_w, viewport_h, &px, &py, &pw, &ph);
    bool over_pane = hittest(px, py, pw, ph, mx, my);

    float gx0, gy0, gw, gh;
    grid_rect(viewport_w, viewport_h, &gx0, &gy0, &gw, &gh);
    bool over_grid = hittest(gx0, gy0, gw, gh, mx, my);

    /* Activate the mask the first time this pane is used, mirroring
       EDITOR_MODE_SHAPE's own activation in editor.c — if you open the
       pane without ever having toggled shape mode in the iso view, the
       mask still needs to exist before world_shape_set() can do anything. */
    if (over_grid && !world->shape.active)
        world_shape_activate(&world->shape, world->width, world->height);

    /* ---- Zoom: scroll wheel while hovering the grid ---- */
    if (over_grid) {
        int sdx, sdy;
        input_mouse_scroll(&sdx, &sdy);
        if (sdy != 0) {
            /* Zoom around the cell under the cursor: convert to world
               coords before changing cell_px, then re-anchor pan so that
               same world coord stays under the cursor after the zoom. */
            float local_x = (float)mx - gx0;
            float local_y = (float)my - gy0;
            float anchor_gx = sp->pan_x + local_x / sp->cell_px;
            float anchor_gy = sp->pan_y + local_y / sp->cell_px;

            float new_cell = sp->cell_px * (sdy > 0 ? ZOOM_STEP : 1.0f / ZOOM_STEP);
            if (new_cell < MIN_CELL_PX) new_cell = MIN_CELL_PX;
            if (new_cell > MAX_CELL_PX) new_cell = MAX_CELL_PX;
            sp->cell_px = new_cell;

            sp->pan_x = anchor_gx - local_x / sp->cell_px;
            sp->pan_y = anchor_gy - local_y / sp->cell_px;
        }
    }

    /* ---- Pan: middle-mouse drag (left/right reserved for paint) ---- */
    if (sp->panning) {
        if (input_mouse_button_down(SDL_BUTTON_MIDDLE)) {
            float dx = (float)(mx - sp->pan_start_mx);
            float dy = (float)(my - sp->pan_start_my);
            sp->pan_x = sp->pan_start_x - dx / sp->cell_px;
            sp->pan_y = sp->pan_start_y - dy / sp->cell_px;
        } else {
            sp->panning = false;
        }
    } else if (over_grid && input_mouse_button_pressed(SDL_BUTTON_MIDDLE)) {
        sp->panning = true;
        sp->pan_start_mx = mx;
        sp->pan_start_my = my;
        sp->pan_start_x = sp->pan_x;
        sp->pan_start_y = sp->pan_y;
    }

    /* ---- Hover (for render highlight) ---- */
    int hgx, hgy;
    sp->hover_valid = over_grid && screen_to_cell(sp, viewport_w, viewport_h, mx, my, &hgx, &hgy)
                      && hgx >= 0 && hgy >= 0 && hgx < world->width && hgy < world->height;
    if (sp->hover_valid) { sp->hover_gx = hgx; sp->hover_gy = hgy; }

    /* ---- Paint: LMB enable, RMB disable, Shift+LMB drag = rect-fill ---- */
    bool shift = input_key_down(SDL_SCANCODE_LSHIFT) || input_key_down(SDL_SCANCODE_RSHIFT);

    if (sp->hover_valid) {
        if (shift) {
            if (input_mouse_button_pressed(SDL_BUTTON_LEFT)) {
                sp->rect_dragging = true;
                sp->rect_x0 = sp->hover_gx;
                sp->rect_y0 = sp->hover_gy;
            }
            if (sp->rect_dragging && !input_mouse_button_down(SDL_BUTTON_LEFT)) {
                int x0 = sp->rect_x0 < sp->hover_gx ? sp->rect_x0 : sp->hover_gx;
                int x1 = sp->rect_x0 < sp->hover_gx ? sp->hover_gx : sp->rect_x0;
                int y0 = sp->rect_y0 < sp->hover_gy ? sp->rect_y0 : sp->hover_gy;
                int y1 = sp->rect_y0 < sp->hover_gy ? sp->hover_gy : sp->rect_y0;
                world_shape_fill_rect(&world->shape, x0, y0, x1, y1, true);
                sp->rect_dragging = false;
            }
        } else {
            sp->rect_dragging = false;
            if (input_mouse_button_down(SDL_BUTTON_LEFT))
                world_shape_set(&world->shape, sp->hover_gx, sp->hover_gy, true);
            if (input_mouse_button_down(SDL_BUTTON_RIGHT))
                world_shape_set(&world->shape, sp->hover_gx, sp->hover_gy, false);
        }
    } else if (sp->rect_dragging && !input_mouse_button_down(SDL_BUTTON_LEFT)) {
        sp->rect_dragging = false;
    }

    return over_pane;
}

void shape_pane_render(const ShapePane *sp, const World *world, int viewport_w, int viewport_h) {
    float px, py, pw, ph;
    pane_rect(viewport_w, viewport_h, &px, &py, &pw, &ph);

    /* Pane background + left border, reading as a distinct docked panel
       against the world view (same visual language panel.h uses for the
       left sidebar — dark fill, single-pixel accent border). */
    renderer_draw_quad(px, py, pw, ph, 0.07f, 0.07f, 0.09f, 1.0f);
    renderer_draw_quad(px, py, 1.0f, ph, 0.22f, 0.45f, 0.30f, 1.0f);

    /* Header */
    text_draw(px + 10.0f, py + 6.0f, 1.4f, 0.85f, 0.90f, 0.85f, 1.0f, "WORLD SHAPE");
    char dims[64];
    snprintf(dims, sizeof dims, "%dx%d  LMB enable  RMB disable", world->width, world->height);
    text_draw(px + 10.0f, py + 6.0f + text_line_height(1.4f) + 2.0f, 1.05f,
              0.50f, 0.52f, 0.50f, 1.0f, dims);

    float gx0, gy0, gw, gh;
    grid_rect(viewport_w, viewport_h, &gx0, &gy0, &gw, &gh);

    /* Clip-by-hand: only draw cells that fall within the grid rect.
       There's no scissor-rect call available here, so visible_cols/rows
       is computed from cell_px and the grid rect is used to bound the
       loop instead of relying on GL clipping. */
    int visible_cols = (int)(gw / sp->cell_px) + 2;
    int visible_rows = (int)(gh / sp->cell_px) + 2;

    int start_x = (int)sp->pan_x;
    int start_y = (int)sp->pan_y;
    if (start_x < 0) start_x = 0;
    if (start_y < 0) start_y = 0;

    for (int gy = start_y; gy < start_y + visible_rows && gy < world->height; gy++) {
        for (int gx = start_x; gx < start_x + visible_cols && gx < world->width; gx++) {
            float cx = gx0 + ((float)gx - sp->pan_x) * sp->cell_px;
            float cy = gy0 + ((float)gy - sp->pan_y) * sp->cell_px;

            /* Cull cells fully outside the grid rect (cheap bound check
               instead of a real scissor test — fine at this cell count). */
            if (cx + sp->cell_px < gx0 || cx > gx0 + gw) continue;
            if (cy + sp->cell_px < gy0 || cy > gy0 + gh) continue;

            bool enabled = world_shape_enabled(&world->shape, gx, gy);
            bool hovered = sp->hover_valid && sp->hover_gx == gx && sp->hover_gy == gy;

            float r, g, b;
            if (enabled) {
                r = hovered ? 0.34f : 0.26f;
                g = hovered ? 0.62f : 0.50f;
                b = hovered ? 0.40f : 0.30f;
            } else {
                r = hovered ? 0.22f : 0.12f;
                g = hovered ? 0.14f : 0.08f;
                b = hovered ? 0.14f : 0.09f;
            }

            float pad = sp->cell_px > 6.0f ? 1.0f : 0.0f;
            renderer_draw_quad(cx + pad, cy + pad,
                               sp->cell_px - pad*2.0f, sp->cell_px - pad*2.0f,
                               r, g, b, 1.0f);
        }
    }

    /* Grid border */
    draw_border(gx0, gy0, gw, gh, 0.20f, 0.20f, 0.24f);

    /* In-progress rect-fill preview */
    if (sp->rect_dragging && sp->hover_valid) {
        int x0 = sp->rect_x0 < sp->hover_gx ? sp->rect_x0 : sp->hover_gx;
        int x1 = sp->rect_x0 < sp->hover_gx ? sp->hover_gx : sp->rect_x0;
        int y0 = sp->rect_y0 < sp->hover_gy ? sp->rect_y0 : sp->hover_gy;
        int y1 = sp->rect_y0 < sp->hover_gy ? sp->hover_gy : sp->rect_y0;

        float rx = gx0 + ((float)x0 - sp->pan_x) * sp->cell_px;
        float ry = gy0 + ((float)y0 - sp->pan_y) * sp->cell_px;
        float rw = (float)(x1 - x0 + 1) * sp->cell_px;
        float rh = (float)(y1 - y0 + 1) * sp->cell_px;
        renderer_draw_quad(rx, ry, rw, rh, 1.0f, 1.0f, 1.0f, 0.12f);
        draw_border(rx, ry, rw, rh, 0.85f, 0.85f, 0.50f);
    }

    /* Footer hint */
    text_draw(px + 10.0f, py + ph - text_line_height(1.0f) - 8.0f, 1.0f,
              0.42f, 0.44f, 0.42f, 1.0f,
              "SCROLL ZOOM  MIDDLE-DRAG PAN  SHIFT+LMB RECT");
}
