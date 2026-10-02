#ifndef SLIME_H
#define SLIME_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SLIME_VERSION_MAJOR 0
#define SLIME_VERSION_MINOR 1
#define SLIME_VERSION_PATCH 0

#define SL_MAX_MATERIALS 16
#define SL_MAX_COLLIDERS 64

typedef struct { float x, y, z; } sl_vec3;

typedef struct sl_world sl_world;
typedef int sl_material;
typedef int sl_collider;

typedef enum { SL_FLUID, SL_GRANULAR } sl_kind;
typedef enum { SL_PLANE, SL_BOX, SL_SPHERE, SL_CAPSULE } sl_shape;

typedef struct {
    void *(*alloc)(size_t size, void *user);
    void (*free)(void *ptr, void *user);
    void *user;
} sl_allocator;

typedef struct {
    int max_particles;
    float particle_radius;   /* particles sit 2 * radius apart at rest */
    sl_vec3 gravity;
    int substeps;            /* 0 means 6 */
    int iterations;          /* contact passes per substep, 0 means 4 */
    float sleep_speed;       /* grains in contact slower than this stay put; 0 means radius/s, < 0 off */
    sl_allocator allocator;  /* all zero means malloc/free */
} sl_world_desc;

typedef struct {
    sl_kind kind;
    float density;    /* kg/m^3, sets particle mass */
    float viscosity;  /* fluid: 0..1 velocity smoothing */
    float cohesion;   /* fluid: pull between nearby particles */
    float friction;   /* granular: friction between grains */
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

sl_world *sl_world_create(const sl_world_desc *desc);
void sl_world_destroy(sl_world *w);

/* Returns -1 when SL_MAX_MATERIALS is reached. */
sl_material sl_material_add(sl_world *w, const sl_material_desc *desc);

/* Returns the new particle index, or -1 when full or the material is invalid. */
int sl_spawn(sl_world *w, sl_material m, sl_vec3 pos, sl_vec3 vel);
/* Fills the box with particles at rest spacing, returns how many were added. */
int sl_spawn_box(sl_world *w, sl_material m, sl_vec3 min, sl_vec3 max);
/* Moves the last particle into index, so indices above it shift. */
void sl_remove(sl_world *w, int index);
void sl_clear(sl_world *w);

/* Returns -1 when SL_MAX_COLLIDERS is reached. */
sl_collider sl_collider_add(sl_world *w, const sl_collider_desc *desc);
/* Target transform for the next step; the collider sweeps there and pushes particles. */
void sl_collider_move(sl_world *w, sl_collider c, sl_vec3 position, const float rotation[4]);

void sl_step(sl_world *w, float dt);

int sl_count(const sl_world *w);
const sl_vec3 *sl_positions(const sl_world *w);
const sl_vec3 *sl_velocities(const sl_world *w);
const sl_material *sl_materials(const sl_world *w);
float sl_particle_radius(const sl_world *w);

#ifdef __cplusplus
}
#endif

#endif
