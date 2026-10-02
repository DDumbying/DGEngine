#include <stdlib.h>
#include <string.h>
#include "internal.h"

static void *default_alloc(size_t size, void *user) { (void)user; return malloc(size); }
static void default_free(void *ptr, void *user) { (void)user; free(ptr); }

void *sl__alloc(sl_world *w, size_t size) { return w->alloc.alloc(size, w->alloc.user); }
void sl__free(sl_world *w, void *ptr) { if (ptr) w->alloc.free(ptr, w->alloc.user); }

float kernel(float r, float h) { return r < h ? (h - r) * (h - r) * (h - r) : 0.0f; }

/* Density of a perfect lattice at rest spacing, so fluid density can be measured relative to it. */
static float rest_kernel_sum(float d, float h) {
    float sum = 0;
    int n = (int)ceilf(h / d);
    for (int i = -n; i <= n; i++)
        for (int j = -n; j <= n; j++)
            for (int k = -n; k <= n; k++)
                sum += kernel(d * sqrtf((float)(i * i + j * j + k * k)), h);
    return sum;
}

sl_world *sl_world_create(const sl_world_desc *desc) {
    if (!desc || desc->max_particles <= 0 || desc->particle_radius <= 0) return NULL;

    sl_allocator a = desc->allocator;
    if (!a.alloc || !a.free) { a.alloc = default_alloc; a.free = default_free; a.user = NULL; }

    sl_world *w = a.alloc(sizeof(sl_world), a.user);
    if (!w) return NULL;
    memset(w, 0, sizeof *w);
    w->alloc = a;
    w->max_particles = desc->max_particles;
    w->substeps = desc->substeps > 0 ? desc->substeps : 6;
    w->iterations = desc->iterations > 0 ? desc->iterations : 4;
    w->sleep_speed = desc->sleep_speed == 0 ? desc->particle_radius : desc->sleep_speed;
    w->radius = desc->particle_radius;
    w->spacing = 2.0f * w->radius;
    w->h = 1.8f * w->spacing;
    w->w_rest = rest_kernel_sum(w->spacing, w->h);
    wall_table_init(w);
    w->gravity = desc->gravity;

    size_t n = (size_t)w->max_particles;
    w->x = sl__alloc(w, n * sizeof(sl_vec3));
    w->p = sl__alloc(w, n * sizeof(sl_vec3));
    w->v = sl__alloc(w, n * sizeof(sl_vec3));
    w->delta = sl__alloc(w, n * sizeof(sl_vec3));
    w->x_step = sl__alloc(w, n * sizeof(sl_vec3));
    w->grad = sl__alloc(w, n * sizeof(sl_vec3));
    w->wall_grad = sl__alloc(w, n * sizeof(sl_vec3));
    w->rho = sl__alloc(w, n * sizeof(float));
    w->grad2 = sl__alloc(w, n * sizeof(float));
    w->touch = sl__alloc(w, n);
    w->fluid = sl__alloc(w, n);
    w->pair_cap = w->max_particles * SL_PAIRS_PER_PARTICLE;
    w->pairs = sl__alloc(w, (size_t)w->pair_cap * sizeof(pair));
    w->contacts = sl__alloc(w, (size_t)w->pair_cap * sizeof(pair));
    w->inv_mass = sl__alloc(w, n * sizeof(float));
    w->lambda = sl__alloc(w, n * sizeof(float));
    w->mat = sl__alloc(w, n * sizeof(sl_material));

    if (!w->x || !w->p || !w->v || !w->delta || !w->x_step || !w->grad || !w->wall_grad || !w->rho || !w->grad2
        || !w->touch || !w->fluid || !w->pairs || !w->contacts || !w->inv_mass || !w->lambda || !w->mat
        || !grid_init(w)) {
        sl_world_destroy(w);
        return NULL;
    }
    return w;
}

void sl_world_destroy(sl_world *w) {
    if (!w) return;
    sl__free(w, w->x);
    sl__free(w, w->p);
    sl__free(w, w->v);
    sl__free(w, w->delta);
    sl__free(w, w->x_step);
    sl__free(w, w->grad);
    sl__free(w, w->wall_grad);
    sl__free(w, w->rho);
    sl__free(w, w->grad2);
    sl__free(w, w->touch);
    sl__free(w, w->fluid);
    sl__free(w, w->pairs);
    sl__free(w, w->contacts);
    sl__free(w, w->inv_mass);
    sl__free(w, w->lambda);
    sl__free(w, w->mat);
    grid_free(w);
    sl_allocator a = w->alloc;
    a.free(w, a.user);
}

sl_material sl_material_add(sl_world *w, const sl_material_desc *desc) {
    if (!w || !desc || w->material_count >= SL_MAX_MATERIALS) return -1;
    sl_material_desc m = *desc;
    if (m.density <= 0) m.density = 1000.0f;
    w->materials[w->material_count] = m;
    return w->material_count++;
}

int sl_spawn(sl_world *w, sl_material m, sl_vec3 pos, sl_vec3 vel) {
    if (!w || m < 0 || m >= w->material_count || w->count >= w->max_particles) return -1;
    int i = w->count++;
    float d = w->spacing;
    w->x[i] = pos;
    w->p[i] = pos;
    w->v[i] = vel;
    w->mat[i] = m;
    w->inv_mass[i] = 1.0f / (w->materials[m].density * d * d * d);
    return i;
}

static float jitter(int i, int j, int k) {
    unsigned h = (unsigned)i * 73856093u ^ (unsigned)j * 19349663u ^ (unsigned)k * 83492791u;
    h = (h ^ (h >> 13)) * 0x5bd1e995u;
    h ^= h >> 15;
    return (float)(h & 0xffff) / 65535.0f - 0.5f;
}

int sl_spawn_box(sl_world *w, sl_material m, sl_vec3 min, sl_vec3 max) {
    if (!w) return 0;
    float d = w->spacing, r = w->radius;
    int nx = (int)floorf((max.x - min.x - 2 * r) / d + 1e-4f) + 1;
    int ny = (int)floorf((max.y - min.y - 2 * r) / d + 1e-4f) + 1;
    int nz = (int)floorf((max.z - min.z - 2 * r) / d + 1e-4f) + 1;
    int added = 0;
    for (int j = 0; j < ny; j++)
        for (int k = 0; k < nz; k++)
            for (int i = 0; i < nx; i++) {
                sl_vec3 pos = v3(min.x + r + i * d + jitter(i, j, k) * 0.02f * d,
                                 min.y + r + j * d,
                                 min.z + r + k * d + jitter(k, i, j) * 0.02f * d);
                if (sl_spawn(w, m, pos, v3(0, 0, 0)) < 0) return added;
                added++;
            }
    return added;
}

void sl_remove(sl_world *w, int index) {
    if (!w || index < 0 || index >= w->count) return;
    int last = --w->count;
    w->x[index] = w->x[last];
    w->p[index] = w->p[last];
    w->v[index] = w->v[last];
    w->mat[index] = w->mat[last];
    w->inv_mass[index] = w->inv_mass[last];
}

void sl_clear(sl_world *w) { if (w) w->count = 0; }

sl_collider sl_collider_add(sl_world *w, const sl_collider_desc *desc) {
    if (!w || !desc || w->collider_count >= SL_MAX_COLLIDERS) return -1;
    collider *c = &w->colliders[w->collider_count];
    c->desc = *desc;
    if (c->desc.shape == SL_PLANE) {
        sl_vec3 n = c->desc.normal;
        float len = v3_len(n);
        c->desc.normal = len > 0 ? v3_scale(n, 1.0f / len) : v3(0, 1, 0);
    }
    c->rot = c->prev_rot = q_from(desc->rotation);
    c->prev_pos = desc->position;
    return w->collider_count++;
}

void sl_collider_move(sl_world *w, sl_collider id, sl_vec3 position, const float rotation[4]) {
    if (!w || id < 0 || id >= w->collider_count) return;
    collider *c = &w->colliders[id];
    c->desc.position = position;
    if (rotation) c->rot = q_from(rotation);
}

void sl_step(sl_world *w, float dt) {
    if (!w || dt <= 0) return;
    float hs = dt / (float)w->substeps;
    for (int i = 0; i < w->count; i++) {
        w->x_step[i] = w->p[i] = w->x[i];
        w->touch[i] = 0;
        w->fluid[i] = w->materials[w->mat[i]].kind == SL_FLUID;
    }
    grid_build(w);
    grid_pairs(w);
    for (int s = 0; s < w->substeps; s++)
        solve_substep(w, hs, (float)(s + 1) / (float)w->substeps);

    /* PBD lets resting grains creep; settled grains in contact are held in place instead. */
    float limit = w->sleep_speed * w->sleep_speed;
    for (int i = 0; i < w->count && w->sleep_speed > 0; i++) {
        if (!w->touch[i] || w->fluid[i]) continue;
        if (v3_len2(v3_scale(v3_sub(w->x[i], w->x_step[i]), 1.0f / dt)) >= limit) continue;
        w->x[i] = w->x_step[i];
        w->v[i] = v3(0, 0, 0);
    }
    for (int i = 0; i < w->collider_count; i++) {
        w->colliders[i].prev_pos = w->colliders[i].desc.position;
        w->colliders[i].prev_rot = w->colliders[i].rot;
    }
}

int sl_count(const sl_world *w) { return w ? w->count : 0; }
const sl_vec3 *sl_positions(const sl_world *w) { return w ? w->x : NULL; }
const sl_vec3 *sl_velocities(const sl_world *w) { return w ? w->v : NULL; }
const sl_material *sl_materials(const sl_world *w) { return w ? w->mat : NULL; }
float sl_particle_radius(const sl_world *w) { return w ? w->radius : 0.0f; }
