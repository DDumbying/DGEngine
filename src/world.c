#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "internal.h"

static void *default_alloc(size_t size, void *user) { (void)user; return malloc(size); }
static void default_free(void *ptr, void *user) { (void)user; free(ptr); }

void *sl__alloc(sl_world *w, size_t size) { return size ? w->alloc.alloc(size, w->alloc.user) : NULL; }
void sl__free(sl_world *w, void *ptr) { if (ptr) w->alloc.free(ptr, w->alloc.user); }

int sl__grow(sl_world *w, void **ptr, int *cap, int need, size_t elem) {
    if (need <= *cap) return 1;
    long long n = *cap > 0 ? *cap : 64;   /* wide, so doubling past 2^30 cannot overflow */
    while (n < need) n *= 2;
    if (n > INT_MAX) n = need;
    void *mem = sl__alloc(w, (size_t)n * elem);
    if (!mem) return 0;
    if (*ptr) { memcpy(mem, *ptr, (size_t)*cap * elem); sl__free(w, *ptr); }
    *ptr = mem;
    *cap = (int)n;
    return 1;
}

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
    void **bytes[] = {(void **)&w->flags, (void **)&w->calm, (void **)&w->mat, (void **)&w->wet, (void **)&w->near_fluid, (void **)&w->mark};
    for (size_t i = 0; i < sizeof bytes / sizeof bytes[0]; i++) out[n++] = (slot_array){bytes[i], 1, 0};
    out[n++] = (slot_array){(void **)&w->push, SL_PUSH_SLOTS * sizeof(sl_vec3), 0};
    out[n++] = (slot_array){(void **)&w->push_id, SL_PUSH_SLOTS, 0};
    if (w->use_aniso) out[n++] = (slot_array){(void **)&w->aniso, 4 * sizeof(sl_vec3), 0};
    void **ints[] = {(void **)&w->id, (void **)&w->obj, (void **)&w->island, (void **)&w->material,
                     (void **)&w->g.bucket, (void **)&w->g.sorted, (void **)&w->active, (void **)&w->id_slot, (void **)&w->free_ids};
    for (size_t i = 0; i < sizeof ints / sizeof ints[0]; i++) out[n++] = (slot_array){ints[i], sizeof(int), 0};
    out[n++] = (slot_array){(void **)&w->order, sizeof(int), 1};
    out[n++] = (slot_array){(void **)&w->nbr_off, sizeof(int), 1};
    out[n++] = (slot_array){(void **)&w->mem_off, sizeof(int), 1};
    return n;
}

int sl__grow_slots(sl_world *w, int need) {
    if (need <= w->cap) return 1;
    if (need > w->max_particles) return 0;
    long long grown = w->cap > 0 ? w->cap : 1024;   /* wide, so doubling past 2^30 cannot overflow */
    while (grown < need) grown *= 2;
    /* At least 16 slots, since some per-slot arrays double as per-chunk scratch (3 entries per chunk). */
    if (grown > w->max_particles) grown = w->max_particles > 16 ? w->max_particles : 16;
    int n = (int)grown;

    slot_array arrays[32];
    int count = slot_arrays(w, arrays);
    for (int i = 0; i < count; i++) {
        size_t old = ((size_t)w->cap + (size_t)arrays[i].extra) * arrays[i].elem;
        size_t size = ((size_t)n + (size_t)arrays[i].extra) * arrays[i].elem;
        void *mem = sl__alloc(w, size);
        if (!mem) return 0;
        if (*arrays[i].ptr) { memcpy(mem, *arrays[i].ptr, old); sl__free(w, *arrays[i].ptr); }
        else memset(mem, 0, size);
        *arrays[i].ptr = mem;
    }

    w->cap = n;
    return 1;
}

unsigned sl_version(void) { return (SLIME_VERSION_MAJOR << 16) | (SLIME_VERSION_MINOR << 8) | SLIME_VERSION_PATCH; }

int sl_deterministic(void) {
    sl_fpmode fpmode = sl_fp_enter();
    /* Inputs are volatile so nothing is folded at compile time. */
    volatile float one = 1.0f, inc = 1.0f / 4096.0f, tiny = 1.17549435e-38f;
    float a = one + inc, c = -(one + 2.0f * inc);
    int ok = a * a + c == 0.0f;   /* rounded twice; fused multiply-add would leave 2^-24 */
    ok &= tiny * 0.5f != 0.0f;    /* subnormals kept, not flushed to zero */

    /* slime's own math functions give these exact bits on every conforming build. */
    volatile float in[] = {0.3f, -0.73f, 1.0f, 0.999f, 2.5f, -3.1f, 7.25f, 0.0625f, 100.0f};
    uint64_t h = 0;
    for (int i = 0; i < 9; i++) {
        float x = in[i], s, co;
        sl_sincos(x, &s, &co);
        float vals[] = {sl_exp2(x), sl_log2(x > 0 ? x : -x), s, co, sl_cbrt(x > 0 ? x : -x), sl_pow(0.3f, x > 0 ? x : -x)};
        for (int k = 0; k < 6; k++) h = (h ^ sl_bits(vals[k])) * 0x100000001b3ull;
    }
    ok &= h == 0x1f2362a7d3611078ull;
    sl_fp_leave(fpmode);
    return ok;
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
    w->id_bits = 1;
    while (w->id_bits < 31 && (1u << w->id_bits) < (unsigned)desc->max_particles) w->id_bits++;
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
    w->use_aniso = desc->anisotropy != 0;
    w->max_diffuse = desc->max_diffuse > 0 ? desc->max_diffuse : 0;
    sl__wall_table_init(w);
    if (w->max_diffuse) {
        w->dpos = sl__alloc(w, (size_t)w->max_diffuse * sizeof(sl_vec3));
        w->dvel = sl__alloc(w, (size_t)w->max_diffuse * sizeof(sl_vec3));
        w->dlife = sl__alloc(w, (size_t)w->max_diffuse * sizeof(float));
        w->dkind = sl__alloc(w, (size_t)w->max_diffuse);
        if (!w->dpos || !w->dvel || !w->dlife || !w->dkind) { sl_world_destroy(w); return NULL; }
    }

    if (!sl__grow_slots(w, desc->max_particles < 1024 ? desc->max_particles : 1024)) { sl_world_destroy(w); return NULL; }
    if (!w->tasks.parallel_for && desc->workers > 1) {
        w->pool = sl__pool_create(w, desc->workers - 1);
        if (!w->pool) { sl_world_destroy(w); return NULL; }
    }
    return w;
}

void sl_world_destroy(sl_world *w) {
    if (!w) return;
    sl__pool_destroy(w, w->pool);
    slot_array arrays[32];
    int count = slot_arrays(w, arrays);
    for (int i = 0; i < count; i++) sl__free(w, *arrays[i].ptr);
    sl__free(w, w->g.start);
    sl__free(w, w->nbr);
    sl__free(w, w->knbr);
    sl__free(w, w->kdist);
    sl__free(w, w->contacts);
    sl__free(w, w->contact_tmp);
    sl__free(w, w->island_calm);
    for (int i = 0; i < w->object_count; i++) sl__free(w, w->objects[i].ids);
    sl__free(w, w->objects);
    sl__free(w, w->dist);
    sl__free(w, w->dist_tmp);
    sl__free(w, w->colors);
    sl__free(w, w->chunk_buf);
    sl__free(w, w->dist_lambda);
    sl__free(w, w->clusters);
    sl__free(w, w->members);
    sl__free(w, w->mem_list);
    sl__free(w, w->dpos);
    sl__free(w, w->dvel);
    sl__free(w, w->dlife);
    sl__free(w, w->dkind);
    sl_allocator a = w->alloc;
    a.free(w, a.user);
}

/* Finite and not negative; negative coefficients would feed energy into the solver. */
static int non_negative(float v) { return isfinite(v) && v >= 0; }

/* A checked copy of a material description with defaults filled in; 0 if it is not valid. */
static int material_desc(const sl_material_desc *in, sl_material_desc *out) {
    sl_material_desc m = *in;
    if ((m.kind != SL_FLUID && m.kind != SL_GRANULAR && m.kind != SL_SOLID) || !non_negative(m.density)
        || !non_negative(m.viscosity) || !non_negative(m.cohesion) || !non_negative(m.friction)
        || !non_negative(m.vorticity) || !non_negative(m.damping) || !non_negative(m.wet_cohesion)) return 0;
    if (m.density <= 0) m.density = 1000.0f;
    *out = m;
    return 1;
}

sl_material sl_material_add(sl_world *w, const sl_material_desc *desc) {
    if (!w || !desc || w->material_count >= SL_MAX_MATERIALS || !material_desc(desc, &w->materials[w->material_count])) return -1;
    return w->material_count++;
}

int sl_material_get(const sl_world *w, sl_material m, sl_material_desc *out) {
    if (!w || !out || m < 0 || m >= w->material_count) return 0;
    *out = w->materials[m];
    return 1;
}

/* Every particle of the material follows the change: its mass from the density, and loose ones the kind;
   particles of ropes, cloth and soft bodies stay solid. They wake, and contacts are rebuilt, since friction
   and stickiness are cached in them. */
int sl_material_set(sl_world *w, sl_material m, const sl_material_desc *desc) {
    sl_material_desc d;
    if (!w || !desc || m < 0 || m >= w->material_count || !material_desc(desc, &d)) return 0;
    w->materials[m] = d;
    float mass = d.density * w->spacing * w->spacing * w->spacing;
    for (int s = 0; s < w->count; s++) {
        if (w->mat[s] != m) continue;
        w->mass[s] = mass;
        w->inv_mass[s] = (w->flags[s] & F_KINEMATIC) ? 0.0f : 1.0f / mass;
        if (w->obj[s] < 0) {
            if (d.kind == SL_FLUID) w->flags[s] |= F_FLUID;
            else w->flags[s] &= (unsigned char)~F_FLUID;
        }
        w->calm[s] = 0;
    }
    w->need_rebuild = 1;
    return 1;
}

void sl_set_gravity(sl_world *w, sl_vec3 gravity) {
    if (!w || !v3_finite(gravity)) return;
    w->gravity = gravity;
    w->wake_all = 1;       /* sleeping particles must feel it */
    w->need_rebuild = 1;   /* contacts cache which way is up */
}

sl_vec3 sl_gravity(const sl_world *w) { return w ? w->gravity : v3(0, 0, 0); }

int sl__slot_of(const sl_world *w, sl_particle p) {
    if (!w || p < 0 || id_index(w, p) >= w->next_id) return -1;
    int s = w->id_slot[id_index(w, p)];
    return s >= 0 && s < w->count && w->id[s] == p ? s : -1;
}

/* A freed id comes back with its generation bumped, so the old one never matches again. */
static sl_particle reissue(const sl_world *w, sl_particle old) {
    unsigned gen_mask = w->id_bits < 31 ? (1u << (31 - w->id_bits)) - 1u : 0u;
    unsigned gen = (((unsigned)old >> w->id_bits) + 1u) & gen_mask;
    return (sl_particle)((gen << w->id_bits) | (unsigned)id_index(w, old));
}

sl_particle sl_spawn(sl_world *w, sl_material m, sl_vec3 pos, sl_vec3 vel) {
    if (!w || m < 0 || m >= w->material_count || !v3_finite(pos) || !v3_finite(vel)) return -1;
    if (w->count >= w->max_particles || !sl__grow_slots(w, w->count + 1)) return -1;
    int s = w->count++;
    int id = w->free_count > 0 ? reissue(w, w->free_ids[--w->free_count]) : w->next_id++;
    float d = w->spacing;
    w->x[s] = w->p[s] = w->x_step[s] = w->x_build[s] = pos;
    w->v[s] = vel;
    w->mat[s] = (unsigned char)m;
    w->material[s] = m;
    w->mass[s] = w->materials[m].density * d * d * d;
    w->inv_mass[s] = 1.0f / w->mass[s];
    w->flags[s] = w->materials[m].kind == SL_FLUID ? F_FLUID : 0;
    w->calm[s] = 0;
    w->obj[s] = -1;
    for (int k = 0; k < SL_PUSH_SLOTS; k++) w->push_id[SL_PUSH_SLOTS * s + k] = 0;
    w->wet[s] = 0;
    if (w->use_aniso) {
        float r = w->radius;
        w->aniso[4 * s] = pos;
        w->aniso[4 * s + 1] = v3(r, 0, 0);
        w->aniso[4 * s + 2] = v3(0, r, 0);
        w->aniso[4 * s + 3] = v3(0, 0, r);
    }
    w->id[s] = id;
    w->id_slot[id_index(w, id)] = s;
    w->need_rebuild = 1;
    return id;
}

static float jitter(int i, int j, int k) {
    unsigned h = (unsigned)i * 73856093u ^ (unsigned)j * 19349663u ^ (unsigned)k * 83492791u;
    h = (h ^ (h >> 13)) * 0x5bd1e995u;
    h ^= h >> 15;
    return (float)(h & 0xffff) / 65535.0f - 0.5f;
}

static int spawn_box(sl_world *w, sl_material m, sl_vec3 min, sl_vec3 max) {
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

int sl_spawn_box(sl_world *w, sl_material m, sl_vec3 min, sl_vec3 max) {
    sl_fpmode fpmode = sl_fp_enter();
    int added = spawn_box(w, m, min, max);
    sl_fp_leave(fpmode);
    return added;
}

/* Wakes are queued and done in one pass at the next step, so edits between steps never scan every particle. */
static void queue_wake(sl_world *w, sl_vec3 lo, sl_vec3 hi) {
    if (w->wake_count == SL_MAX_WAKES) { w->wake_all = 1; return; }
    w->wakes[w->wake_count].lo = lo;
    w->wakes[w->wake_count].hi = hi;
    w->wake_count++;
}

/* Whatever a collider can reach while it sweeps from a to b; containers and planes reach everything. */
static void queue_collider_wake(sl_world *w, const sl_collider_desc *d, sl_vec3 a, sl_vec3 b, float margin) {
    if (d->inside || d->shape == SL_PLANE) { w->wake_all = 1; return; }
    float reach = sl__bound_radius(d) + margin;
    queue_wake(w, v3(sl_min(a.x, b.x) - reach, sl_min(a.y, b.y) - reach, sl_min(a.z, b.z) - reach),
               v3(sl_max(a.x, b.x) + reach, sl_max(a.y, b.y) + reach, sl_max(a.z, b.z) + reach));
}

static void apply_wakes(sl_world *w) {
    if (w->wake_all) {
        for (int s = 0; s < w->count; s++) w->calm[s] = 0;
    } else if (w->wake_count) {
        for (int s = 0; s < w->count; s++) {
            sl_vec3 x = w->x[s];
            for (int k = 0; k < w->wake_count; k++) {
                sl_vec3 lo = w->wakes[k].lo, hi = w->wakes[k].hi;
                if (x.x > lo.x && x.y > lo.y && x.z > lo.z && x.x < hi.x && x.y < hi.y && x.z < hi.z) { w->calm[s] = 0; break; }
            }
        }
    }
    w->wake_count = w->wake_all = 0;
}

/* Wakes whatever touched a marked slot: its neighbors when the lists are current, else a box around them. */
static void wake_marked(sl_world *w) {
    if (w->nbr_valid) {
        for (int s = 0; s < w->count && s < w->built; s++)
            if (w->mark[s]) for (int n = w->nbr_off[s]; n < w->nbr_off[s + 1]; n++) w->calm[w->nbr[n]] = 0;
        return;
    }
    float big = 1e30f, reach = 2 * w->h;
    sl_vec3 lo = v3(big, big, big), hi = v3(-big, -big, -big);
    for (int s = 0; s < w->count; s++) {
        if (!w->mark[s]) continue;
        sl_vec3 x = w->x[s];
        lo = v3(sl_min(lo.x, x.x), sl_min(lo.y, x.y), sl_min(lo.z, x.z));
        hi = v3(sl_max(hi.x, x.x), sl_max(hi.y, x.y), sl_max(hi.z, x.z));
    }
    for (int s = 0; s < w->count; s++) {
        sl_vec3 x = w->x[s];
        if (x.x > lo.x - reach && x.y > lo.y - reach && x.z > lo.z - reach && x.x < hi.x + reach && x.y < hi.y + reach && x.z < hi.z + reach)
            w->calm[s] = 0;
    }
}

static void move_slot(sl_world *w, int dst, int src) {
    w->x[dst] = w->x[src]; w->p[dst] = w->p[src]; w->v[dst] = w->v[src];
    w->x_step[dst] = w->x_step[src]; w->x_build[dst] = w->x_build[src];
    w->inv_mass[dst] = w->inv_mass[src]; w->mass[dst] = w->mass[src];
    w->flags[dst] = w->flags[src]; w->calm[dst] = w->calm[src]; w->mat[dst] = w->mat[src]; w->wet[dst] = w->wet[src];
    w->material[dst] = w->material[src];
    if (w->use_aniso) for (int k = 0; k < 4; k++) w->aniso[4 * dst + k] = w->aniso[4 * src + k];
    w->id[dst] = w->id[src]; w->obj[dst] = w->obj[src];
    for (int k = 0; k < SL_PUSH_SLOTS; k++) {
        w->push[SL_PUSH_SLOTS * dst + k] = w->push[SL_PUSH_SLOTS * src + k];
        w->push_id[SL_PUSH_SLOTS * dst + k] = w->push_id[SL_PUSH_SLOTS * src + k];
    }
}

/* Removes every slot with mark set in one pass, keeping the order of the rest; returns how many went. */
static int remove_marked(sl_world *w) {
    wake_marked(w);
    int *old_to_new = w->order, n = 0;
    for (int s = 0; s < w->count; s++) {
        if (w->mark[s]) { w->free_ids[w->free_count++] = w->id[s]; old_to_new[s] = -1; continue; }
        if (n != s) move_slot(w, n, s);
        w->id_slot[id_index(w, w->id[n])] = n;
        old_to_new[s] = n++;
    }
    int removed = w->count - n;
    if (!removed) return 0;
    w->count = n;
    for (int g = 0; g < w->grab_count; g++)
        if (sl__slot_of(w, w->grabs[g].id) < 0) w->grabs[g--] = w->grabs[--w->grab_count];
    sl__objects_remap(w, old_to_new);
    w->need_rebuild = 1;
    w->nbr_valid = 0;
    return removed;
}

/* Marks loose particles in the sphere (if any) and every particle of objects flagged for removal. */
int sl__remove_doomed(sl_world *w, const sl_vec3 *center, float radius) {
    int doomed = 0;
    for (int s = 0; s < w->count; s++) {
        int o = w->obj[s], in = center && v3_len2(v3_sub(w->x[s], *center)) <= radius * radius;
        if (in && o >= 0 && w->objects[o].alive == 1) w->objects[o].alive = 2;
        w->mark[s] = (unsigned char)(in && o < 0);
    }
    for (int o = 0; o < w->object_count; o++) doomed |= w->objects[o].alive == 2;
    for (int s = 0; doomed && s < w->count; s++) if (w->obj[s] >= 0 && w->objects[w->obj[s]].alive == 2) w->mark[s] = 1;
    for (int o = 0; doomed && o < w->object_count; o++) if (w->objects[o].alive == 2) sl__objects_drop(w, o);
    if (doomed) sl__color_dist(w);
    return remove_marked(w);
}

int sl_remove_many(sl_world *w, const sl_particle *ids, int count) {
    if (!w || !ids || count <= 0) return 0;
    memset(w->mark, 0, (size_t)w->count);
    for (int k = 0; k < count; k++) {
        int s = sl__slot_of(w, ids[k]);
        if (s >= 0 && w->obj[s] < 0) w->mark[s] = 1;
    }
    return remove_marked(w);
}

int sl_remove(sl_world *w, sl_particle p) {
    int s = sl__slot_of(w, p);
    if (s < 0 || w->obj[s] >= 0) return 0;
    memset(w->mark, 0, (size_t)w->count);
    w->mark[s] = 1;
    return remove_marked(w);
}

void sl_clear(sl_world *w) {
    if (!w) return;
    /* Object slots stay, emptied, so their generations keep handles from before the clear dead. */
    for (int i = 0; i < w->object_count; i++) {
        sl__free(w, w->objects[i].ids);
        int gen = w->objects[i].gen;
        memset(&w->objects[i], 0, sizeof(object));
        w->objects[i].gen = gen;
    }
    w->dist_count = w->cluster_count = w->member_count = 0;
    memset(w->dist_color_off, 0, sizeof w->dist_color_off);
    memset(w->color_off, 0, sizeof w->color_off);
    /* Ids go back on the free list rather than starting over, so handles from before the clear stay dead. */
    for (int s = 0; s < w->count; s++) w->free_ids[w->free_count++] = w->id[s];
    w->count = 0;
    w->active_count = w->contact_count = w->pair_count = w->island_count = 0;
    w->grab_count = 0;
    w->diffuse_count = 0;
    w->wake_count = w->wake_all = 0;
    w->need_rebuild = w->mem_dirty = 1;
    w->nbr_valid = 0;
}

int sl_alive(const sl_world *w, sl_particle p) { return sl__slot_of(w, p) >= 0; }

sl_vec3 sl_position(const sl_world *w, sl_particle p) {
    int s = sl__slot_of(w, p);
    return s >= 0 ? w->x[s] : v3(0, 0, 0);
}

sl_vec3 sl_velocity(const sl_world *w, sl_particle p) {
    int s = sl__slot_of(w, p);
    return s >= 0 ? w->v[s] : v3(0, 0, 0);
}

void sl_set_position(sl_world *w, sl_particle p, sl_vec3 pos) {
    int s = sl__slot_of(w, p);
    if (s < 0 || !v3_finite(pos)) return;
    w->x[s] = w->p[s] = w->x_step[s] = pos;
    w->calm[s] = 0;
    w->need_rebuild = 1;   /* the rebuild regroups islands, so whatever it lands next to wakes with it */
}

void sl_set_velocity(sl_world *w, sl_particle p, sl_vec3 vel) {
    int s = sl__slot_of(w, p);
    if (s < 0 || !v3_finite(vel)) return;
    w->v[s] = vel;
    w->calm[s] = 0;
}

void sl_pin(sl_world *w, sl_particle p, int pinned) {
    int s = sl__slot_of(w, p);
    if (s < 0) return;
    if (pinned) { w->flags[s] |= F_PINNED; w->inv_mass[s] = 0; w->v[s] = v3(0, 0, 0); }
    else {
        w->flags[s] &= (unsigned char)~F_PINNED;
        if (!(w->flags[s] & F_GRAB)) w->inv_mass[s] = 1.0f / w->mass[s];
    }
    w->calm[s] = 0;
}

float sl__bound_radius(const sl_collider_desc *d) {
    switch (d->shape) {
    case SL_SPHERE: return d->radius;
    case SL_CAPSULE: return d->radius + d->half_extents.y;
    case SL_BOX: return v3_len(d->half_extents);
    default: return 1e30f;
    }
}

static int finite_rotation(const float r[4]) {
    return isfinite(r[0]) && isfinite(r[1]) && isfinite(r[2]) && isfinite(r[3]);
}

static int valid_desc(const sl_collider_desc *d) {
    sl_vec3 e = d->half_extents;
    return (d->shape == SL_PLANE || d->shape == SL_BOX || d->shape == SL_SPHERE || d->shape == SL_CAPSULE)
        && v3_finite(d->position) && v3_finite(d->normal) && finite_rotation(d->rotation)
        && non_negative(e.x) && non_negative(e.y) && non_negative(e.z) && non_negative(d->radius) && non_negative(d->friction);
}

static void unit_normal(sl_collider_desc *d) {
    if (d->shape != SL_PLANE) return;
    float len = v3_len(d->normal);
    d->normal = len > 0 ? v3_scale(d->normal, 1.0f / len) : v3(0, 1, 0);
}

sl_collider sl_collider_add(sl_world *w, const sl_collider_desc *desc) {
    if (!w || !desc || !valid_desc(desc)) return -1;
    int slot = 0;
    while (slot < w->collider_count && !w->colliders[slot].removed) slot++;
    if (slot >= SL_MAX_COLLIDERS) return -1;
    collider *c = &w->colliders[slot];
    memset(c, 0, sizeof *c);
    c->enabled = 1;
    c->desc = *desc;
    unit_normal(&c->desc);
    c->rot = c->prev_rot = q_from(desc->rotation);
    c->prev_pos = desc->position;
    queue_collider_wake(w, &c->desc, desc->position, desc->position, w->h);
    if (slot == w->collider_count) w->collider_count++;
    return slot;
}

static int valid_collider(const sl_world *w, sl_collider c) {
    return w && c >= 0 && c < w->collider_count && !w->colliders[c].removed;
}

/* Shape, size, normal, inside and friction change at once; position and rotation become the target, as
   with sl_collider_move, so the collider sweeps there. What the old and new shapes reach wakes. */
int sl_collider_set(sl_world *w, sl_collider c, const sl_collider_desc *desc) {
    if (!valid_collider(w, c) || !desc || !valid_desc(desc)) return 0;
    collider *col = &w->colliders[c];
    queue_collider_wake(w, &col->desc, col->prev_pos, col->desc.position, w->h);
    col->desc = *desc;
    unit_normal(&col->desc);
    col->rot = q_from(desc->rotation);
    queue_collider_wake(w, &col->desc, col->prev_pos, col->desc.position, w->h);
    return 1;
}

static void wake_collider(sl_world *w, sl_collider c) {
    const sl_collider_desc *d = &w->colliders[c].desc;
    queue_collider_wake(w, d, d->position, d->position, w->h);
}

void sl_collider_set_enabled(sl_world *w, sl_collider c, int enabled) {
    if (!valid_collider(w, c) || w->colliders[c].enabled == !!enabled) return;
    w->colliders[c].enabled = !!enabled;
    w->colliders[c].force = v3(0, 0, 0);
    wake_collider(w, c);
}

int sl_collider_enabled(const sl_world *w, sl_collider c) { return valid_collider(w, c) && w->colliders[c].enabled; }

void sl_collider_remove(sl_world *w, sl_collider c) {
    if (!valid_collider(w, c)) return;
    wake_collider(w, c);
    w->colliders[c].enabled = 0;
    w->colliders[c].removed = 1;
    unsigned char id = (unsigned char)(c + 1);
    for (int k = 0; k < SL_PUSH_SLOTS * w->count; k++)
        if (w->push_id[k] == id) { w->push_id[k] = 0; w->push[k] = v3(0, 0, 0); }
    while (w->collider_count > 0 && w->colliders[w->collider_count - 1].removed) w->collider_count--;
}

/* Nearest particle hit by a ray, -1 if none within max_dist. */
sl_particle sl_raycast(const sl_world *w, sl_vec3 origin, sl_vec3 dir, float max_dist, float *hit_dist) {
    if (!w) return -1;
    float len = v3_len(dir);
    if (len < 1e-12f) return -1;
    dir = v3_scale(dir, 1.0f / len);
    float best = max_dist, r2 = w->radius * w->radius;
    int hit = -1;
    for (int s = 0; s < w->count; s++) {
        sl_vec3 oc = v3_sub(w->x[s], origin);
        float along = v3_dot(oc, dir), off2 = v3_len2(oc) - along * along;
        if (off2 > r2) continue;
        float t = along - sqrtf(r2 - off2);
        if (t < 0) t = along + sqrtf(r2 - off2);
        if (t >= 0 && t < best) { best = t; hit = s; }
    }
    if (hit < 0) return -1;
    if (hit_dist) *hit_dist = best;
    return w->id[hit];
}

static int find_grab(const sl_world *w, sl_particle p) {
    for (int g = 0; g < w->grab_count; g++) if (w->grabs[g].id == p) return g;
    return -1;
}

int sl_grab_begin(sl_world *w, sl_particle p) {
    int s = sl__slot_of(w, p);
    if (s < 0 || find_grab(w, p) >= 0 || w->grab_count >= SL_MAX_GRABS) return 0;
    w->grabs[w->grab_count++] = (grab){p, w->x[s], w->x[s]};
    w->flags[s] |= F_GRAB;
    w->inv_mass[s] = 0;
    w->calm[s] = 0;
    return 1;
}

void sl_grab_move(sl_world *w, sl_particle p, sl_vec3 target) {
    int g = w ? find_grab(w, p) : -1;
    if (g >= 0 && v3_finite(target)) w->grabs[g].to = target;
}

void sl_grab_end(sl_world *w, sl_particle p) {
    int g = w ? find_grab(w, p) : -1;
    if (g < 0) return;
    w->grabs[g] = w->grabs[--w->grab_count];
    int s = sl__slot_of(w, p);
    if (s < 0) return;
    w->flags[s] &= (unsigned char)~F_GRAB;
    if (!(w->flags[s] & F_PINNED)) w->inv_mass[s] = 1.0f / w->mass[s];
    w->calm[s] = 0;
}

/* Removes loose particles inside the sphere and whole objects that reach into it. */
int sl_remove_sphere(sl_world *w, sl_vec3 center, float radius) {
    if (!w || !(radius > 0) || !v3_finite(center)) return 0;
    return sl__remove_doomed(w, &center, radius);
}

void sl_collider_move(sl_world *w, sl_collider id, sl_vec3 position, const float rotation[4]) {
    if (!valid_collider(w, id) || !v3_finite(position) || (rotation && !finite_rotation(rotation))) return;
    collider *c = &w->colliders[id];
    c->desc.position = position;
    if (rotation) c->rot = q_from(rotation);
}

sl_vec3 sl_collider_force(const sl_world *w, sl_collider c) {
    if (!valid_collider(w, c)) return v3(0, 0, 0);
    return w->colliders[c].force;
}

/* Sleeping particles near a moving collider wake up before it reaches them. */
static void wake_for_colliders(sl_world *w) {
    for (int c = 0; c < w->collider_count; c++) {
        collider *col = &w->colliders[c];
        sl_vec3 a = col->prev_pos, b = col->desc.position;
        quat q0 = col->prev_rot, q1 = col->rot;
        int moved = a.x != b.x || a.y != b.y || a.z != b.z || q0.x != q1.x || q0.y != q1.y || q0.z != q1.z || q0.w != q1.w;
        if (moved && col->enabled) queue_collider_wake(w, &col->desc, a, b, w->h + w->spacing);
    }
    apply_wakes(w);
}

/* Neighbor lists hold while nothing can have moved more than half the skin by the end of the substep. */
static void check_skin(sl_world *w, int begin, int end, int chunk, void *ctx) {
    float limit = 0.5f * w->skin;
    int far = 0;
    (void)ctx;
    for (int k = begin; k < end && !far; k++) {
        int s = w->active[k];
        far = v3_len(v3_sub(w->x[s], w->x_build[s])) + v3_len(w->v[s]) * w->hs > limit;
    }
    w->order[chunk] = far;
}

static int needs_rebuild(sl_world *w) {
    PROF(P_SKIN, sl__parallel(w, w->active_count, check_skin, NULL));
    for (int c = 0; c < sl__chunks(w->active_count); c++) if (w->order[c]) return 1;
    return 0;
}

static void refresh(sl_world *w, int step_start) {
    if (w->need_rebuild || needs_rebuild(w)) {
        if (sl__grid_rebuild(w, step_start)) { w->need_rebuild = 0; w->rebuilds++; }
    }
}

static void begin_step(sl_world *w, int begin, int end, int chunk, void *ctx) {
    (void)chunk; (void)ctx;
    for (int k = begin; k < end; k++) {
        int s = w->active[k];
        w->x_step[s] = w->x[s];
        w->flags[s] &= (unsigned char)~(F_TOUCH | F_WET);
        for (int k = 0; k < SL_PUSH_SLOTS; k++) { w->push_id[SL_PUSH_SLOTS * s + k] = 0; w->push[SL_PUSH_SLOTS * s + k] = v3(0, 0, 0); }
    }
}

/* PBD lets resting grains creep, so settled grains in contact hold still; then track calm for sleeping. */
static void end_step(sl_world *w, int begin, int end, int chunk, void *ctx) {
    (void)chunk; (void)ctx;
    float slow = w->sleep_speed * w->sleep_speed;
    for (int k = begin; k < end; k++) {
        int s = w->active[k];
        if (!v3_finite(w->x[s]) || !v3_finite(w->v[s])) { w->x[s] = w->p[s] = w->x_step[s]; w->v[s] = v3(0, 0, 0); }
        if (w->flags[s] & F_WET) w->wet[s] = 255;
        else if (w->wet[s]) w->wet[s]--;
        if (w->sleep_speed <= 0) continue;
        int still = v3_len2(v3_scale(v3_sub(w->x[s], w->x_step[s]), 1.0f / w->dt)) < slow;
        if (still && (w->flags[s] & (F_TOUCH | F_FLUID)) == F_TOUCH && w->obj[s] < 0) {
            w->x[s] = w->p[s] = w->x_step[s];
            w->v[s] = v3(0, 0, 0);
        }
        w->calm[s] = still ? (w->calm[s] < 255 ? w->calm[s] + 1 : 255) : 0;
    }
}

void sl_step(sl_world *w, float dt) {
    if (!w || !(dt > 0)) return;
    sl_fpmode fpmode = sl_fp_enter();
#ifdef SLIME_PROFILE
    double start = sl__now();
#endif
    w->dt = dt;
    w->hs = dt / (float)w->substeps;

    wake_for_colliders(w);
    for (int g = 0; g < w->grab_count; g++) {
        int s = sl__slot_of(w, w->grabs[g].id);
        if (s >= 0) w->calm[s] = 0;
        else w->grabs[g--] = w->grabs[--w->grab_count];
    }
    if (w->mem_dirty && sl__objects_membership(w)) w->mem_dirty = 0;
    if (!w->need_rebuild) sl__settle_islands(w);
    refresh(w, 1);

    /* A world that is fully asleep skips the solver; spray and collider loads still update below. */
    if (w->active_count) {
        PROF(P_STABILIZE, sl__stabilize(w); sl__stabilize(w));
        sl__parallel(w, w->active_count, begin_step, NULL);
        for (int s = 0; s < w->substeps; s++) {
            if (s > 0) refresh(w, 0);
            sl__solve_substep(w, (float)(s + 1) / (float)w->substeps);
        }
        PROF(P_FLUID_STEP, sl__fluid_step(w));
        sl__parallel(w, w->active_count, end_step, NULL);
    }
    for (int g = 0; g < w->grab_count; g++) w->grabs[g].from = w->grabs[g].to;
    PROF(P_EXTRAS, if (w->max_diffuse) sl__diffuse_step(w); if (w->use_aniso) sl__anisotropy_step(w));
    w->step_count++;
    sl__objects_plasticity(w);

    for (int c = 0; c < w->collider_count; c++) w->colliders[c].force = v3(0, 0, 0);
    for (int s = 0; s < w->count && w->collider_count; s++)
        for (int k = 0; k < SL_PUSH_SLOTS; k++)
            if (w->push_id[SL_PUSH_SLOTS * s + k]) {
                collider *col = &w->colliders[w->push_id[SL_PUSH_SLOTS * s + k] - 1];
                col->force = v3_madd(col->force, w->push[SL_PUSH_SLOTS * s + k], 1.0f / dt);
            }
    for (int c = 0; c < w->collider_count; c++) {
        w->colliders[c].prev_pos = w->colliders[c].desc.position;
        w->colliders[c].prev_rot = w->colliders[c].rot;
    }
#ifdef SLIME_PROFILE
    sl__prof[P_STEP] += sl__now() - start;
#endif
    sl_fp_leave(fpmode);
}

int sl_count(const sl_world *w) { return w ? w->count : 0; }
const sl_vec3 *sl_positions(const sl_world *w) { return w ? w->x : NULL; }
const sl_vec3 *sl_velocities(const sl_world *w) { return w ? w->v : NULL; }
float sl_particle_radius(const sl_world *w) { return w ? w->radius : 0.0f; }
const sl_particle *sl_ids(const sl_world *w) { return w ? w->id : NULL; }
const unsigned char *sl_wetness(const sl_world *w) { return w ? w->wet : NULL; }
const sl_vec3 *sl_anisotropy(const sl_world *w) { return w && w->use_aniso ? w->aniso : NULL; }

int sl_diffuse(const sl_world *w, const sl_vec3 **positions, const sl_vec3 **velocities, const unsigned char **kinds,
               const float **life) {
    if (positions) *positions = w ? w->dpos : NULL;
    if (velocities) *velocities = w ? w->dvel : NULL;
    if (kinds) *kinds = w ? w->dkind : NULL;
    if (life) *life = w ? w->dlife : NULL;
    return w ? w->diffuse_count : 0;
}

const sl_material *sl_materials(const sl_world *w) { return w ? w->material : NULL; }

void sl_get_stats(const sl_world *w, sl_stats *out) {
    if (!w || !out) return;
    memset(out, 0, sizeof *out);
    out->particles = w->count;
    out->awake = w->active_count;
    out->pairs = w->pair_count;
    out->contacts = w->contact_count;
    out->islands = w->island_count;
    out->rebuilds = w->rebuilds;
    slot_array arrays[32];
    int count = slot_arrays((sl_world *)w, arrays);   /* only reads the table, nothing is written */
    size_t bytes = sizeof *w;
    for (int i = 0; i < count; i++) bytes += ((size_t)w->cap + (size_t)arrays[i].extra) * arrays[i].elem;
    bytes += (size_t)w->g.start_cap * sizeof(int) + (size_t)w->nbr_cap * sizeof(int) + (size_t)w->knbr_cap * sizeof(int) + (size_t)w->kdist_cap * sizeof(float)
        + (size_t)(w->contact_cap + w->contact_tmp_cap) * sizeof(contact) + (size_t)w->island_cap * sizeof(int)
        + (size_t)w->mem_list_cap * sizeof(int) + (size_t)(w->dist_cap + w->dist_tmp_cap) * sizeof(dist_con)
        + (size_t)w->dist_lambda_cap * sizeof(float) + (size_t)w->member_cap * sizeof(member)
        + (size_t)w->cluster_cap * sizeof(cluster) + (size_t)w->color_cap + (size_t)w->chunk_cap * sizeof(int)
        + (size_t)w->max_diffuse * (2 * sizeof(sl_vec3) + sizeof(float) + 1) + (size_t)w->object_cap * sizeof(object);
    for (int o = 0; o < w->object_count; o++) if (w->objects[o].ids) bytes += (size_t)w->objects[o].count * sizeof(sl_particle);
    out->memory_bytes = bytes;
}
