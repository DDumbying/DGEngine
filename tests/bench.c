#include <stdio.h>
#include <time.h>
#include "slime/slime.h"

static double now(void) {
    struct timespec t;
    timespec_get(&t, TIME_UTC);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

static void run(float size) {
    sl_world_desc d = {0};
    d.max_particles = 100000;
    d.particle_radius = 0.05f;
    d.gravity = (sl_vec3){0, -9.81f, 0};
    sl_world *w = sl_world_create(&d);
    sl_material_desc water = {SL_FLUID, 1000, 0.01f, 0, 0};
    sl_material_desc sand = {SL_GRANULAR, 1600, 0, 0, 0.9f};
    sl_material mw = sl_material_add(w, &water), ms = sl_material_add(w, &sand);
    sl_collider_desc box = {0};
    box.shape = SL_BOX;
    box.half_extents = (sl_vec3){size, 2.0f, size};
    box.inside = 1;
    sl_collider_add(w, &box);
    sl_spawn_box(w, mw, (sl_vec3){-size, -2.0f, -size}, (sl_vec3){0, -0.6f, size});
    sl_spawn_box(w, ms, (sl_vec3){0.1f, -2.0f, -size}, (sl_vec3){size, -1.2f, size});

    for (int i = 0; i < 30; i++) sl_step(w, 1.0f / 60.0f);
    int frames = 120;
    double t0 = now();
    for (int i = 0; i < frames; i++) sl_step(w, 1.0f / 60.0f);
    double ms_step = (now() - t0) * 1000.0 / frames;
    printf("%6d particles: %6.2f ms/step\n", sl_count(w), ms_step);
    sl_world_destroy(w);
}

int main(void) {
    run(0.6f);
    run(1.0f);
    run(1.4f);
    return 0;
}
