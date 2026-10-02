#include <stdlib.h>
#include <string.h>
#include "internal.h"

static void *default_alloc(size_t size, void *user) { (void)user; return malloc(size); }
static void default_free(void *ptr, void *user) { (void)user; free(ptr); }

void *sl__alloc(sl_world *w, size_t size) { return size ? w->alloc.alloc(size, w->alloc.user) : NULL; }
void sl__free(sl_world *w, void *ptr) { if (ptr) w->alloc.free(ptr, w->alloc.user); }

int sl__grow(sl_world *w, void **ptr, int *cap, int need, size_t elem) {
    if (need <= *cap) return 1;
    int n = *cap > 0 ? *cap : 64;
    while (n < need) n *= 2;
    void *mem = sl__alloc(w, (size_t)n * elem);
    if (!mem) return 0;
    if (*ptr) { memcpy(mem, *ptr, (size_t)*cap * elem); sl__free(w, *ptr); }
    *ptr = mem;
    *cap = n;
    return 1;
}

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

typedef struct { void **ptr; size_t elem; int extra; } slot_array;

/* Every per-slot array, so growing never misses one. */
static int slot_arrays(sl_world *w, slot_array *out) {
    int n = 0;
    void **vecs[] = {(void **)&w->x, (void **)&w->p, (void **)&w->v, (void **)&w->x_step, (void **)&w->x_build,
                     (void **)&w->delta, (void **)&w->tmp};
    for (size_t i = 0; i < sizeof vecs / sizeof vecs[0]; i++) out[n++] = (slot_array){vecs[i], sizeof(sl_vec3), 0};
    void **floats[] = {(void **)&w->lambda, (void **)&w->inv_mass, (void **)&w->mass};
    for (size_t i = 0; i < sizeof floats / sizeof floats[0]; i++) out[n++] = (slot_array){floats[i], sizeof(float), 0};
    void **bytes[] = {(void **)&w->flags, (void **)&w->calm, (void **)&w->mat};
    for (size_t i = 0; i < sizeof bytes / sizeof bytes[0]; i++) out[n++] = (slot_array){bytes[i], 1, 0};
    out[n++] = (slot_array){(void **)&w->push, 2 * sizeof(sl_vec3), 0};
    out[n++] = (slot_array){(void **)&w->push_id, 2, 0};
    void **ints[] = {(void **)&w->id, (void **)&w->obj, (void **)&w->island,
                     (void **)&w->g.bucket, (void **)&w->g.sorted, (void **)&w->active, (void **)&w->id_slot, (void **)&w->free_ids};
    for (size_t i = 0; i < sizeof ints / sizeof ints[0]; i++) out[n++] = (slot_array){ints[i], sizeof(int), 0};
    out[n++] = (slot_array){(void **)&w->order, sizeof(int), 1};
    out[n++] = (slot_array){(void **)&w->nbr_off, sizeof(int), 1};
    out[n++] = (slot_array){(void **)&w->mem_off, sizeof(int), 1};
    return n;
}

static int grow_slots(sl_world *w, int need) {
    if (need <= w->cap) return 1;
    if (need > w->max_particles) return 0;
    int n = w->cap > 0 ? w->cap : 1024;
    while (n < need) n *= 2;
    if (n > w->max_particles) n = w->max_particles;

    slot_array arrays[32];
    int count = slot_arrays(w, arrays);
    for (int i = 0; i < count; i++) {
        size_t old = (size_t)(w->cap + arrays[i].extra) * arrays[i].elem;
        void *mem = sl__alloc(w, (size_t)(n + arrays[i].extra) * arrays[i].elem);
        if (!mem) return 0;
        if (*arrays[i].ptr) { memcpy(mem, *arrays[i].ptr, old); sl__free(w, *arrays[i].ptr); }
        else memset(mem, 0, (size_t)(n + arrays[i].extra) * arrays[i].elem);
        *arrays[i].ptr = mem;
    }

    int table = 1024;
    while (table < 2 * n) table <<= 1;
    w->g.table_size = table;
    w->cap = n;
    return 1;
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
    w->substeps = desc->substeps > 0 ? desc->substeps : 4;
    w->iterations = desc->iterations > 0 ? desc->iterations : 4;
    w->fluid_iterations = desc->fluid_iterations > 0 ? desc->fluid_iterations : 2;
    w->radius = desc->particle_radius;
    w->spacing = 2.0f * w->radius;
    w->h = 1.8f * w->spacing;
    w->skin = 0.3f * w->spacing;
    w->w_rest = rest_kernel_sum(w->spacing, w->h);
    w->sleep_speed = desc->sleep_speed == 0 ? desc->particle_radius : desc->sleep_speed;
    w->gravity = desc->gravity;
    w->g.cell = w->h + w->skin;
    w->tasks = desc->tasks;
    wall_table_init(w);

    if (!grow_slots(w, desc->max_particles < 1024 ? desc->max_particles : 1024)) { sl_world_destroy(w); return NULL; }
    if (!w->tasks.parallel_for && desc->workers > 1) {
        w->pool = pool_create(w, desc->workers - 1);
        if (!w->pool) { sl_world_destroy(w); return NULL; }
    }
    return w;
}

void sl_world_destroy(sl_world *w) {
    if (!w) return;
    pool_destroy(w, w->pool);
    slot_array arrays[32];
    int count = slot_arrays(w, arrays);
    for (int i = 0; i < count; i++) sl__free(w, *arrays[i].ptr);
    sl__free(w, w->g.start);
    sl__free(w, w->nbr);
    sl__free(w, w->nbr_r);
    sl__free(w, w->contacts);
    sl__free(w, w->contact_tmp);
    sl__free(w, w->island_calm);
    for (int i = 0; i < w->object_count; i++) sl__free(w, w->objects[i].ids);
    sl__free(w, w->objects);
    sl__free(w, w->dist);
    sl__free(w, w->dist_lambda);
    sl__free(w, w->clusters);
    sl__free(w, w->members);
    sl__free(w, w->mem_list);
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

static int slot_of(const sl_world *w, sl_particle p) {
    if (!w || p < 0 || p >= w->next_id) return -1;
    int s = w->id_slot[p];
    return s >= 0 && s < w->count && w->id[s] == p ? s : -1;
}

sl_particle sl_spawn(sl_world *w, sl_material m, sl_vec3 pos, sl_vec3 vel) {
    if (!w || m < 0 || m >= w->material_count || !v3_finite(pos) || !v3_finite(vel)) return -1;
    if (w->count >= w->max_particles || !grow_slots(w, w->count + 1)) return -1;
    int s = w->count++;
    int id = w->free_count > 0 ? w->free_ids[--w->free_count] : w->next_id++;
    float d = w->spacing;
    w->x[s] = w->p[s] = w->x_step[s] = w->x_build[s] = pos;
    w->v[s] = vel;
    w->mat[s] = (unsigned char)m;
    w->mass[s] = w->materials[m].density * d * d * d;
    w->inv_mass[s] = 1.0f / w->mass[s];
    w->flags[s] = w->materials[m].kind == SL_FLUID ? F_FLUID : 0;
    w->calm[s] = 0;
    w->obj[s] = -1;
    w->push_id[2 * s] = w->push_id[2 * s + 1] = 0;
    w->id[s] = id;
    w->id_slot[id] = s;
    w->need_rebuild = 1;
    return id;
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

static void wake_near(sl_world *w, sl_vec3 c, float reach) {
    for (int s = 0; s < w->count; s++)
        if (v3_len2(v3_sub(w->x[s], c)) < reach * reach) w->calm[s] = 0;
}

/* Moves the last slot into s; used by removal and object destruction. */
static void remove_slot(sl_world *w, int s) {
    int last = --w->count;
    w->free_ids[w->free_count++] = w->id[s];
    if (s != last) {
        w->x[s] = w->x[last]; w->p[s] = w->p[last]; w->v[s] = w->v[last];
        w->x_step[s] = w->x_step[last]; w->x_build[s] = w->x_build[last];
        w->inv_mass[s] = w->inv_mass[last]; w->mass[s] = w->mass[last];
        w->flags[s] = w->flags[last]; w->calm[s] = w->calm[last]; w->mat[s] = w->mat[last];
        w->id[s] = w->id[last]; w->obj[s] = w->obj[last];
        w->push[2 * s] = w->push[2 * last]; w->push[2 * s + 1] = w->push[2 * last + 1];
        w->push_id[2 * s] = w->push_id[2 * last]; w->push_id[2 * s + 1] = w->push_id[2 * last + 1];
        w->id_slot[w->id[s]] = s;
        for (int c = 0; c < w->dist_count; c++) {
            if (w->dist[c].a == last) w->dist[c].a = s;
            if (w->dist[c].b == last) w->dist[c].b = s;
        }
        for (int m = 0; m < w->member_count; m++)
            if (w->members[m].slot == last) w->members[m].slot = s;
    }
    w->mem_dirty = 1;
    w->need_rebuild = 1;
}

int sl_remove(sl_world *w, sl_particle p) {
    int s = slot_of(w, p);
    if (s < 0 || w->obj[s] >= 0) return 0;
    wake_near(w, w->x[s], w->h * 2);
    remove_slot(w, s);
    return 1;
}

void sl_clear(sl_world *w) {
    if (!w) return;
    for (int i = 0; i < w->object_count; i++) sl__free(w, w->objects[i].ids);
    w->object_count = w->dist_count = w->cluster_count = w->member_count = 0;
    w->count = w->free_count = w->next_id = 0;
    w->need_rebuild = 1;
}

/* Called by object code to drop all particles of an object at once. */
void sl__remove_object_particles(sl_world *w, int obj) {
    for (int s = w->count - 1; s >= 0; s--)
        if (w->obj[s] == obj) { wake_near(w, w->x[s], w->h * 2); remove_slot(w, s); }
}

int sl_alive(const sl_world *w, sl_particle p) { return slot_of(w, p) >= 0; }

sl_vec3 sl_position(const sl_world *w, sl_particle p) {
    int s = slot_of(w, p);
    return s >= 0 ? w->x[s] : v3(0, 0, 0);
}

sl_vec3 sl_velocity(const sl_world *w, sl_particle p) {
    int s = slot_of(w, p);
    return s >= 0 ? w->v[s] : v3(0, 0, 0);
}

void sl_set_position(sl_world *w, sl_particle p, sl_vec3 pos) {
    int s = slot_of(w, p);
    if (s < 0 || !v3_finite(pos)) return;
    w->x[s] = w->p[s] = w->x_step[s] = pos;
    w->calm[s] = 0;
    wake_near(w, pos, w->h * 2);
    w->need_rebuild = 1;
}

void sl_set_velocity(sl_world *w, sl_particle p, sl_vec3 vel) {
    int s = slot_of(w, p);
    if (s < 0 || !v3_finite(vel)) return;
    w->v[s] = vel;
    w->calm[s] = 0;
}

void sl_pin(sl_world *w, sl_particle p, int pinned) {
    int s = slot_of(w, p);
    if (s < 0) return;
    if (pinned) { w->flags[s] |= F_PINNED; w->inv_mass[s] = 0; w->v[s] = v3(0, 0, 0); }
    else { w->flags[s] &= (unsigned char)~F_PINNED; w->inv_mass[s] = 1.0f / w->mass[s]; }
    w->calm[s] = 0;
}

static float bound_radius(const sl_collider_desc *d) {
    switch (d->shape) {
    case SL_SPHERE: return d->radius;
    case SL_CAPSULE: return d->radius + d->half_extents.y;
    case SL_BOX: return v3_len(d->half_extents);
    default: return 1e30f;
    }
}

sl_collider sl_collider_add(sl_world *w, const sl_collider_desc *desc) {
    if (!w || !desc || w->collider_count >= SL_MAX_COLLIDERS) return -1;
    collider *c = &w->colliders[w->collider_count];
    memset(c, 0, sizeof *c);
    c->desc = *desc;
    if (c->desc.shape == SL_PLANE) {
        sl_vec3 n = c->desc.normal;
        float len = v3_len(n);
        c->desc.normal = len > 0 ? v3_scale(n, 1.0f / len) : v3(0, 1, 0);
    }
    c->rot = c->prev_rot = q_from(desc->rotation);
    c->prev_pos = desc->position;
    if (c->desc.inside) for (int s = 0; s < w->count; s++) w->calm[s] = 0;
    else wake_near(w, desc->position, bound_radius(&c->desc) + w->h);
    return w->collider_count++;
}

void sl_collider_move(sl_world *w, sl_collider id, sl_vec3 position, const float rotation[4]) {
    if (!w || id < 0 || id >= w->collider_count || !v3_finite(position)) return;
    collider *c = &w->colliders[id];
    c->desc.position = position;
    if (rotation) c->rot = q_from(rotation);
}

sl_vec3 sl_collider_force(const sl_world *w, sl_collider c) {
    if (!w || c < 0 || c >= w->collider_count) return v3(0, 0, 0);
    return w->colliders[c].force;
}

/* Sleeping particles near a moving collider wake up before it reaches them. */
static void wake_for_colliders(sl_world *w) {
    for (int c = 0; c < w->collider_count; c++) {
        collider *col = &w->colliders[c];
        sl_vec3 a = col->prev_pos, b = col->desc.position;
        quat q0 = col->prev_rot, q1 = col->rot;
        int moved = a.x != b.x || a.y != b.y || a.z != b.z || q0.x != q1.x || q0.y != q1.y || q0.z != q1.z || q0.w != q1.w;
        if (!moved) continue;
        float reach = bound_radius(&col->desc) + w->h + w->spacing;
        if (col->desc.shape == SL_PLANE || col->desc.inside) {
            for (int s = 0; s < w->count; s++) w->calm[s] = 0;
            continue;
        }
        sl_vec3 lo = v3(fminf(a.x, b.x) - reach, fminf(a.y, b.y) - reach, fminf(a.z, b.z) - reach);
        sl_vec3 hi = v3(fmaxf(a.x, b.x) + reach, fmaxf(a.y, b.y) + reach, fmaxf(a.z, b.z) + reach);
        for (int s = 0; s < w->count; s++) {
            sl_vec3 x = w->x[s];
            if (x.x > lo.x && x.y > lo.y && x.z > lo.z && x.x < hi.x && x.y < hi.y && x.z < hi.z) w->calm[s] = 0;
        }
    }
}

/* Neighbor lists hold while nothing can have moved more than half the skin by the end of the substep. */
static int needs_rebuild(sl_world *w) {
    float limit = 0.5f * w->skin;
    for (int k = 0; k < w->active_count; k++) {
        int s = w->active[k];
        if (v3_len(v3_sub(w->x[s], w->x_build[s])) + v3_len(w->v[s]) * w->hs > limit) return 1;
    }
    return 0;
}

static void refresh(sl_world *w, int move) {
    if (w->need_rebuild || needs_rebuild(w)) {
        if (grid_rebuild(w, move)) { w->need_rebuild = 0; w->rebuilds++; }
    }
}

static void begin_step(sl_world *w, int begin, int end, int chunk, void *ctx) {
    (void)chunk; (void)ctx;
    for (int k = begin; k < end; k++) {
        int s = w->active[k];
        w->x_step[s] = w->x[s];
        w->flags[s] &= (unsigned char)~(F_TOUCH | F_WET);
        w->push_id[2 * s] = w->push_id[2 * s + 1] = 0;
        w->push[2 * s] = w->push[2 * s + 1] = v3(0, 0, 0);
    }
}

/* PBD lets resting grains creep, so settled grains in contact hold still; then track calm for sleeping. */
static void end_step(sl_world *w, int begin, int end, int chunk, void *ctx) {
    (void)chunk; (void)ctx;
    float slow = w->sleep_speed * w->sleep_speed;
    for (int k = begin; k < end; k++) {
        int s = w->active[k];
        if (!v3_finite(w->x[s]) || !v3_finite(w->v[s])) { w->x[s] = w->p[s] = w->x_step[s]; w->v[s] = v3(0, 0, 0); }
        if (w->sleep_speed <= 0) continue;
        int still = v3_len2(v3_scale(v3_sub(w->x[s], w->x_step[s]), 1.0f / w->dt)) < slow;
        /* Grains touching fluid are not held, or the hold would undo the push that keeps the two apart. */
        if (still && (w->flags[s] & (F_TOUCH | F_FLUID | F_WET)) == F_TOUCH && w->obj[s] < 0) {
            w->x[s] = w->p[s] = w->x_step[s];
            w->v[s] = v3(0, 0, 0);
        }
        w->calm[s] = still ? (w->calm[s] < 255 ? w->calm[s] + 1 : 255) : 0;
    }
}

void sl_step(sl_world *w, float dt) {
    if (!w || !(dt > 0)) return;
    w->dt = dt;
    w->hs = dt / (float)w->substeps;
    if (w->pool) pool_wake(w->pool);

    wake_for_colliders(w);
    if (w->mem_dirty && objects_membership(w)) w->mem_dirty = 0;
    if (!w->need_rebuild) update_islands(w);
    refresh(w, 1);

    sl__parallel(w, w->active_count, begin_step, NULL);
    for (int s = 0; s < w->substeps; s++) {
        if (s > 0) refresh(w, 0);
        solve_substep(w, (float)(s + 1) / (float)w->substeps);
    }
    fluid_step(w);
    sl__parallel(w, w->active_count, end_step, NULL);
    objects_plasticity(w);

    for (int c = 0; c < w->collider_count; c++) w->colliders[c].force = v3(0, 0, 0);
    for (int s = 0; s < w->count && w->collider_count; s++)
        for (int k = 0; k < 2; k++)
            if (w->push_id[2 * s + k]) {
                collider *col = &w->colliders[w->push_id[2 * s + k] - 1];
                col->force = v3_madd(col->force, w->push[2 * s + k], 1.0f / dt);
            }
    for (int c = 0; c < w->collider_count; c++) {
        w->colliders[c].prev_pos = w->colliders[c].desc.position;
        w->colliders[c].prev_rot = w->colliders[c].rot;
    }
    if (w->pool) pool_sleep(w->pool);
}

int sl_count(const sl_world *w) { return w ? w->count : 0; }
const sl_vec3 *sl_positions(const sl_world *w) { return w ? w->x : NULL; }
const sl_vec3 *sl_velocities(const sl_world *w) { return w ? w->v : NULL; }
float sl_particle_radius(const sl_world *w) { return w ? w->radius : 0.0f; }
const sl_particle *sl_ids(const sl_world *w) { return w ? w->id : NULL; }

/* Materials are stored as bytes; this view is rebuilt on request into a scratch array. */
const sl_material *sl_materials(const sl_world *w) {
    if (!w) return NULL;
    sl_world *mw = (sl_world *)w;
    for (int s = 0; s < w->count; s++) mw->order[s] = w->mat[s];
    return mw->order;
}

void sl_get_stats(const sl_world *w, sl_stats *out) {
    if (!w || !out) return;
    memset(out, 0, sizeof *out);
    out->particles = w->count;
    out->awake = w->active_count;
    out->pairs = w->pair_count;
    out->contacts = w->contact_count;
    out->islands = w->island_count;
    out->rebuilds = w->rebuilds;
    size_t per_slot = 9 * sizeof(sl_vec3) + 3 * sizeof(float) + 5 + 11 * sizeof(int);
    out->memory_bytes = sizeof *w + (size_t)w->cap * per_slot + (size_t)w->g.start_cap * sizeof(int)
        + (size_t)(w->nbr_cap + w->nbr_r_cap) * sizeof(int) + (size_t)(w->contact_cap + w->contact_tmp_cap) * sizeof(contact)
        + (size_t)w->island_cap * sizeof(int) + (size_t)w->mem_list_cap * sizeof(int)
 + (size_t)w->dist_cap * sizeof(dist_con) + (size_t)w->dist_lambda_cap * sizeof(float)
        + (size_t)w->member_cap * sizeof(member) + (size_t)w->cluster_cap * sizeof(cluster);
}
