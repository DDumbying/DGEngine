#ifndef SLIME_H
#define SLIME_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SLIME_VERSION_MAJOR 0
#define SLIME_VERSION_MINOR 2
#define SLIME_VERSION_PATCH 0

#define SL_MAX_MATERIALS 16
#define SL_MAX_COLLIDERS 64

typedef struct { float x, y, z; } sl_vec3;

typedef struct sl_world sl_world;
typedef int sl_material;
typedef int sl_collider;
typedef int sl_object;
typedef int sl_particle;   /* stable id, stays valid until the particle is removed */

typedef enum { SL_FLUID, SL_GRANULAR, SL_SOLID } sl_kind;
typedef enum { SL_PLANE, SL_BOX, SL_SPHERE, SL_CAPSULE } sl_shape;

typedef struct {
    void *(*alloc)(size_t size, void *user);
    void (*free)(void *ptr, void *user);
    void *user;
} sl_allocator;

/* Runs task over chunks [0, count) and returns once all are done; chunks may run on any thread. */
typedef void sl_task_fn(int first, int last, void *ctx);
typedef struct {
    void (*parallel_for)(sl_task_fn *task, int count, void *ctx, void *user);
    void *user;
} sl_task_system;

typedef struct {
    int max_particles;
    float particle_radius;   /* particles sit 2 * radius apart at rest */
    sl_vec3 gravity;
    int substeps;            /* 0 means 4 */
    int iterations;          /* contact passes per substep, 0 means 4 */
    int fluid_iterations;    /* fluid pressure passes per substep, 0 means 2 */
    float sleep_speed;       /* resting things slower than this sleep; 0 means radius/s, < 0 off */
    int workers;             /* threads for the built-in pool, 0 or 1 means single threaded */
    sl_task_system tasks;    /* optional, use your own job system instead of the pool */
    sl_allocator allocator;  /* all zero means malloc/free */
} sl_world_desc;

typedef struct {
    sl_kind kind;
    float density;    /* kg/m^3, sets particle mass */
    float viscosity;  /* fluid: 0..1 velocity smoothing */
    float cohesion;   /* fluid: pull between nearby particles */
    float friction;   /* granular and solid: friction on contact */
    float vorticity;  /* fluid: keeps swirls alive, around 0.1 */
    float damping;    /* share of velocity lost per second, like air drag */
} sl_material_desc;

typedef struct {
    sl_shape shape;
    sl_vec3 position;
    float rotation[4];      /* quaternion x, y, z, w; all zero means identity */
    sl_vec3 half_extents;   /* box size, capsule half height in y */
    float radius;           /* sphere and capsule */
    sl_vec3 normal;         /* plane; zero means +y */
    int inside;             /* keep particles inside instead of outside */
    float friction;
} sl_collider_desc;

typedef struct {
    int particles, awake, pairs, contacts, islands, rebuilds;
    size_t memory_bytes;
} sl_stats;

sl_world *sl_world_create(const sl_world_desc *desc);
void sl_world_destroy(sl_world *w);

/* Returns -1 when SL_MAX_MATERIALS is reached. */
sl_material sl_material_add(sl_world *w, const sl_material_desc *desc);

/* Returns -1 when full or the material is invalid. */
sl_particle sl_spawn(sl_world *w, sl_material m, sl_vec3 pos, sl_vec3 vel);
/* Fills the box with particles at rest spacing, returns how many were added. */
int sl_spawn_box(sl_world *w, sl_material m, sl_vec3 min, sl_vec3 max);
/* Particles that belong to an object can only go with sl_object_destroy. Returns 1 on success. */
int sl_remove(sl_world *w, sl_particle p);
void sl_clear(sl_world *w);

int sl_alive(const sl_world *w, sl_particle p);
sl_vec3 sl_position(const sl_world *w, sl_particle p);
sl_vec3 sl_velocity(const sl_world *w, sl_particle p);
void sl_set_position(sl_world *w, sl_particle p, sl_vec3 pos);
void sl_set_velocity(sl_world *w, sl_particle p, sl_vec3 vel);
/* A pinned particle ignores forces and is only moved by sl_set_position. */
void sl_pin(sl_world *w, sl_particle p, int pinned);

/* Returns -1 when SL_MAX_COLLIDERS is reached. */
sl_collider sl_collider_add(sl_world *w, const sl_collider_desc *desc);
/* Target transform for the next step; the collider sweeps there and pushes particles. */
void sl_collider_move(sl_world *w, sl_collider c, sl_vec3 position, const float rotation[4]);
/* Force particles put on the collider during the last step, to feed a rigid-body engine. */
sl_vec3 sl_collider_force(const sl_world *w, sl_collider c);

/* Compliance is softness: 0 is stiff, larger stretches more. Particles are spaced about 2 * radius. */
sl_object sl_rope_create(sl_world *w, sl_material m, sl_vec3 a, sl_vec3 b, float compliance);
sl_object sl_cloth_create(sl_world *w, sl_material m, sl_vec3 origin, sl_vec3 u, sl_vec3 v,
                          float stretch_compliance, float bend_compliance);
/* stiffness 0..1 pulls back to shape, plasticity 0..1 keeps dents. */
sl_object sl_softbody_create_box(sl_world *w, sl_material m, sl_vec3 min, sl_vec3 max,
                                 float stiffness, float plasticity);
void sl_object_destroy(sl_world *w, sl_object o);
/* Particle ids of the object in creation order (rope from a to b, cloth row by row). */
int sl_object_particles(const sl_world *w, sl_object o, const sl_particle **ids);
/* Cloth grid size, 0 for other objects. */
void sl_object_grid(const sl_world *w, sl_object o, int *nu, int *nv);

void sl_step(sl_world *w, float dt);

/* Bulk access in internal order, which changes between steps; sl_ids maps each slot to its id. */
int sl_count(const sl_world *w);
const sl_vec3 *sl_positions(const sl_world *w);
const sl_vec3 *sl_velocities(const sl_world *w);
const sl_material *sl_materials(const sl_world *w);
const sl_particle *sl_ids(const sl_world *w);

float sl_particle_radius(const sl_world *w);
void sl_get_stats(const sl_world *w, sl_stats *out);

#ifdef __cplusplus
}
#endif

#endif
