#include "window.h"

#include <glad/glad.h>
#include "../core/log.h"

bool window_create(Window *w, const char *title, int width, int height) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        LOG_ERROR("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    w->handle = SDL_CreateWindow(
        title,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
    );

    if (!w->handle) {
        LOG_ERROR("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }

    w->gl_ctx = SDL_GL_CreateContext(w->handle);
    if (!w->gl_ctx) {
        LOG_ERROR("SDL_GL_CreateContext failed: %s", SDL_GetError());
        SDL_DestroyWindow(w->handle);
        return false;
    }

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        LOG_ERROR("GLAD failed to load OpenGL function pointers");
        SDL_GL_DeleteContext(w->gl_ctx);
        SDL_DestroyWindow(w->handle);
        return false;
    }

    SDL_GL_SetSwapInterval(1); /* vsync */

    w->width  = width;
    w->height = height;
    w->title  = title;

    LOG_INFO("Window created: %s (%dx%d) | OpenGL %s",
             title, width, height, glGetString(GL_VERSION));

    return true;
}

void window_destroy(Window *w) {
    SDL_GL_DeleteContext(w->gl_ctx);
    SDL_DestroyWindow(w->handle);
    SDL_Quit();
}

void window_swap(Window *w) {
    SDL_GL_SwapWindow(w->handle);
}

void window_get_size(Window *w, int *width, int *height) {
    SDL_GetWindowSize(w->handle, width, height);
    w->width  = *width;
    w->height = *height;
}
