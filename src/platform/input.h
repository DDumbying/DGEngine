#ifndef DGE_INPUT_H
#define DGE_INPUT_H

#include <stdbool.h>
#include <SDL2/SDL.h>

/* Call once per frame before reading any input state. */
void input_begin_frame(void);

/* Feed a raw SDL event into the input system. */
void input_process_event(const SDL_Event *event);

/* Keyboard — uses SDL_Scancode values. */
bool input_key_down(SDL_Scancode key);      /* held this frame */
bool input_key_pressed(SDL_Scancode key);   /* went down this frame */
bool input_key_released(SDL_Scancode key);  /* went up this frame */

/* Mouse */
void  input_mouse_pos(int *x, int *y);
bool  input_mouse_button_down(int button);      /* SDL_BUTTON_LEFT etc. */
bool  input_mouse_button_pressed(int button);
bool  input_mouse_button_released(int button);
void  input_mouse_scroll(int *dx, int *dy);

/* Mouse motion delta this frame — non-zero only when the mouse moved.
   Used for middle-mouse drag panning so camera.c doesn't need to store
   previous mouse position itself. */
void  input_mouse_delta(int *dx, int *dy);

/* True when the user asked to quit (Alt-F4 / window close). */
bool input_quit_requested(void);

/*  Text input (Phase H) — deliberately separate from the scancode API
    above. Scancodes are physical key identity ("the key where Q is on
    a US layout"); SDL_TEXTINPUT delivers the actual character the OS
    keyboard layout/IME produced (so shift+digit punctuation, dead
    keys, non-US layouts, etc. come out right without this engine
    knowing anything about keyboard layouts itself). Existing scancode
    shortcuts (WASD, F5, TAB, ...) are untouched by any of this and
    keep working exactly as before, focused text field or not. */

/* Turns SDL_TEXTINPUT event delivery on/off. This is a thin wrapper
   over SDL's own text-input mode, which is global, not per-widget —
   call _activate() when a text field gains focus and _deactivate()
   when it loses it. Only one field is expected to be focused at a
   time; callers must deactivate the old field before activating a
   new one. */
void input_text_input_activate(void);
void input_text_input_deactivate(void);

/* Returns true if a text field currently has focus. If true, gameplay systems 
   (like camera panning or editor shortcuts) should ignore keyboard input to 
   prevent "input bleed". */
bool input_keyboard_consumed(void);

/* UTF-8 bytes typed this frame — usually a single ASCII character,
   occasionally more (fast typing can deliver more than one
   SDL_TEXTINPUT event per frame; an IME composition commit can also
   produce several bytes at once). Empty string if nothing was typed
   this frame. The returned pointer is only valid until the next
   input_begin_frame() call. */
const char *input_text_typed(void);

#endif /* DGE_INPUT_H */
