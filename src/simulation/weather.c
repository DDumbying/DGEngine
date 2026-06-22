#include "weather.h"
#include "../renderer/renderer.h"
#include "../core/log.h"
#include <stdlib.h>
#include <math.h>

const char *weather_name(WeatherType type) {
    switch (type) {
        case WEATHER_SUNNY: return "SUNNY";
        case WEATHER_RAIN:  return "RAIN";
        case WEATHER_SNOW:  return "SNOW";
        default:            return "UNKNOWN";
    }
}

float weather_speed_multiplier(WeatherType type) {
    switch (type) {
        case WEATHER_SUNNY: return 1.0f;
        case WEATHER_RAIN:  return 0.7f;
        case WEATHER_SNOW:  return 0.5f;
        default:            return 1.0f;
    }
}

void weather_init(WeatherSystem *ws) {
    ws->type = WEATHER_SUNNY;
    ws->timer = 0.0f;
    ws->duration = 30.0f + (float)(rand() % 30); /* 30-60 game seconds */
    ws->num_particles = 0;
    ws->initialized_particles = false;
}

static void init_particles_for_weather(WeatherSystem *ws, const Camera *cam) {
    float half_w = (float)cam->viewport_w * 0.5f / cam->zoom;
    float half_h = (float)cam->viewport_h * 0.5f / cam->zoom;
    float left   = cam->position.x - half_w;
    float right  = cam->position.x + half_w;
    float top    = cam->position.y - half_h;
    float bottom = cam->position.y + half_h;

    ws->num_particles = (ws->type == WEATHER_RAIN) ? 256 : ((ws->type == WEATHER_SNOW) ? 192 : 0);

    for (int i = 0; i < ws->num_particles; i++) {
        ws->particles[i].x = left + ((float)rand() / (float)RAND_MAX) * (right - left);
        ws->particles[i].y = top + ((float)rand() / (float)RAND_MAX) * (bottom - top);
        ws->particles[i].opacity = 0.3f + ((float)rand() / (float)RAND_MAX) * 0.5f;

        if (ws->type == WEATHER_RAIN) {
            ws->particles[i].vx = -60.0f - ((float)rand() / (float)RAND_MAX) * 40.0f;
            ws->particles[i].vy = 500.0f + ((float)rand() / (float)RAND_MAX) * 300.0f;
            ws->particles[i].size_w = 1.0f + ((float)rand() / (float)RAND_MAX) * 1.0f;
            ws->particles[i].size_h = 10.0f + ((float)rand() / (float)RAND_MAX) * 10.0f;
        } else if (ws->type == WEATHER_SNOW) {
            ws->particles[i].vx = -15.0f + ((float)rand() / (float)RAND_MAX) * 30.0f;
            ws->particles[i].vy = 80.0f + ((float)rand() / (float)RAND_MAX) * 60.0f;
            ws->particles[i].size_w = 2.0f + ((float)rand() / (float)RAND_MAX) * 3.0f;
            ws->particles[i].size_h = ws->particles[i].size_w;
        }
    }
    ws->initialized_particles = true;
}

void weather_update(WeatherSystem *ws, float game_dt, float real_dt, const Camera *cam) {
    /* 1. Advance weather timer (in game time) */
    ws->timer += game_dt;
    if (ws->timer >= ws->duration) {
        ws->timer = 0.0f;
        ws->duration = 30.0f + (float)(rand() % 30);
        ws->type = (WeatherType)((ws->type + 1) % WEATHER_COUNT);
        ws->initialized_particles = false;
        LOG_INFO("Weather changed to: %s (duration=%.1f game-seconds)", weather_name(ws->type), ws->duration);
    }

    if (ws->type == WEATHER_SUNNY) {
        ws->num_particles = 0;
        return;
    }

    /* Make sure particles are initialized once camera details are known */
    if (!ws->initialized_particles && cam->viewport_w > 0) {
        init_particles_for_weather(ws, cam);
    }

    /* 2. Update particles (in real time) */
    float half_w = (float)cam->viewport_w * 0.5f / cam->zoom;
    float half_h = (float)cam->viewport_h * 0.5f / cam->zoom;
    float left   = cam->position.x - half_w;
    float right  = cam->position.x + half_w;
    float top    = cam->position.y - half_h;
    float bottom = cam->position.y + half_h;

    float pad = 20.0f; /* spawn/wrap padding outside viewport */

    for (int i = 0; i < ws->num_particles; i++) {
        WeatherParticle *p = &ws->particles[i];

        /* Update position */
        p->x += p->vx * real_dt;
        p->y += p->vy * real_dt;

        /* If snowing, add a bit of gentle sway */
        if (ws->type == WEATHER_SNOW) {
            p->x += sinf(p->y * 0.05f + (float)i) * 10.0f * real_dt;
        }

        /* Wrap or re-spawn when out of view */
        if (p->y > bottom + pad || p->x < left - pad || p->x > right + pad) {
            p->y = top - pad - ((float)rand() / (float)RAND_MAX) * pad;
            p->x = left + ((float)rand() / (float)RAND_MAX) * (right - left);
            p->opacity = 0.3f + ((float)rand() / (float)RAND_MAX) * 0.5f;

            if (ws->type == WEATHER_RAIN) {
                p->vx = -60.0f - ((float)rand() / (float)RAND_MAX) * 40.0f;
                p->vy = 500.0f + ((float)rand() / (float)RAND_MAX) * 300.0f;
            } else if (ws->type == WEATHER_SNOW) {
                p->vx = -15.0f + ((float)rand() / (float)RAND_MAX) * 30.0f;
                p->vy = 80.0f + ((float)rand() / (float)RAND_MAX) * 60.0f;
            }
        }
    }
}

void weather_render(const WeatherSystem *ws, const Camera *cam) {
    (void)cam;
    if (ws->type == WEATHER_SUNNY || ws->num_particles == 0) {
        return;
    }

    for (int i = 0; i < ws->num_particles; i++) {
        const WeatherParticle *p = &ws->particles[i];

        /* Draw particle quad */
        if (ws->type == WEATHER_RAIN) {
            /* Thin blueish rain lines */
            renderer_draw_quad(p->x, p->y, p->size_w, p->size_h,
                               0.4f, 0.6f, 0.9f, p->opacity);
        } else if (ws->type == WEATHER_SNOW) {
            /* White soft snowflakes */
            renderer_draw_quad(p->x, p->y, p->size_w, p->size_h,
                               0.95f, 0.95f, 1.0f, p->opacity);
        }
    }
}
