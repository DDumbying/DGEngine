#ifndef DGE_WINDOW_H
#define DGE_WINDOW_H

#include <stdbool.h>
#include <SDL2/SDL.h>

typedef struct {
    SDL_Window    *handle;
    SDL_GLContext  gl_ctx;
    int            width;
    int            height;
    const char    *title;
} Window;

/* Returns false on failure. */
bool window_create(Window *w, const char *title, int width, int height);
void window_destroy(Window *w);
void window_swap(Window *w);
void window_get_size(Window *w, int *width, int *height);

#endif /* DGE_WINDOW_H */
