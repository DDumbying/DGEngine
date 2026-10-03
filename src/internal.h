#ifndef SLIME_INTERNAL_H
#define SLIME_INTERNAL_H

#include "slime/slime.h"
#include "vec3.h"

#define SL_CHUNK 256
#define SL_COLOR_CHUNK 64
#define SL_WALL_SAMPLES 64
#define SL_MAX_COLORS 64
#define SL_CALM_STEPS 30

enum { F_FLUID = 1, F_PINNED = 2, F_TOUCH = 4, F_AWAKE = 8, F_WET = 16, F_GRAB = 32 };
#define F_KINEMATIC (F_PINNED | F_GRAB)   /* moved only by the user, never by the solver */
#define SL_MAX_GRABS 16

typedef struct {
    sl_collider_desc desc;
    quat rot, prev_rot;
    sl_vec3 prev_pos;
    sl_vec3 pos_t, pos_t0;   /* frame at the current substep and the one before */
    quat rot_t, rot_t0;
    sl_vec3 force;
    int enabled, removed;
} collider;

typedef struct { sl_particle id; sl_vec3 from, to; } grab;

/* lift: shock propagation factor; mu, dist and glue only change on a rebuild, so they are kept here. */
typedef struct { int i, j; float lift, mu, dist, glue; } contact;
typedef struct { int a, b; float rest, compliance; int obj; } dist_con;
typedef struct { int slot, cluster; sl_vec3 rest; } member;
typedef struct {
    int first, count, obj;
    float stiffness, plasticity, pull;   /* pull: share of the way to the goal per pass */
    quat rot;
    sl_vec3 center;
} cluster;

typedef struct {
    int alive, kind, nu, nv;
    sl_particle *ids;
    int count;
    float self_dist;
} object;

enum { OBJ_ROPE = 1, OBJ_CLOTH, OBJ_SOFT };

typedef struct {
    int dense;            /* dense grid over the bounds, or a hash table when particles are spread out */
    int nx, ny, nz, cells, start_cap, table_size;
    int span;             /* cells to scan on each side; their size covers the search range */
    float cell;
    sl_vec3 origin;
    int *start;           /* cells + 1 */
    int *bucket;          /* per slot */
    int *sorted;          /* slots grouped by cell */
} grid;

typedef struct sl_pool sl_pool;

struct sl_world {
    sl_allocator alloc;
    int max_particles, cap, count, substeps, iterations, fluid_iterations;
    float radius, spacing, h, skin, w_rest, sleep_speed, hs, dt;
    float wall_table[SL_WALL_SAMPLES + 1];
    sl_vec3 gravity;

    /* per slot */
    sl_vec3 *x, *p, *v, *x_step, *x_build, *delta, *tmp;
    sl_vec3 *push;               /* per slot, 2 entries: impulse given to colliders last awake step */
    float *lambda, *inv_mass, *mass;
    unsigned char *flags, *calm, *mat, *wet, *near_fluid, *mark, *push_id;   /* push_id: 2 per slot, collider + 1 or 0 */
    sl_vec3 *aniso;              /* 4 per slot when enabled: center, then three axes */
    int *id, *obj, *island, *order;
    sl_material *material;       /* mat widened to the public type, so sl_materials can hand it out */

    /* stable ids: the low id_bits are an index into id_slot, the rest count how often that index was reused */
    int *id_slot, *free_ids, free_count, next_id, id_bits;

    sl_material_desc materials[SL_MAX_MATERIALS];
    int material_count;
    collider colliders[SL_MAX_COLLIDERS];
    int collider_count;

    grid g;
    int *nbr_off, *nbr, nbr_cap, nbr_r_cap;
    float *nbr_r;                       /* distance per neighbor entry, from the lambda pass */
    int pair_count;           /* close pairs, each counted once */
    int chunk_room;           /* list entries each chunk may write in one pass */
    contact *contacts, *contact_tmp;
    int contact_count, contact_cap, contact_tmp_cap, color_off[SL_MAX_COLORS + 2];
    int *active, active_count;
    int *island_calm, island_count, island_cap;
    int need_rebuild, rebuilds, built;   /* built: particle count when neighbor lists were made */
    int nbr_valid;                       /* neighbor lists still match the current slots */
    int islands_stale;                   /* island labels predate the current neighbor lists */
    unsigned char *colors;               /* graph coloring scratch */
    int color_cap, *chunk_buf, chunk_cap;

    object *objects;
    int object_count, object_cap;
    dist_con *dist, *dist_tmp;
    float *dist_lambda;
    int dist_count, dist_cap, dist_tmp_cap, dist_lambda_cap, dist_color_off[SL_MAX_COLORS + 2];
    cluster *clusters;
    int cluster_count, cluster_cap;
    member *members;
    int member_count, member_cap;
    int *mem_off, *mem_list, mem_list_cap, mem_dirty;   /* per slot: indices into members */


    grab grabs[SL_MAX_GRABS];
    int grab_count;

    int use_aniso, max_diffuse, diffuse_count;
    unsigned step_count;
    sl_vec3 *dpos, *dvel;
    float *dlife;
    unsigned char *dkind;

    sl_task_system tasks;
    sl_pool *pool;
};

/* Phase timers, only in builds made with SLIME_PROFILE; one world at a time. */
enum { P_SORT, P_REORDER, P_PAIRS, P_NEIGHBORS, P_CONTACTS, P_COLOR, P_ISLANDS, P_SKIN, P_STABILIZE, P_PREDICT,
       P_LAMBDA, P_DELTA, P_APPLY, P_SOLIDS, P_OBJECTS, P_COLLIDERS, P_VELOCITY, P_FLUID_STEP, P_EXTRAS, P_STEP, P_COUNT };
#ifdef SLIME_PROFILE
extern double sl__prof[P_COUNT];
extern long sl__dispatches;
double sl__now(void);
#define PROF(ph, stmt) do { double t0_ = sl__now(); stmt; sl__prof[ph] += sl__now() - t0_; } while (0)
#else
#define PROF(ph, stmt) do { stmt; } while (0)
#endif

/* memory */
void *sl__alloc(sl_world *w, size_t size);
void sl__free(sl_world *w, void *ptr);
int sl__grow(sl_world *w, void **ptr, int *cap, int need, size_t elem);

/* threads: fn runs over [begin, end) of count items, chunk is a fixed index for reductions */
typedef void (*sl_range_fn)(sl_world *w, int begin, int end, int chunk, void *ctx);
void sl__parallel(sl_world *w, int count, sl_range_fn fn, void *ctx);
/* Any chunk size, for passes whose result does not depend on how the work is split. */
void sl__parallel_sized(sl_world *w, int count, int size, sl_range_fn fn, void *ctx);
int sl__chunks(int count);
sl_pool *pool_create(sl_world *w, int threads);
void pool_destroy(sl_world *w, sl_pool *p);
void pool_wake(sl_pool *p);
void pool_sleep(sl_pool *p);
void pool_run(sl_pool *p, sl_task_fn *task, int count, void *ctx);

static inline float kernel(float r, float h) { return r < h ? (h - r) * (h - r) * (h - r) : 0.0f; }
static inline float kernel_grad(float r, float h) { return r < h ? -3.0f * (h - r) * (h - r) : 0.0f; }

static inline int id_index(const sl_world *w, sl_particle p) { return (int)((unsigned)p & ((1u << w->id_bits) - 1u)); }
int slot_of(const sl_world *w, sl_particle p);
float bound_radius(const sl_collider_desc *d);
int remove_doomed(sl_world *w, const sl_vec3 *center, float radius);
int color_graph(sl_world *w, const int *ends, int stride, int count, int *offsets);
void run_colors(sl_world *w, const int *offsets, sl_range_fn fn);

/* grid and neighbors */
int grid_rebuild(sl_world *w, int step_start);   /* step_start: also reorder memory and decide who sleeps */
void settle_islands(sl_world *w);

/* solver passes */
void collider_frames(sl_world *w, float t);
void wall_table_init(sl_world *w);
float wall_density(sl_world *w, int i, sl_vec3 *grad, float scale);
void book_push(sl_world *w, int i, int collider, sl_vec3 impulse);
void solve_colliders(sl_world *w);
void solve_substep(sl_world *w, float t);
void stabilize(sl_world *w);
void move_grabs(sl_world *w, float t);
void fluid_step(sl_world *w);
void anisotropy_step(sl_world *w);
void diffuse_step(sl_world *w);
int grid_fluid_near(sl_world *w, sl_vec3 x, sl_vec3 *avg_vel);
float collider_distance(const collider *col, sl_vec3 p, sl_vec3 *n);
void objects_substep(sl_world *w);
void objects_solve(sl_world *w);
void objects_plasticity(sl_world *w);
void objects_remap(sl_world *w, const int *old_to_new);
void objects_drop(sl_world *w, int obj);
void color_dist(sl_world *w);
int objects_membership(sl_world *w);

#endif
