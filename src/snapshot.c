#include <string.h>
#include "internal.h"

/* A snapshot is a 56 byte header and a body, all little-endian and written field by field.

   Header: "SLIM", format, slime version, the settings a loading world must share (radius bits, max_particles,
   substeps, iterations, fluid iterations, sleep speed bits, anisotropy), body size (64 bit), body hash (64 bit).

   Body, ordered so every reference can be checked when it is read: gravity, step counter, pending rebuild;
   materials; colliders; the id allocator and particle ids; objects; the other particle fields, one array per
   field so general compressors find long runs; distance constraints in colored order; clusters and members;
   grabs; queued wakes. Caches are not stored: they are rebuilt from the positions of the last neighbor build,
   which gives the same neighbor lists, contacts and coloring the saving world had. */

#define SNAP_FORMAT 1u
#define SNAP_HEADER 56
#define PERSISTENT_FLAGS (F_FLUID | F_PINNED | F_GRAB)

/* ---------- writing and hashing ---------- */

typedef struct {
    unsigned char *buf;   /* NULL when only measuring or hashing */
    size_t len, cap;
    uint64_t h, acc;
    int fill, overflow;
} writer;

static void mix(uint64_t *h, uint64_t word) {
    *h ^= word;
    *h *= 0x9e3779b97f4a7c15ull;
    *h ^= *h >> 29;
}

static void put_byte(writer *wr, unsigned char b) {
    wr->acc |= (uint64_t)b << (8 * wr->fill);
    if (++wr->fill == 8) { mix(&wr->h, wr->acc); wr->acc = 0; wr->fill = 0; }
    if (wr->buf) {
        if (wr->len < wr->cap) wr->buf[wr->len] = b;
        else wr->overflow = 1;
    }
    wr->len++;
}

static void put_u32(writer *wr, uint32_t v) { for (int k = 0; k < 4; k++) put_byte(wr, (unsigned char)(v >> (8 * k))); }
static void put_i32(writer *wr, int v) { put_u32(wr, (uint32_t)v); }

static void put_f32(writer *wr, float v) {
    uint32_t bits;
    memcpy(&bits, &v, 4);
    put_u32(wr, bits);
}

static void put_vec3(writer *wr, sl_vec3 v) { put_f32(wr, v.x); put_f32(wr, v.y); put_f32(wr, v.z); }
static void put_quat(writer *wr, quat q) { put_f32(wr, q.x); put_f32(wr, q.y); put_f32(wr, q.z); put_f32(wr, q.w); }

static uint64_t finish_hash(const writer *wr) {
    uint64_t h = wr->h;
    mix(&h, wr->acc ^ ((uint64_t)wr->len << 56));
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ull;
    h ^= h >> 33;
    return h;
}

static void write_body(const sl_world *w, writer *wr) {
    int n = w->count;
    put_vec3(wr, w->gravity);
    put_u32(wr, w->step_count);
    put_u32(wr, (uint32_t)(w->need_rebuild != 0));

    put_i32(wr, w->material_count);
    for (int m = 0; m < w->material_count; m++) {
        const sl_material_desc *d = &w->materials[m];
        put_u32(wr, (uint32_t)d->kind);
        put_f32(wr, d->density); put_f32(wr, d->viscosity); put_f32(wr, d->cohesion); put_f32(wr, d->friction);
        put_f32(wr, d->vorticity); put_f32(wr, d->damping); put_f32(wr, d->wet_cohesion);
    }

    put_i32(wr, w->collider_count);
    for (int c = 0; c < w->collider_count; c++) {
        const collider *col = &w->colliders[c];
        const sl_collider_desc *d = &col->desc;
        put_u32(wr, (uint32_t)col->removed); put_u32(wr, (uint32_t)col->enabled);
        put_u32(wr, (uint32_t)d->shape); put_vec3(wr, d->position);
        for (int k = 0; k < 4; k++) put_f32(wr, d->rotation[k]);
        put_vec3(wr, d->half_extents); put_f32(wr, d->radius); put_vec3(wr, d->normal);
        put_u32(wr, (uint32_t)(d->inside != 0)); put_f32(wr, d->friction);
        put_quat(wr, col->rot); put_quat(wr, col->prev_rot); put_vec3(wr, col->prev_pos); put_vec3(wr, col->force);
    }

    put_i32(wr, w->next_id);
    put_i32(wr, w->free_count);
    for (int k = 0; k < w->free_count; k++) put_i32(wr, w->free_ids[k]);
    put_i32(wr, n);
    for (int s = 0; s < n; s++) put_i32(wr, w->id[s]);

    put_i32(wr, w->object_count);
    for (int o = 0; o < w->object_count; o++) {
        const object *ob = &w->objects[o];
        put_u32(wr, (uint32_t)(ob->alive != 0));
        put_i32(wr, ob->gen);
        if (!ob->alive) continue;
        put_i32(wr, ob->kind); put_i32(wr, ob->nu); put_i32(wr, ob->nv); put_f32(wr, ob->self_dist);
        put_i32(wr, ob->count);
        for (int k = 0; k < ob->count; k++) put_i32(wr, ob->ids[k]);
    }

    for (int s = 0; s < n; s++) put_vec3(wr, w->x[s]);
    for (int s = 0; s < n; s++) put_vec3(wr, w->v[s]);
    for (int s = 0; s < n; s++) put_vec3(wr, w->x_build[s]);
    for (int s = 0; s < n; s++) put_f32(wr, w->mass[s]);
    for (int s = 0; s < n; s++) put_byte(wr, w->flags[s] & PERSISTENT_FLAGS);
    for (int s = 0; s < n; s++) put_byte(wr, w->calm[s]);
    for (int s = 0; s < n; s++) put_byte(wr, w->mat[s]);
    for (int s = 0; s < n; s++) put_byte(wr, w->wet[s]);
    for (int s = 0; s < n; s++) put_i32(wr, w->obj[s]);
    for (int k = 0; k < SL_PUSH_SLOTS * n; k++) put_byte(wr, w->push_id[k]);
    for (int k = 0; k < SL_PUSH_SLOTS * n; k++) if (w->push_id[k]) put_vec3(wr, w->push[k]);   /* most slots are empty */

    put_i32(wr, w->dist_count);
    for (int k = 0; k < w->dist_count; k++) {
        const dist_con *c = &w->dist[k];
        put_i32(wr, c->a); put_i32(wr, c->b); put_f32(wr, c->rest); put_f32(wr, c->compliance); put_i32(wr, c->obj);
    }
    for (int k = 0; k < SL_MAX_COLORS + 2; k++) put_i32(wr, w->dist_color_off[k]);

    put_i32(wr, w->cluster_count);
    put_i32(wr, w->member_count);
    for (int c = 0; c < w->cluster_count; c++) {
        const cluster *cl = &w->clusters[c];
        put_i32(wr, cl->first); put_i32(wr, cl->count); put_i32(wr, cl->obj);
        put_f32(wr, cl->stiffness); put_f32(wr, cl->plasticity); put_f32(wr, cl->pull);
        put_quat(wr, cl->rot); put_vec3(wr, cl->center);
    }
    for (int m = 0; m < w->member_count; m++) {
        put_i32(wr, w->members[m].slot); put_i32(wr, w->members[m].cluster); put_vec3(wr, w->members[m].rest);
    }

    put_i32(wr, w->grab_count);
    for (int g = 0; g < w->grab_count; g++) {
        put_i32(wr, w->grabs[g].id); put_vec3(wr, w->grabs[g].from); put_vec3(wr, w->grabs[g].to);
    }

    put_u32(wr, (uint32_t)(w->wake_all != 0));
    put_i32(wr, w->wake_count);
    for (int k = 0; k < w->wake_count; k++) { put_vec3(wr, w->wakes[k].lo); put_vec3(wr, w->wakes[k].hi); }
}

static uint32_t float_bits(float f) {
    uint32_t bits;
    memcpy(&bits, &f, 4);
    return bits;
}

static void write_header(const sl_world *w, writer *wr, uint64_t body_size, uint64_t checksum) {
    put_byte(wr, 'S'); put_byte(wr, 'L'); put_byte(wr, 'I'); put_byte(wr, 'M');
    put_u32(wr, SNAP_FORMAT);
    put_u32(wr, sl_version());
    put_u32(wr, float_bits(w->radius));
    put_i32(wr, w->max_particles);
    put_i32(wr, w->substeps);
    put_i32(wr, w->iterations);
    put_i32(wr, w->fluid_iterations);
    put_u32(wr, float_bits(w->sleep_speed));
    put_u32(wr, (uint32_t)w->use_aniso);
    put_u32(wr, (uint32_t)body_size);
    put_u32(wr, (uint32_t)(body_size >> 32));
    put_u32(wr, (uint32_t)checksum);
    put_u32(wr, (uint32_t)(checksum >> 32));
}

uint64_t sl_state_hash(const sl_world *w) {
    if (!w) return 0;
    writer wr = {0};
    write_body(w, &wr);
    return finish_hash(&wr);
}

size_t sl_snapshot_size(const sl_world *w) {
    if (!w) return 0;
    writer wr = {0};
    write_body(w, &wr);
    return SNAP_HEADER + wr.len;
}

size_t sl_snapshot_save(const sl_world *w, void *buf, size_t cap) {
    if (!w || !buf || cap < SNAP_HEADER) return 0;
    writer body = {0};
    body.buf = (unsigned char *)buf + SNAP_HEADER;
    body.cap = cap - SNAP_HEADER;
    write_body(w, &body);
    if (body.overflow) return 0;
    writer head = {0};
    head.buf = buf;
    head.cap = SNAP_HEADER;
    write_header(w, &head, body.len, finish_hash(&body));
    return SNAP_HEADER + body.len;
}

/* ---------- reading ---------- */

typedef struct {
    const unsigned char *p;
    size_t len, at;
    int bad, no_memory;
} reader;

static uint32_t get_u32(reader *rd) {
    if (rd->len - rd->at < 4) { rd->bad = 1; rd->at = rd->len; return 0; }
    const unsigned char *b = rd->p + rd->at;
    rd->at += 4;
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

static unsigned char get_byte(reader *rd) {
    if (rd->at >= rd->len) { rd->bad = 1; return 0; }
    return rd->p[rd->at++];
}

static int get_i32(reader *rd) { return (int)get_u32(rd); }

/* Floats from the network must be finite and of sane size: anything near the float range overflows to
   infinity inside the solver. 1e15 meters is far beyond any game world. */
#define SNAP_LIMIT 1e15f

static float get_range(reader *rd, float lo, float hi) {
    uint32_t bits = get_u32(rd);
    float v;
    memcpy(&v, &bits, 4);
    if (!(v >= lo && v <= hi)) { rd->bad = 1; return lo; }   /* also catches NaN */
    return v;
}

static float get_f32(reader *rd) { return get_range(rd, -SNAP_LIMIT, SNAP_LIMIT); }
static float get_size(reader *rd) { return get_range(rd, 0, SNAP_LIMIT); }

static sl_vec3 get_vec3(reader *rd) {
    sl_vec3 v;
    v.x = get_f32(rd); v.y = get_f32(rd); v.z = get_f32(rd);
    return v;
}

static quat get_quat(reader *rd) {
    quat q;
    q.x = get_f32(rd); q.y = get_f32(rd); q.z = get_f32(rd); q.w = get_f32(rd);
    return q;
}

/* An integer that must lie in [lo, hi]. */
static int get_int(reader *rd, int lo, int hi) {
    int v = get_i32(rd);
    if (v < lo || v > hi) { rd->bad = 1; return lo; }
    return v;
}

/* A particle id that must have been issued, its index below next_id. */
static int get_id(reader *rd, const sl_world *w, int next_id, int *index) {
    int id = get_i32(rd);
    *index = id >= 0 ? id_index(w, id) : -1;
    if (*index < 0 || *index >= next_id) { rd->bad = 1; *index = 0; }
    return id;
}

/* What the validating pass learns, so that everything can be allocated before the world is touched. */
typedef struct {
    int count, next_id, objects, dist, clusters, members;
    int *seen;        /* per id index: slot + 1 for live particles, -1 for free ones, 0 unseen (max_particles) */
    int *obj_ids;     /* per object: id count + 1 when alive, 0 when not (objects) */
} sizes;

/* Reads the body once to validate it (apply == 0, filling sz) and once more to store it (apply == 1), with
   new_ids holding the incoming objects' id arrays. The validating pass checks every count, index and float. */
static void read_body(sl_world *w, reader *rd, sizes *sz, int apply, sl_particle **new_ids) {
    sl_vec3 gravity = get_vec3(rd);
    unsigned step_count = get_u32(rd);
    int need_rebuild = get_int(rd, 0, 1);
    if (apply) { w->gravity = gravity; w->step_count = step_count; w->need_rebuild = need_rebuild; }

    int materials = get_int(rd, 0, SL_MAX_MATERIALS);
    for (int m = 0; m < materials && !rd->bad; m++) {
        sl_material_desc d;
        d.kind = (sl_kind)get_int(rd, SL_FLUID, SL_SOLID);
        d.density = get_size(rd); d.viscosity = get_size(rd); d.cohesion = get_size(rd); d.friction = get_size(rd);
        d.vorticity = get_size(rd); d.damping = get_size(rd); d.wet_cohesion = get_size(rd);
        if (!(d.density > 0)) rd->bad = 1;
        if (apply) w->materials[m] = d;
    }

    int colliders = get_int(rd, 0, SL_MAX_COLLIDERS);
    for (int c = 0; c < colliders && !rd->bad; c++) {
        collider col;
        memset(&col, 0, sizeof col);
        col.removed = get_int(rd, 0, 1);
        col.enabled = get_int(rd, 0, 1);
        col.desc.shape = (sl_shape)get_int(rd, SL_PLANE, SL_CAPSULE);
        col.desc.position = get_vec3(rd);
        for (int k = 0; k < 4; k++) col.desc.rotation[k] = get_f32(rd);
        col.desc.half_extents.x = get_size(rd); col.desc.half_extents.y = get_size(rd); col.desc.half_extents.z = get_size(rd);
        col.desc.radius = get_size(rd);
        col.desc.normal = get_vec3(rd);
        col.desc.inside = get_int(rd, 0, 1);
        col.desc.friction = get_size(rd);
        col.rot = get_quat(rd);
        col.prev_rot = get_quat(rd);
        col.prev_pos = get_vec3(rd);
        col.force = get_vec3(rd);
        if (apply) w->colliders[c] = col;
    }

    /* Every issued id index is either alive in exactly one slot or on the free list exactly once. */
    int next_id = get_int(rd, 0, w->max_particles), *seen = sz->seen, index;
    int free_count = get_int(rd, 0, next_id);
    if (!apply) for (int k = 0; k < next_id; k++) seen[k] = 0;
    for (int k = 0; k < free_count && !rd->bad; k++) {
        int id = get_id(rd, w, next_id, &index);
        if (!apply && seen[index]) rd->bad = 1;
        if (apply) w->free_ids[k] = id;
        else seen[index] = -1;
    }
    int n = get_int(rd, 0, w->max_particles);
    if (n + free_count != next_id) rd->bad = 1;
    for (int s = 0; s < n && !rd->bad; s++) {
        int id = get_id(rd, w, next_id, &index);
        if (!apply && seen[index]) rd->bad = 1;
        if (apply) { w->id[s] = id; w->id_slot[index] = s; }
        else seen[index] = s + 1;
    }
    if (!apply) { sz->count = n; sz->next_id = next_id; }
    if (apply) { w->next_id = next_id; w->free_count = free_count; }

    int objects = get_int(rd, 0, 1 << OBJ_BITS);
    if (!apply && !rd->bad) {
        sz->objects = objects;
        sz->obj_ids = objects ? sl__alloc(w, (size_t)objects * sizeof(int)) : NULL;
        if (objects && !sz->obj_ids) { rd->no_memory = 1; return; }
    }
    for (int o = 0; o < objects && !rd->bad; o++) {
        object ob;
        memset(&ob, 0, sizeof ob);
        ob.alive = get_int(rd, 0, 1);
        ob.gen = get_int(rd, 0, OBJ_GENS - 1);
        if (ob.alive) {
            ob.kind = get_int(rd, OBJ_ROPE, OBJ_SOFT);
            ob.nu = get_int(rd, 0, w->max_particles);
            ob.nv = get_int(rd, 0, w->max_particles);
            ob.self_dist = get_size(rd);
            ob.count = get_int(rd, 0, n);
            /* Callers index cloth particles by grid position, so the grid must match the count exactly. */
            if (ob.kind == OBJ_CLOTH ? (ob.nu < 2 || ob.nv < 2 || (long long)ob.nu * ob.nv != ob.count) : (ob.nu || ob.nv))
                rd->bad = 1;
            for (int k = 0; k < ob.count && !rd->bad; k++) {
                int id = get_id(rd, w, next_id, &index);
                if (!apply && seen[index] <= 0) rd->bad = 1;   /* must be a live particle */
                if (apply) new_ids[o][k] = id;
            }
            if (apply) ob.ids = new_ids[o];
        }
        if (apply) w->objects[o] = ob;
        else sz->obj_ids[o] = ob.alive ? ob.count + 1 : 0;
    }
    if (apply) w->object_count = objects;

    for (int s = 0; s < n && !rd->bad; s++) { sl_vec3 v = get_vec3(rd); if (apply) w->x[s] = w->p[s] = w->x_step[s] = v; }
    for (int s = 0; s < n && !rd->bad; s++) { sl_vec3 v = get_vec3(rd); if (apply) w->v[s] = v; }
    for (int s = 0; s < n && !rd->bad; s++) { sl_vec3 v = get_vec3(rd); if (apply) w->x_build[s] = v; }
    for (int s = 0; s < n && !rd->bad; s++) {
        float m = get_size(rd);
        if (!(m > 0)) rd->bad = 1;
        if (apply) w->mass[s] = m;
    }
    for (int s = 0; s < n && !rd->bad; s++) {
        unsigned char f = get_byte(rd);
        if (f & ~PERSISTENT_FLAGS) rd->bad = 1;
        if (apply) w->flags[s] = f;
    }
    for (int s = 0; s < n && !rd->bad; s++) { unsigned char c = get_byte(rd); if (apply) w->calm[s] = c; }
    for (int s = 0; s < n && !rd->bad; s++) {
        unsigned char m = get_byte(rd);
        if (m >= materials) rd->bad = 1;
        if (apply) { w->mat[s] = m; w->material[s] = m; }
    }
    for (int s = 0; s < n && !rd->bad; s++) { unsigned char v = get_byte(rd); if (apply) w->wet[s] = v; }
    for (int s = 0; s < n && !rd->bad; s++) {
        int o = get_int(rd, -1, objects - 1);
        if (!apply && o >= 0 && !sz->obj_ids[o]) rd->bad = 1;   /* belongs to an object that is gone */
        if (apply) w->obj[s] = o;
    }
    /* Which push slots are used decides which vectors follow, so both passes read the ids from the buffer. */
    size_t ids_at = rd->at;
    for (int k = 0; k < SL_PUSH_SLOTS * n && !rd->bad; k++) {
        unsigned char id = get_byte(rd);
        if (id > colliders) rd->bad = 1;
        if (apply) w->push_id[k] = id;
    }
    for (int k = 0; k < SL_PUSH_SLOTS * n && !rd->bad; k++) {
        sl_vec3 v = rd->p[ids_at + (size_t)k] ? get_vec3(rd) : v3(0, 0, 0);
        if (apply) w->push[k] = v;
    }

    int dist = get_int(rd, 0, 1 << 28);
    if (dist && (n == 0 || objects == 0)) rd->bad = 1;
    for (int k = 0; k < dist && !rd->bad; k++) {
        dist_con c;
        c.a = get_int(rd, 0, n - 1);
        c.b = get_int(rd, 0, n - 1);
        c.rest = get_size(rd);
        c.compliance = get_size(rd);
        c.obj = get_int(rd, 0, objects - 1);
        if (apply) w->dist[k] = c;
    }
    int prev = 0;
    for (int k = 0; k < SL_MAX_COLORS + 2 && !rd->bad; k++) {
        prev = get_int(rd, prev, dist);
        if (apply) w->dist_color_off[k] = prev;
    }
    if (prev != dist) rd->bad = 1;
    if (!apply) sz->dist = dist;
    if (apply) w->dist_count = dist;

    int clusters = get_int(rd, 0, 1 << 28), members = get_int(rd, 0, 1 << 28);
    if ((clusters && (objects == 0 || members == 0)) || (members && (n == 0 || clusters == 0))) rd->bad = 1;
    for (int c = 0; c < clusters && !rd->bad; c++) {
        cluster cl;
        cl.first = get_int(rd, 0, members - 1);
        cl.count = get_int(rd, 1, members - cl.first);
        cl.obj = get_int(rd, 0, objects - 1);
        cl.stiffness = get_range(rd, 0, 1); cl.plasticity = get_range(rd, 0, 1); cl.pull = get_range(rd, 0, 1);
        cl.rot = get_quat(rd);
        cl.center = get_vec3(rd);
        if (apply) w->clusters[c] = cl;
    }
    for (int m = 0; m < members && !rd->bad; m++) {
        member mb;
        mb.slot = get_int(rd, 0, n - 1);
        mb.cluster = get_int(rd, 0, clusters - 1);
        mb.rest = get_vec3(rd);
        if (apply) w->members[m] = mb;
    }
    if (!apply) { sz->clusters = clusters; sz->members = members; }
    if (apply) { w->cluster_count = clusters; w->member_count = members; }

    int grabs = get_int(rd, 0, SL_MAX_GRABS);
    for (int g = 0; g < grabs && !rd->bad; g++) {
        grab gr;
        gr.id = get_i32(rd);
        gr.from = get_vec3(rd);
        gr.to = get_vec3(rd);
        if (apply) w->grabs[g] = gr;
    }

    int wake_all = get_int(rd, 0, 1), wakes = get_int(rd, 0, SL_MAX_WAKES);
    for (int k = 0; k < wakes && !rd->bad; k++) {
        sl_vec3 lo = get_vec3(rd), hi = get_vec3(rd);
        if (apply) { w->wakes[k].lo = lo; w->wakes[k].hi = hi; }
    }
    if (apply) {
        w->material_count = materials;
        w->collider_count = colliders;
        w->grab_count = grabs;
        w->wake_all = wake_all;
        w->wake_count = wakes;
    }
    if (rd->at != rd->len) rd->bad = 1;   /* nothing may trail the body */
}

/* Grows every buffer the incoming state needs; existing contents are kept, so a failure changes nothing. */
static int reserve(sl_world *w, const sizes *sz, sl_particle ***new_ids) {
    *new_ids = NULL;
    /* Slots hold the free list and the id map too, which run up to next_id. */
    if (!sl__grow_slots(w, sz->next_id > sz->count ? sz->next_id : sz->count)
        || !sl__grow(w, (void **)&w->objects, &w->object_cap, sz->objects, sizeof(object))
        || !sl__grow(w, (void **)&w->dist, &w->dist_cap, sz->dist, sizeof(dist_con))
        || !sl__grow(w, (void **)&w->dist_lambda, &w->dist_lambda_cap, sz->dist, sizeof(float))
        || !sl__grow(w, (void **)&w->dist_tmp, &w->dist_tmp_cap, sz->dist + 1, sizeof(dist_con))
        || !sl__grow(w, (void **)&w->clusters, &w->cluster_cap, sz->clusters, sizeof(cluster))
        || !sl__grow(w, (void **)&w->members, &w->member_cap, sz->members, sizeof(member))) return 0;
    if (!sz->objects) return 1;
    sl_particle **ids = sl__alloc(w, (size_t)sz->objects * sizeof(sl_particle *));
    if (!ids) return 0;
    int ok = 1;
    for (int o = 0; o < sz->objects; o++) {
        ids[o] = NULL;
        if (ok && sz->obj_ids[o] > 1) ok = (ids[o] = sl__alloc(w, (size_t)(sz->obj_ids[o] - 1) * sizeof(sl_particle))) != NULL;
    }
    if (!ok) {
        for (int o = 0; o < sz->objects; o++) sl__free(w, ids[o]);
        sl__free(w, ids);
        return 0;
    }
    *new_ids = ids;
    return 1;
}

/* Everything that is not stored follows from what is: caches are rebuilt from the last build's positions. */
static void rebuild_derived(sl_world *w) {
    int n = w->count;
    for (int s = 0; s < n; s++) w->inv_mass[s] = (w->flags[s] & F_KINEMATIC) ? 0.0f : 1.0f / w->mass[s];
    w->active_count = w->contact_count = w->pair_count = w->island_count = 0;
    memset(w->color_off, 0, sizeof w->color_off);
    w->diffuse_count = 0;
    w->nbr_valid = 0;
    w->built = 0;
    w->islands_stale = 1;
    w->mem_dirty = 1;
    for (int s = 0; w->use_aniso && s < n; s++) {
        float r = w->radius;
        w->aniso[4 * s] = w->x[s];
        w->aniso[4 * s + 1] = v3(r, 0, 0);
        w->aniso[4 * s + 2] = v3(0, r, 0);
        w->aniso[4 * s + 3] = v3(0, 0, r);
    }
    if (w->need_rebuild || n == 0) return;

    /* Build from the positions the saving world built from: x is pointed at them while the build runs. */
    sl_vec3 *x = w->x;
    w->x = w->x_build;
    int ok = sl__grid_rebuild(w, 0);
    w->x = x;
    for (int s = 0; s < n; s++) w->p[s] = x[s];
    if (!ok) { w->need_rebuild = 1; return; }

    if (w->use_aniso) {
        for (int s = 0; s < n; s++) w->active[s] = s;
        w->active_count = n;
        sl__anisotropy_step(w);
    }
}

sl_snapshot_result sl_snapshot_load(sl_world *w, const void *buf, size_t size) {
    if (!w || !buf) return SL_SNAPSHOT_CORRUPT;
    const unsigned char *b = buf;
    if (size >= 4 && memcmp(b, "SLIM", 4) != 0) return SL_SNAPSHOT_CORRUPT;
    if (size < SNAP_HEADER) return SL_SNAPSHOT_TRUNCATED;

    reader head = {b, SNAP_HEADER, 4, 0, 0};
    uint32_t format = get_u32(&head), version = get_u32(&head);
    if (format != SNAP_FORMAT || version != sl_version()) return SL_SNAPSHOT_VERSION;
    uint32_t radius = get_u32(&head);
    int max_particles = get_i32(&head), substeps = get_i32(&head), iterations = get_i32(&head);
    int fluid_iterations = get_i32(&head);
    uint32_t sleep_speed = get_u32(&head), aniso = get_u32(&head);
    if (radius != float_bits(w->radius) || max_particles != w->max_particles || substeps != w->substeps
        || iterations != w->iterations || fluid_iterations != w->fluid_iterations
        || sleep_speed != float_bits(w->sleep_speed) || aniso != (uint32_t)w->use_aniso) return SL_SNAPSHOT_SETTINGS;
    uint64_t body_size = get_u32(&head);
    body_size |= (uint64_t)get_u32(&head) << 32;
    uint64_t checksum = get_u32(&head);
    checksum |= (uint64_t)get_u32(&head) << 32;
    if (body_size > size - SNAP_HEADER) return SL_SNAPSHOT_TRUNCATED;

    writer check = {0};
    for (uint64_t k = 0; k < body_size; k++) put_byte(&check, b[SNAP_HEADER + k]);
    if (finish_hash(&check) != checksum) return SL_SNAPSHOT_CORRUPT;

    sizes sz;
    memset(&sz, 0, sizeof sz);
    sz.seen = sl__alloc(w, (size_t)w->max_particles * sizeof(int));
    if (!sz.seen) return SL_SNAPSHOT_NO_MEMORY;
    reader rd = {b + SNAP_HEADER, (size_t)body_size, 0, 0, 0};
    read_body(w, &rd, &sz, 0, NULL);
    sl_snapshot_result result = rd.no_memory ? SL_SNAPSHOT_NO_MEMORY : rd.bad ? SL_SNAPSHOT_CORRUPT : SL_SNAPSHOT_OK;
    sl_particle **new_ids = NULL;
    if (result == SL_SNAPSHOT_OK && !reserve(w, &sz, &new_ids)) result = SL_SNAPSHOT_NO_MEMORY;

    if (result == SL_SNAPSHOT_OK) {
        for (int o = 0; o < w->object_count; o++) sl__free(w, w->objects[o].ids);
        reader again = {b + SNAP_HEADER, (size_t)body_size, 0, 0, 0};
        w->count = sz.count;
        read_body(w, &again, &sz, 1, new_ids);
        rebuild_derived(w);
    }
    sl__free(w, new_ids);
    sl__free(w, sz.obj_ids);
    sl__free(w, sz.seen);
    return result;
}
