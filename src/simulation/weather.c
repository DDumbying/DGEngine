#include "weather.h"
#include "../renderer/renderer.h"
#include "../core/log.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

const char *weather_name(WeatherType type) {
    switch (type) {
        case WEATHER_NONE:  return "NONE";
        case WEATHER_SUNNY: return "SUNNY";
        case WEATHER_RAIN:  return "RAIN";
        case WEATHER_SNOW:  return "SNOW";
        default:            return "?";
    }
}

float weather_speed_multiplier(WeatherType type) {
    switch (type) {
        case WEATHER_RAIN:  return 0.7f;
        case WEATHER_SNOW:  return 0.5f;
        default:            return 1.0f;
    }
}

void weather_init(WeatherSystem *ws) {
    memset(ws, 0, sizeof(*ws));
    ws->type    = WEATHER_NONE;   /* starts disabled — user controls it */
    ws->enabled = false;
    ws->timer   = 0.0f;
    ws->duration = 60.0f;
}

void weather_set_enabled(WeatherSystem *ws, bool enabled) {
    ws->enabled = enabled;
    if (!enabled) ws->timer = 0.0f;
    LOG_INFO("Weather auto-cycle -> %s", enabled ? "ON" : "OFF");
}

void weather_set_type(WeatherSystem *ws, WeatherType type) {
    ws->type = type;
    ws->timer = 0.0f;
    ws->initialized_particles = false;
    ws->num_particles = 0;
    LOG_INFO("Weather manually set to: %s", weather_name(type));
}

static void init_particles(WeatherSystem *ws, const Camera *cam) {
    float half_w = (float)cam->viewport_w * 0.5f / cam->zoom;
    float half_h = (float)cam->viewport_h * 0.5f / cam->zoom;
    float left   = cam->position.x - half_w;
    float right  = cam->position.x + half_w;
    float top    = cam->position.y - half_h;
    float bottom = cam->position.y + half_h;
    (void)bottom;

    ws->num_particles = (ws->type == WEATHER_RAIN) ? 256
                      : (ws->type == WEATHER_SNOW) ? 192 : 0;

    for (int i = 0; i < ws->num_particles; i++) {
        float r = (float)rand() / (float)RAND_MAX;
        ws->particles[i].x = left + r * (right - left);
        ws->particles[i].y = top  + ((float)rand()/(float)RAND_MAX) * (half_h * 2.0f);
        ws->particles[i].opacity = 0.3f + ((float)rand()/(float)RAND_MAX) * 0.5f;
        if (ws->type == WEATHER_RAIN) {
            ws->particles[i].vx = -60.0f - ((float)rand()/(float)RAND_MAX)*40.0f;
            ws->particles[i].vy = 500.0f + ((float)rand()/(float)RAND_MAX)*300.0f;
            ws->particles[i].size_w = 1.0f + ((float)rand()/(float)RAND_MAX)*1.0f;
            ws->particles[i].size_h = 10.0f + ((float)rand()/(float)RAND_MAX)*10.0f;
        } else {
            ws->particles[i].vx = -15.0f + ((float)rand()/(float)RAND_MAX)*30.0f;
            ws->particles[i].vy = 80.0f  + ((float)rand()/(float)RAND_MAX)*60.0f;
            ws->particles[i].size_w = ws->particles[i].size_h =
                2.0f + ((float)rand()/(float)RAND_MAX)*3.0f;
        }
    }
    ws->initialized_particles = true;
}

void weather_update(WeatherSystem *ws, float game_dt, float real_dt,
                     const Camera *cam) {
    /* Auto-cycle only when enabled */
    if (ws->enabled && ws->type != WEATHER_NONE) {
        ws->timer += game_dt;
        if (ws->timer >= ws->duration) {
            ws->timer = 0.0f;
            ws->duration = 30.0f + (float)(rand() % 30);
            /* Cycle through SUNNY/RAIN/SNOW, skip NONE */
            int next = (int)ws->type + 1;
            if (next >= WEATHER_COUNT) next = WEATHER_SUNNY;
            ws->type = (WeatherType)next;
            ws->initialized_particles = false;
            LOG_INFO("Weather -> %s", weather_name(ws->type));
        }
    }

    if (ws->type == WEATHER_NONE || ws->type == WEATHER_SUNNY) {
        ws->num_particles = 0;
        return;
    }

    if (!ws->initialized_particles && cam->viewport_w > 0)
        init_particles(ws, cam);

    float half_w = (float)cam->viewport_w * 0.5f / cam->zoom;
    float half_h = (float)cam->viewport_h * 0.5f / cam->zoom;
    float left   = cam->position.x - half_w;
    float right  = cam->position.x + half_w;
    float top    = cam->position.y - half_h;
    float bottom = cam->position.y + half_h;
    float pad    = 20.0f;

    for (int i = 0; i < ws->num_particles; i++) {
        WeatherParticle *p = &ws->particles[i];
        p->x += p->vx * real_dt;
        p->y += p->vy * real_dt;
        if (ws->type == WEATHER_SNOW)
            p->x += sinf(p->y * 0.05f + (float)i) * 10.0f * real_dt;
        if (p->y > bottom + pad || p->x < left - pad || p->x > right + pad) {
            p->y = top - pad - ((float)rand()/(float)RAND_MAX) * pad;
            p->x = left + ((float)rand()/(float)RAND_MAX) * (right - left);
            p->opacity = 0.3f + ((float)rand()/(float)RAND_MAX) * 0.5f;
            if (ws->type == WEATHER_RAIN) {
                p->vx = -60.0f - ((float)rand()/(float)RAND_MAX)*40.0f;
                p->vy = 500.0f + ((float)rand()/(float)RAND_MAX)*300.0f;
            } else {
                p->vx = -15.0f + ((float)rand()/(float)RAND_MAX)*30.0f;
                p->vy = 80.0f  + ((float)rand()/(float)RAND_MAX)*60.0f;
            }
        }
    }
}

void weather_render(const WeatherSystem *ws, const Camera *cam) {
    (void)cam;
    if (ws->num_particles == 0) return;
    for (int i = 0; i < ws->num_particles; i++) {
        const WeatherParticle *p = &ws->particles[i];
        if (ws->type == WEATHER_RAIN)
            renderer_draw_quad(p->x, p->y, p->size_w, p->size_h,
                               0.4f, 0.6f, 0.9f, p->opacity);
        else
            renderer_draw_quad(p->x, p->y, p->size_w, p->size_h,
                               0.95f, 0.95f, 1.0f, p->opacity);
    }
}

#define DGEA_MAGIC   "DGEA"
#define DGEA_VERSION 2u   /* bumped: added enabled flag */

bool weather_save(const WeatherSystem *ws, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) { LOG_ERROR("weather_save: cannot open '%s'", path); return false; }
    unsigned int ver = DGEA_VERSION;
    unsigned char type = (unsigned char)ws->type;
    unsigned char ena  = ws->enabled ? 1 : 0;
    bool ok = true;
    ok &= fwrite(DGEA_MAGIC, 1, 4, f) == 4;
    ok &= fwrite(&ver,          sizeof(ver),          1, f) == 1;
    ok &= fwrite(&type,         1,                    1, f) == 1;
    ok &= fwrite(&ws->timer,    sizeof(ws->timer),    1, f) == 1;
    ok &= fwrite(&ws->duration, sizeof(ws->duration), 1, f) == 1;
    ok &= fwrite(&ena,          1,                    1, f) == 1;
    fclose(f);
    return ok;
}

bool weather_load(WeatherSystem *ws, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    char magic[4]; unsigned int ver; unsigned char type; float timer, dur;
    bool ok = true;
    ok &= fread(magic, 1, 4, f) == 4;
    ok &= fread(&ver,   sizeof(ver),   1, f) == 1;
    ok &= fread(&type,  1,             1, f) == 1;
    ok &= fread(&timer, sizeof(timer), 1, f) == 1;
    ok &= fread(&dur,   sizeof(dur),   1, f) == 1;
    unsigned char ena = 0;
    if (ver >= 2) ok &= fread(&ena, 1, 1, f) == 1;
    fclose(f);
    if (!ok || magic[0]!='D'||magic[1]!='G'||magic[2]!='E'||magic[3]!='A') return false;
    if (type >= WEATHER_COUNT) type = 0;
    ws->type     = (WeatherType)type;
    ws->timer    = timer;
    ws->duration = dur;
    ws->enabled  = ena != 0;
    ws->initialized_particles = false;
    ws->num_particles = 0;
    return true;
}
