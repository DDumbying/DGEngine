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

/* True when the user asked to quit (Alt-F4 / window close). */
bool input_quit_requested(void);

#endif /* DGE_INPUT_H */
