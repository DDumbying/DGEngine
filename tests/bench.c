#include <stdio.h>
#include <string.h>
#include <time.h>
#include "slime/slime.h"

static double now(void) {
    struct timespec t;
    timespec_get(&t, TIME_UTC);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

static sl_world *scene(int workers, float size, int with_sand) {
    sl_world_desc d = {0};
    d.max_particles = 200000;
    d.particle_radius = 0.05f;
    d.gravity = (sl_vec3){0, -9.81f, 0};
    d.workers = workers;
    sl_world *w = sl_world_create(&d);
    sl_material_desc water = {SL_FLUID, 1000, 0.01f, 0, 0, 0, 0};
    sl_material_desc sand = {SL_GRANULAR, 1600, 0, 0, 0.9f, 0, 0};
    sl_material mw = sl_material_add(w, &water), ms = sl_material_add(w, &sand);
    sl_collider_desc box = {0};
    box.shape = SL_BOX;
    box.half_extents = (sl_vec3){size, 2.0f, size};
    box.inside = 1;
    sl_collider_add(w, &box);
    sl_spawn_box(w, mw, (sl_vec3){-size, -2.0f, -size}, (sl_vec3){0, -0.6f, size});
    if (with_sand) sl_spawn_box(w, ms, (sl_vec3){0.1f, -2.0f, -size}, (sl_vec3){size, -1.2f, size});
    return w;
}

static double time_steps(sl_world *w, int frames) {
    double t0 = now();
    for (int i = 0; i < frames; i++) sl_step(w, 1.0f / 60.0f);
    return (now() - t0) * 1000.0 / frames;
}

static void row(const char *name, int workers, float size, int with_sand) {
    sl_world *w = scene(workers, size, with_sand);
    for (int i = 0; i < 20; i++) sl_step(w, 1.0f / 60.0f);
    double moving = time_steps(w, 60);
    for (int i = 0; i < 1500; i++) sl_step(w, 1.0f / 60.0f);
    double settled = time_steps(w, 60);
    sl_stats st;
    sl_get_stats(w, &st);
    printf("%-12s %9d %8d %12.2f %12.2f %9d %11zu\n", name, st.particles, workers, moving, settled, st.awake,
           st.memory_bytes / (size_t)st.particles);
    sl_world_destroy(w);
}

int main(int argc, char **argv) {
    float sizes[] = {0.6f, 1.0f, 1.4f, 1.8f};
    int quick = argc > 1 && strcmp(argv[1], "--quick") == 0;
    printf("%-12s %9s %8s %12s %12s %9s %11s\n", "scene", "particles", "workers", "first 1s ms", "after 26s ms", "awake", "bytes/part");
    for (int s = quick ? 2 : 0; s < 4; s++)
        for (int k = 1; k <= 4; k *= 4) row("dam + sand", k, sizes[s], 1);
    for (int k = 1; k <= 4; k *= 4) row("water pool", k, 1.4f, 0);
    return 0;
}
