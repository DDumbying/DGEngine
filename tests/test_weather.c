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

    /* 1. Verify default values — weather_init() deliberately starts
       disabled at WEATHER_NONE ("user controls it", see weather.c);
       it's not random/uninitialized, so this is an exact check, not a
       range. */
    assert(ws.type == WEATHER_NONE);
    assert(ws.enabled == false);
    assert(approx(ws.timer, 0.0f));
    assert(approx(ws.duration, 60.0f));
    printf("PASS: weather_init sets correct defaults (disabled, NONE)\n");

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

    /* 4. Test transitions over time — auto-cycle only runs when
       enabled (see weather_update's "Auto-cycle only when enabled"
       gate), so the test has to opt in explicitly now instead of
       relying on weather_init's old "starts at SUNNY" default. */
    weather_set_type(&ws, WEATHER_SUNNY);
    weather_set_enabled(&ws, true);

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

    /* 5. Save/load round trip — only type/timer/duration are persisted;
       particles are ephemeral and must come back reset so the next
       weather_update() regenerates them. */
    ws.timer = 12.5f;
    ws.duration = 45.0f;
    assert(weather_save(&ws, "/tmp/dge_test_weather.dge"));

    WeatherSystem loaded;
    weather_init(&loaded); /* start from a different state to prove load overwrites it */
    assert(loaded.type != WEATHER_RAIN);
    assert(weather_load(&loaded, "/tmp/dge_test_weather.dge"));

    assert(loaded.type == WEATHER_RAIN);
    assert(approx(loaded.timer, 12.5f));
    assert(approx(loaded.duration, 45.0f));
    assert(loaded.num_particles == 0);
    assert(loaded.initialized_particles == false);
    printf("PASS: weather_save/load round-trip preserves type/timer/duration, "
           "resets ephemeral particle state\n");

    /* 6. Corrupt file: should fail cleanly, not crash */
    FILE *f = fopen("/tmp/dge_test_weather_garbage.dge", "wb");
    fwrite("XXXX", 1, 4, f);
    fclose(f);
    WeatherSystem garbage;
    weather_init(&garbage);
    assert(!weather_load(&garbage, "/tmp/dge_test_weather_garbage.dge"));
    printf("PASS: weather_load rejects bad magic instead of crashing\n");

    printf("test_weather: ALL TESTS PASSED\n");
    return 0;
}
