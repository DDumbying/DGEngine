#ifndef DGE_WEATHER_H
#define DGE_WEATHER_H

#include <stdbool.h>
#include "../renderer/camera.h"
#include "../ecs/registry.h"

typedef enum {
    WEATHER_NONE  = 0,   /* disabled — shows nothing, speed multiplier = 1 */
    WEATHER_SUNNY = 1,
    WEATHER_RAIN  = 2,
    WEATHER_SNOW  = 3,
    WEATHER_COUNT = 4,
} WeatherType;

typedef struct {
    float x, y;
    float vx, vy;
    float size_w, size_h;
    float opacity;
} WeatherParticle;

typedef struct {
    WeatherType type;
    float       timer;
    float       duration;
    WeatherParticle particles[256];
    int         num_particles;
    bool        initialized_particles;
    bool        enabled;   /* false = frozen at current type, no auto-cycle */
} WeatherSystem;

void        weather_init(WeatherSystem *ws);

/* enabled=false: type stays fixed, no auto-transitions, particles still render */
void        weather_set_enabled(WeatherSystem *ws, bool enabled);

/* Manually set weather type (works whether enabled or not). */
void        weather_set_type(WeatherSystem *ws, WeatherType type);

void        weather_update(WeatherSystem *ws, float game_dt, float real_dt,
                            const Camera *cam);
void        weather_render(const WeatherSystem *ws, const Camera *cam);
const char *weather_name(WeatherType type);
float       weather_speed_multiplier(WeatherType type);
bool        weather_save(const WeatherSystem *ws, const char *path);
bool        weather_load(WeatherSystem *ws, const char *path);

#endif /* DGE_WEATHER_H */
