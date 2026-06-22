#include "input.h"

#include <string.h>

#define MAX_KEYS SDL_NUM_SCANCODES
#define MAX_MOUSE_BUTTONS 6

static bool s_keys_cur[MAX_KEYS];
static bool s_keys_prev[MAX_KEYS];

static bool s_mouse_cur[MAX_MOUSE_BUTTONS];
static bool s_mouse_prev[MAX_MOUSE_BUTTONS];

static int  s_mouse_x, s_mouse_y;
static int  s_scroll_x, s_scroll_y;
static bool s_quit;

void input_begin_frame(void) {
    memcpy(s_keys_prev,  s_keys_cur,  sizeof(s_keys_cur));
    memcpy(s_mouse_prev, s_mouse_cur, sizeof(s_mouse_cur));
    s_scroll_x = s_scroll_y = 0;
}

void input_process_event(const SDL_Event *event) {
    switch (event->type) {
        case SDL_QUIT:
            s_quit = true;
            break;

        case SDL_KEYDOWN:
            if (event->key.keysym.scancode < MAX_KEYS)
                s_keys_cur[event->key.keysym.scancode] = true;
            break;

        case SDL_KEYUP:
            if (event->key.keysym.scancode < MAX_KEYS)
                s_keys_cur[event->key.keysym.scancode] = false;
            break;

        case SDL_MOUSEMOTION:
            s_mouse_x = event->motion.x;
            s_mouse_y = event->motion.y;
            break;

        case SDL_MOUSEBUTTONDOWN:
            if (event->button.button < MAX_MOUSE_BUTTONS)
                s_mouse_cur[event->button.button] = true;
            break;

        case SDL_MOUSEBUTTONUP:
            if (event->button.button < MAX_MOUSE_BUTTONS)
                s_mouse_cur[event->button.button] = false;
            break;

        case SDL_MOUSEWHEEL:
            s_scroll_x += event->wheel.x;
            s_scroll_y += event->wheel.y;
            break;

        default:
            break;
    }
}

bool input_key_down(SDL_Scancode key) {
    return key < MAX_KEYS && s_keys_cur[key];
}
bool input_key_pressed(SDL_Scancode key) {
    return key < MAX_KEYS && s_keys_cur[key] && !s_keys_prev[key];
}
bool input_key_released(SDL_Scancode key) {
    return key < MAX_KEYS && !s_keys_cur[key] && s_keys_prev[key];
}

void input_mouse_pos(int *x, int *y) {
    *x = s_mouse_x;
    *y = s_mouse_y;
}
bool input_mouse_button_down(int b) {
    return b < MAX_MOUSE_BUTTONS && s_mouse_cur[b];
}
bool input_mouse_button_pressed(int b) {
    return b < MAX_MOUSE_BUTTONS && s_mouse_cur[b] && !s_mouse_prev[b];
}
bool input_mouse_button_released(int b) {
    return b < MAX_MOUSE_BUTTONS && !s_mouse_cur[b] && s_mouse_prev[b];
}
void input_mouse_scroll(int *dx, int *dy) {
    *dx = s_scroll_x;
    *dy = s_scroll_y;
}
bool input_quit_requested(void) { return s_quit; }
