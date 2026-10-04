#ifndef SLIME_H
#define SLIME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SLIME_VERSION_MAJOR 0
#define SLIME_VERSION_MINOR 6
#define SLIME_VERSION_PATCH 0

/* Shared library builds export only the public API; static builds need nothing. */
#if defined(SLIME_SHARED) && defined(_WIN32)
#  ifdef SLIME_BUILDING
#    define SL_API __declspec(dllexport)
#  else
#    define SL_API __declspec(dllimport)
#  endif
#elif defined(SLIME_SHARED) && defined(__GNUC__)
#  define SL_API __attribute__((visibility("default")))
#else
#  define SL_API
#endif

#define SL_MAX_MATERIALS 16
#define SL_MAX_COLLIDERS 64

typedef struct { float x, y, z; } sl_vec3;

typedef struct sl_world sl_world;
typedef int sl_material;
typedef int sl_collider;
typedef int sl_object;     /* stays dead once destroyed, even after its slot is reused or the world is cleared */
typedef int sl_particle;   /* stable id, valid until the particle is removed; after that it reads as dead, even
                              once the particle's slot is reused or the world is cleared */

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
    int anisotropy;          /* compute fluid surface ellipsoids each step, see sl_anisotropy */
    int max_diffuse;         /* spray, foam and bubble particles, 0 means none */
    sl_task_system tasks;    /* optional, use your own job system instead of the pool */
    sl_allocator allocator;  /* all zero means malloc/free */
} sl_world_desc;

typedef struct {
    sl_kind kind;
    float density;    /* kg/m^3, sets particle mass */
    float viscosity;  /* fluid: 0..1 velocity smoothing */
    float cohesion;   /* fluid: pull between nearby particles */
    float friction;   /* granular and solid: friction on contact; for sand it sets how steep piles get */
    float vorticity;  /* fluid: keeps swirls alive, around 0.1 */
    float damping;    /* share of velocity lost per second, like air drag */
    float wet_cohesion;      /* granular: how strongly wet grains stick together, 0..1 */
} sl_material_desc;

typedef struct {
    sl_shape shape;
    sl_vec3 position;
    float rotation[4];      /* quaternion x, y, z, w; all zero means identity */
    sl_vec3 half_extents;   /* box size, capsule half height in y */
    float radius;           /* sphere and capsule */
    sl_vec3 normal;         /* plane, in the collider's frame so rotation turns it; zero means +y */
    int inside;             /* keep particles inside instead of outside */
    float friction;
} sl_collider_desc;

typedef struct {
    int particles, awake, pairs, contacts, islands, rebuilds;
    size_t memory_bytes;
} sl_stats;

/* Version the library was built as, (major << 16) | (minor << 8) | patch, to check against the header. */
SL_API unsigned sl_version(void);
/* 1 when this build computes floats exactly like the reference, which bit-identical results across machines
   need; 0 means something like fast math or fused multiply-add got into the build. slime sets round to
   nearest without flush-to-zero while it works and restores the caller's mode afterwards. */
SL_API int sl_deterministic(void);

SL_API sl_world *sl_world_create(const sl_world_desc *desc);
SL_API void sl_world_destroy(sl_world *w);

/* Returns -1 when SL_MAX_MATERIALS is reached, or for an unknown kind or a negative or non-finite value. */
SL_API sl_material sl_material_add(sl_world *w, const sl_material_desc *desc);
/* Changes a material for every particle made of it while the world runs: density changes their mass, kind
   changes how loose particles behave (particles of ropes, cloth and soft bodies stay solid). Validated like
   sl_material_add; returns 1 on success, 0 leaving the material unchanged. */
SL_API int sl_material_set(sl_world *w, sl_material m, const sl_material_desc *desc);
SL_API int sl_material_get(const sl_world *w, sl_material m, sl_material_desc *out);
/* Gravity can change at any time; sleeping particles wake to feel it. Non-finite values are ignored. */
SL_API void sl_set_gravity(sl_world *w, sl_vec3 gravity);
SL_API sl_vec3 sl_gravity(const sl_world *w);

/* Returns -1 when full or the material is invalid. */
SL_API sl_particle sl_spawn(sl_world *w, sl_material m, sl_vec3 pos, sl_vec3 vel);
/* Fills the box with particles at rest spacing, returns how many were added. */
SL_API int sl_spawn_box(sl_world *w, sl_material m, sl_vec3 min, sl_vec3 max);
/* Particles that belong to an object can only go with sl_object_destroy. Returns 1 on success. */
SL_API int sl_remove(sl_world *w, sl_particle p);
/* Removes many loose particles in one pass, which is much cheaper than sl_remove in a loop; ids that are dead,
   repeated or belong to an object are skipped. Returns how many went. */
SL_API int sl_remove_many(sl_world *w, const sl_particle *ids, int count);
SL_API void sl_clear(sl_world *w);

SL_API int sl_alive(const sl_world *w, sl_particle p);
SL_API sl_vec3 sl_position(const sl_world *w, sl_particle p);
SL_API sl_vec3 sl_velocity(const sl_world *w, sl_particle p);
SL_API void sl_set_position(sl_world *w, sl_particle p, sl_vec3 pos);
SL_API void sl_set_velocity(sl_world *w, sl_particle p, sl_vec3 vel);
/* A pinned particle ignores forces and is only moved by sl_set_position. */
SL_API void sl_pin(sl_world *w, sl_particle p, int pinned);

/* Returns -1 when SL_MAX_COLLIDERS is reached, or for an unknown shape or a negative or non-finite value. */
SL_API sl_collider sl_collider_add(sl_world *w, const sl_collider_desc *desc);
/* Changes shape, size, normal, inside and friction at once; position and rotation become the target for the
   next step, as with sl_collider_move. Validated like sl_collider_add; returns 1 on success, 0 otherwise. */
SL_API int sl_collider_set(sl_world *w, sl_collider c, const sl_collider_desc *desc);
/* Target transform for the next step; the collider sweeps there and pushes particles. Non-finite input is ignored. */
SL_API void sl_collider_move(sl_world *w, sl_collider c, sl_vec3 position, const float rotation[4]);
/* Force particles put on the collider during the last step, to feed a rigid-body engine. Each particle can load
   up to four colliders at once. */
SL_API sl_vec3 sl_collider_force(const sl_world *w, sl_collider c);
SL_API void sl_collider_set_enabled(sl_world *w, sl_collider c, int enabled);
SL_API int sl_collider_enabled(const sl_world *w, sl_collider c);
/* The id may be handed out again by a later sl_collider_add. */
SL_API void sl_collider_remove(sl_world *w, sl_collider c);

/* Nearest particle hit by a ray, -1 if none; hit_dist may be NULL. */
SL_API sl_particle sl_raycast(const sl_world *w, sl_vec3 origin, sl_vec3 dir, float max_dist, float *hit_dist);
/* Particles whose centers lie in a region described like a collider: box, sphere, capsule or plane (the solid
   half-space behind its normal), rotated and positioned, with inside flipping it. materials is a bit mask (bit m for material m), 0 for all. Up to cap
   ids are written in slot order and the full count is returned; ids and out may be NULL. out gets the count,
   total mass and mass-weighted center and velocity, enough for "is this under water", "how much sand is in the
   bucket" or drag and buoyancy on a game's own bodies. One pass over the particles; read only. */
typedef struct { int count; float mass; sl_vec3 center, velocity; } sl_query_result;
SL_API int sl_query(const sl_world *w, const sl_collider_desc *shape, unsigned materials, sl_particle *ids, int cap,
                    sl_query_result *out);
/* Hold a particle and move it to target each step; on release it keeps its velocity, so it can be thrown. */
SL_API int sl_grab_begin(sl_world *w, sl_particle p);
SL_API void sl_grab_move(sl_world *w, sl_particle p, sl_vec3 target);
SL_API void sl_grab_end(sl_world *w, sl_particle p);
/* Removes loose particles inside the sphere and whole objects that reach into it; returns the count. */
SL_API int sl_remove_sphere(sl_world *w, sl_vec3 center, float radius);

/* Compliance is softness: 0 is stiff, larger stretches more. Particles are spaced about 2 * radius. */
SL_API sl_object sl_rope_create(sl_world *w, sl_material m, sl_vec3 a, sl_vec3 b, float compliance);
SL_API sl_object sl_cloth_create(sl_world *w, sl_material m, sl_vec3 origin, sl_vec3 u, sl_vec3 v,
                          float stretch_compliance, float bend_compliance);
/* stiffness 0..1 pulls back to shape, plasticity 0..1 keeps dents. */
SL_API sl_object sl_softbody_create_box(sl_world *w, sl_material m, sl_vec3 min, sl_vec3 max,
                                 float stiffness, float plasticity);
/* A rope along a polyline of count >= 2 points, resampled at particle spacing. */
SL_API sl_object sl_rope_create_path(sl_world *w, sl_material m, const sl_vec3 *points, int count, float compliance);
/* A soft body of any shape, one particle per point; space the points about 2 * radius apart, for example a mesh
   sampled on a lattice. Clusters work as for the box version, so stiffness and plasticity mean the same. */
SL_API sl_object sl_softbody_create(sl_world *w, sl_material m, const sl_vec3 *points, int count,
                                    float stiffness, float plasticity);
SL_API void sl_object_destroy(sl_world *w, sl_object o);
/* Particle ids of the object in creation order (rope from a to b, cloth row by row). */
SL_API int sl_object_particles(const sl_world *w, sl_object o, const sl_particle **ids);
/* Cloth grid size, 0 for other objects. */
SL_API void sl_object_grid(const sl_world *w, sl_object o, int *nu, int *nv);

SL_API void sl_step(sl_world *w, float dt);

/* Bulk access in internal order, which changes between steps; sl_ids maps each slot to its id. */
SL_API int sl_count(const sl_world *w);
SL_API const sl_vec3 *sl_positions(const sl_world *w);
SL_API const sl_vec3 *sl_velocities(const sl_world *w);
SL_API const sl_material *sl_materials(const sl_world *w);
SL_API const sl_particle *sl_ids(const sl_world *w);

/* 0 dry to 255 soaked, per slot; grains touching fluid get wet and dry over a few seconds. */
SL_API const unsigned char *sl_wetness(const sl_world *w);
/* Per slot: smoothed center then three ellipsoid axes, 4 vectors each; needs desc.anisotropy. */
SL_API const sl_vec3 *sl_anisotropy(const sl_world *w);

typedef enum { SL_SPRAY, SL_FOAM, SL_BUBBLE } sl_diffuse_kind;
/* Spray, foam and bubbles thrown off by fast water; any output pointer may be NULL. */
SL_API int sl_diffuse(const sl_world *w, const sl_vec3 **positions, const sl_vec3 **velocities, const unsigned char **kinds,
               const float **life);

/* Snapshots are plain bytes in a fixed little-endian layout: load them on any platform, with the same slime
   version and a world created with the same particle radius, max_particles, substeps, iterations, fluid
   iterations, sleep speed and anisotropy setting. They hold the gameplay state: particles, objects,
   materials, colliders, gravity and grabs. Spray is cleared and surface ellipsoids are rebuilt on load.
   Handles stay valid across a save and load. Determinism assumes allocations succeed. */
SL_API size_t sl_snapshot_size(const sl_world *w);
/* Returns the bytes written, or 0 when cap is smaller than sl_snapshot_size. */
SL_API size_t sl_snapshot_save(const sl_world *w, void *buf, size_t cap);
typedef enum {
    SL_SNAPSHOT_OK,
    SL_SNAPSHOT_TRUNCATED,   /* shorter than the snapshot it starts */
    SL_SNAPSHOT_CORRUPT,     /* not a snapshot, or damaged */
    SL_SNAPSHOT_VERSION,     /* saved by another slime version */
    SL_SNAPSHOT_SETTINGS,    /* saved by a world created with other settings */
    SL_SNAPSHOT_NO_MEMORY
} sl_snapshot_result;
/* Replaces the whole world state; on failure the world is left exactly as it was. */
SL_API sl_snapshot_result sl_snapshot_load(sl_world *w, const void *buf, size_t size);
/* Hash of exactly what a snapshot holds, to compare across machines each frame and catch desyncs. */
SL_API uint64_t sl_state_hash(const sl_world *w);

SL_API float sl_particle_radius(const sl_world *w);
SL_API void sl_get_stats(const sl_world *w, sl_stats *out);

#ifdef __cplusplus
}
#endif

#endif
