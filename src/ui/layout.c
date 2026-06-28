#include "layout.h"
#include "../renderer/renderer.h"
#include "../platform/input.h"
#include "text.h"
#include <SDL2/SDL.h>

#define SCALE_LBL      1.5f
#define SCALE_BODY     1.5f
#define SCALE_SMALL    1.3f

static bool hittest(float x, float y, float w, float h, int mx, int my) {
    return (float)mx >= x && (float)mx < x + w &&
           (float)my >= y && (float)my < y + h;
}

static void draw_box_border(float x, float y, float w, float h,
                             float br, float bg, float bb) {
    renderer_draw_quad(x,       y,       w,    1.0f, br, bg, bb, 1.0f);
    renderer_draw_quad(x,       y+h-1,   w,    1.0f, br, bg, bb, 1.0f);
    renderer_draw_quad(x,       y,       1.0f, h,    br, bg, bb, 1.0f);
    renderer_draw_quad(x+w-1,   y,       1.0f, h,    br, bg, bb, 1.0f);
}

void ui_layout_begin(UILayout *l, float x, float y, float w) {
    l->start_x = x;
    l->width = w;
    l->cursor_x = x;
    l->cursor_y = y;
    l->row_height = 0.0f;
}

void ui_layout_gap(UILayout *l, float gap) {
    l->cursor_y += gap;
    l->cursor_x = l->start_x;
    l->row_height = 0.0f;
}

void ui_layout_label(UILayout *l, const char *text, bool small_font) {
    float scale = small_font ? SCALE_SMALL : SCALE_BODY;
    text_draw(l->cursor_x, l->cursor_y, scale, 0.52f, 0.52f, 0.55f, 1.0f, text);
    
    float th = text_line_height(scale);
    if (th > l->row_height) l->row_height = th;
    
    // Auto advance if we aren't in a manual row
    l->cursor_y += th + 2.0f;
}

bool ui_layout_button(UILayout *l, const char *label, float w, float h, bool active, int mx, int my) {
    if (w <= 0.0f) w = l->width;
    
    bool hover = hittest(l->cursor_x, l->cursor_y, w, h, mx, my);
    bool click = hover && input_mouse_button_pressed(SDL_BUTTON_LEFT);
    
    float bg = active ? 0.48f : (hover ? 0.22f : 0.14f);
    renderer_draw_quad(l->cursor_x, l->cursor_y, w, h, bg, bg, bg + 0.03f, 1.0f);
    draw_box_border(l->cursor_x, l->cursor_y, w, h,
                    active ? 0.30f : 0.30f,
                    active ? 0.80f : 0.30f,
                    active ? 0.45f : 0.36f);
    
    float tw = text_measure_width(label, SCALE_BODY);
    float th = text_line_height(SCALE_BODY);
    text_draw(l->cursor_x + (w - tw) * 0.5f, l->cursor_y + (h - th) * 0.5f,
              SCALE_BODY, 0.90f, 0.90f, 0.90f, 1.0f, label);
              
    if (h > l->row_height) l->row_height = h;
    l->cursor_y += h + 4.0f; /* default gap */
    
    return click;
}

bool ui_layout_field(UILayout *l, TextInput *ti, float w, float h, bool focused, int mx, int my, bool *out_enter_pressed) {
    if (w <= 0.0f) w = l->width;
    
    bool hover = hittest(l->cursor_x, l->cursor_y, w, h, mx, my);
    bool click = hover && input_mouse_button_pressed(SDL_BUTTON_LEFT);
    
    float fbg = focused ? 0.17f : 0.11f;
    renderer_draw_quad(l->cursor_x, l->cursor_y, w, h, fbg, fbg, fbg, 1.0f);
    draw_box_border(l->cursor_x, l->cursor_y, w, h,
                    focused ? 0.30f : 0.22f,
                    focused ? 0.75f : 0.30f,
                    focused ? 0.45f : 0.22f);
    
    float th = text_line_height(SCALE_BODY);
    bool enter = false;
    if (focused) {
        enter = textinput_update(ti, l->cursor_x + 4.0f, l->cursor_y + (h - th) * 0.5f, w - 8.0f, SCALE_BODY);
    } else {
        textinput_render(ti, l->cursor_x + 4.0f, l->cursor_y + (h - th) * 0.5f, SCALE_BODY, 1.0f, 1.0f, 1.0f, 1.0f);
    }
    
    if (out_enter_pressed) *out_enter_pressed = enter;
    
    if (h > l->row_height) l->row_height = h;
    l->cursor_y += h + 4.0f; /* default gap */
    
    return click;
}

void ui_layout_row_begin(UILayout *l) {
    l->row_height = 0.0f;
}

void ui_layout_row_step_x(UILayout *l, float x_offset) {
    /* Step X right, and reverse the auto-Y advance that label/field/button did */
    l->cursor_x = l->start_x + x_offset;
    l->cursor_y -= l->row_height + 4.0f; 
}

void ui_layout_row_end(UILayout *l) {
    l->cursor_x = l->start_x;
    l->cursor_y += l->row_height + 4.0f;
    l->row_height = 0.0f;
}
