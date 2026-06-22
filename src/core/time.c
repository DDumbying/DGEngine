#include "time.h"

#include <SDL2/SDL.h>

static Uint64 s_freq      = 0;
static Uint64 s_last      = 0;
static float  s_delta     = 0.0f;
static double s_elapsed   = 0.0;
static unsigned int s_frame = 0;

void dge_time_tick(void) {
    if (s_freq == 0) {
        /* First call — initialise. */
        s_freq  = SDL_GetPerformanceFrequency();
        s_last  = SDL_GetPerformanceCounter();
        s_delta = 0.0f;
        return;
    }

    Uint64 now = SDL_GetPerformanceCounter();
    s_delta    = (float)((double)(now - s_last) / (double)s_freq);

    /* Cap to avoid huge jumps when debugging / focus loss. */
    if (s_delta > 0.05f) s_delta = 0.05f;

    s_elapsed += s_delta;
    s_last     = now;
    s_frame++;
}

float        dge_time_delta(void)   { return s_delta; }
double       dge_time_elapsed(void) { return s_elapsed; }
unsigned int dge_time_frame(void)   { return s_frame; }
