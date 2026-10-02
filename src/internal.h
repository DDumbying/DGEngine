#ifndef SLIME_INTERNAL_H
#define SLIME_INTERNAL_H

#include "slime/slime.h"
#include "vec3.h"

#define SL_PAIRS_PER_PARTICLE 32
#define SL_WALL_SAMPLES 64

typedef struct {
    sl_collider_desc desc;
    quat rot, prev_rot;
    sl_vec3 prev_pos;
    sl_vec3 pos_t, pos_t0;   /* frame at the current substep and the one before */
    quat rot_t, rot_t0;
} collider;

typedef struct { int i, j; float r; } pair;   /* contacts store the shock factor in r */

typedef struct {
    int table_size;   /* power of two */
    float cell;   /* kernel radius plus the pair margin */
    int *start;       /* table_size + 1 */
    int *sorted;      /* particle indices grouped by bucket */
    sl_vec3 *sorted_p;
    int *bucket;      /* bucket of each particle */
} grid;

struct sl_world {
    sl_allocator alloc;
    int max_particles, count, substeps, iterations;
    float radius, spacing, h, w_rest, sleep_speed;
    float wall_table[SL_WALL_SAMPLES + 1];
    sl_vec3 gravity;

    sl_vec3 *x, *p, *v, *delta, *x_step, *grad, *wall_grad;
    float *inv_mass, *lambda, *rho, *grad2;
    sl_material *mat;
    unsigned char *touch, *fluid;
    pair *pairs, *contacts;
    int pair_count, pair_cap, contact_count;

    sl_material_desc materials[SL_MAX_MATERIALS];
    int material_count;
    collider colliders[SL_MAX_COLLIDERS];
    int collider_count;

    grid g;
};

void *sl__alloc(sl_world *w, size_t size);
void sl__free(sl_world *w, void *ptr);

int grid_init(sl_world *w);
void grid_free(sl_world *w);
void grid_build(sl_world *w);
void grid_pairs(sl_world *w);

float kernel(float r, float h);
void wall_table_init(sl_world *w);
void collider_frames(sl_world *w, float t);
float wall_density(const sl_world *w, int i, sl_vec3 *grad);
void collide_particles(sl_world *w);
void solve_substep(sl_world *w, float hs, float t);

#endif
