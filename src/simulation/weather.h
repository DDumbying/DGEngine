#ifndef DGE_WEATHER_H
#define DGE_WEATHER_H

#include "../renderer/camera.h"
#include "../ecs/registry.h"

typedef enum {
    WEATHER_SUNNY = 0,
    WEATHER_RAIN,
    WEATHER_SNOW,
    WEATHER_COUNT
} WeatherType;

typedef struct {
    float x, y;
    float vx, vy;
    float size_w, size_h;
    float opacity;
} WeatherParticle;

typedef struct {
    WeatherType type;
    float       timer;      /* Game-time seconds elapsed in current weather */
    float       duration;   /* Game-time seconds before next transition */
    WeatherParticle particles[256];
    int         num_particles;
    bool        initialized_particles;
} WeatherSystem;

/* Initialize weather system */
void weather_init(WeatherSystem *ws);

/* Update weather state, timer, and particles. */
void weather_update(WeatherSystem *ws, float game_dt, float real_dt, const Camera *cam);

/* Render the weather overlay/particles */
void weather_render(const WeatherSystem *ws, const Camera *cam);

/* Get human-readable name of weather */
const char *weather_name(WeatherType type);

/* Get movement speed multiplier based on weather */
float weather_speed_multiplier(WeatherType type);

#endif /* DGE_WEATHER_H */
