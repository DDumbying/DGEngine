/* A game using slime the way the README describes; CI builds it against the installed package and as a subdirectory. */
#include <stdio.h>
#include <slime/slime.h>

int main(void) {
    sl_world_desc desc = {0};
    desc.max_particles = 1000;
    desc.particle_radius = 0.05f;
    desc.gravity = (sl_vec3){0, -9.81f, 0};
    sl_world *w = sl_world_create(&desc);
    if (!w) return 1;
    sl_material water = sl_material_add(w, &(sl_material_desc){.kind = SL_FLUID, .density = 1000});
    sl_collider_add(w, &(sl_collider_desc){.shape = SL_PLANE});
    sl_spawn_box(w, water, (sl_vec3){-0.2f, 0, -0.2f}, (sl_vec3){0.2f, 0.4f, 0.2f});
    for (int i = 0; i < 30; i++) sl_step(w, 1.0f / 60.0f);
    printf("slime %u.%u.%u, %d particles\n", sl_version() >> 16, (sl_version() >> 8) & 255, sl_version() & 255, sl_count(w));
    int ok = sl_count(w) > 0 && sl_version() == ((SLIME_VERSION_MAJOR << 16) | (SLIME_VERSION_MINOR << 8) | SLIME_VERSION_PATCH);
    sl_world_destroy(w);
    return ok ? 0 : 1;
}
