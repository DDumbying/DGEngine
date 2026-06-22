#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include "simulation/weather.h"
#include <string.h>

void renderer_draw_quad(float x, float y, float w, float h,
                        float r, float g, float b, float a) {
    (void)x; (void)y; (void)w; (void)h;
    (void)r; (void)g; (void)b; (void)a;
}

static int approx(float a, float b) { return fabsf(a - b) < 0.0001f; }

int main(void) {
    srand(42u);
    WeatherSystem ws;
    weather_init(&ws);

    /* 1. Verify default values */
    assert(ws.type == WEATHER_SUNNY);
    assert(approx(ws.timer, 0.0f));
    assert(ws.duration >= 30.0f && ws.duration <= 60.0f);
    printf("PASS: weather_init sets correct defaults\n");

    /* 2. Verify speed multipliers */
    assert(approx(weather_speed_multiplier(WEATHER_SUNNY), 1.0f));
    assert(approx(weather_speed_multiplier(WEATHER_RAIN), 0.7f));
    assert(approx(weather_speed_multiplier(WEATHER_SNOW), 0.5f));
    printf("PASS: weather_speed_multiplier values are correct\n");

    /* 3. Verify weather name strings */
    assert(strcmp(weather_name(WEATHER_SUNNY), "SUNNY") == 0);
    assert(strcmp(weather_name(WEATHER_RAIN), "RAIN") == 0);
    assert(strcmp(weather_name(WEATHER_SNOW), "SNOW") == 0);
    printf("PASS: weather_name strings are correct\n");

    /* 4. Test transitions over time */
    Camera dummy_cam;
    dummy_cam.position.x = 0.0f;
    dummy_cam.position.y = 0.0f;
    dummy_cam.zoom = 1.0f;
    dummy_cam.viewport_w = 0; /* will prevent particle spawn but test timer */
    dummy_cam.viewport_h = 0;

    float step = ws.duration + 1.0f;
    weather_update(&ws, step, step, &dummy_cam);

    /* Weather should have changed to next state */
    assert(ws.type == WEATHER_RAIN);
    assert(approx(ws.timer, 0.0f));
    printf("PASS: weather_update handles transition after duration\n");

    printf("test_weather: ALL TESTS PASSED\n");
    return 0;
}
