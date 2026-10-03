#include <string.h>
#include "internal.h"

static int new_object(sl_world *w, int kind) {
    if (!sl__grow(w, (void **)&w->objects, &w->object_cap, w->object_count + 1, sizeof(object))) return -1;
    object *o = &w->objects[w->object_count];
    memset(o, 0, sizeof *o);
    o->alive = 1;
    o->kind = kind;
    return w->object_count++;
}

static int spawn_into(sl_world *w, int obj, sl_material m, sl_vec3 pos) {
    object *o = &w->objects[obj];
    sl_particle id = sl_spawn(w, m, pos, v3(0, 0, 0));
    if (id < 0) return 0;
    int s = slot_of(w, id);
    w->obj[s] = obj;
    w->flags[s] &= (unsigned char)~F_FLUID;
    o->ids[o->count++] = id;
    return 1;
}

static int add_dist(sl_world *w, int obj, int a, int b, float compliance) {
    if (!sl__grow(w, (void **)&w->dist, &w->dist_cap, w->dist_count + 1, sizeof(dist_con))
        || !sl__grow(w, (void **)&w->dist_lambda, &w->dist_lambda_cap, w->dist_count + 1, sizeof(float))) return 0;
    w->dist[w->dist_count++] = (dist_con){a, b, v3_len(v3_sub(w->x[a], w->x[b])), compliance, obj};
    return 1;
}

/* Same coloring as contacts, so each color of distance constraints runs in parallel. */
void color_dist(sl_world *w) {
    int fill[SL_MAX_COLORS + 2];
    if (!sl__grow(w, (void **)&w->dist_tmp, &w->dist_tmp_cap, w->dist_count + 1, sizeof(dist_con))
        || !color_graph(w, (const int *)(void *)w->dist, (int)(sizeof(dist_con) / sizeof(int)), w->dist_count, w->dist_color_off)) {
        /* Without room to color, everything goes in the overflow color and runs on one thread. */
        memset(w->dist_color_off, 0, sizeof w->dist_color_off);
        w->dist_color_off[SL_MAX_COLORS + 1] = w->dist_count;
        return;
    }
    memcpy(fill, w->dist_color_off, sizeof fill);
    for (int k = 0; k < w->dist_count; k++) w->dist_tmp[fill[w->colors[k]]++] = w->dist[k];
    if (w->dist_count) memcpy(w->dist, w->dist_tmp, (size_t)w->dist_count * sizeof(dist_con));
}

static int alloc_ids(sl_world *w, int obj, int n) {
    w->objects[obj].ids = sl__alloc(w, (size_t)n * sizeof(sl_particle));
    return w->objects[obj].ids != NULL;
}

static sl_object finish(sl_world *w, int obj, int ok) {
    if (!ok) { sl_object_destroy(w, obj); return -1; }
    color_dist(w);
    w->mem_dirty = 1;
    w->need_rebuild = 1;
    return obj;
}

static int valid_material(const sl_world *w, sl_material m) { return w && m >= 0 && m < w->material_count; }

sl_object sl_rope_create(sl_world *w, sl_material m, sl_vec3 a, sl_vec3 b, float compliance) {
    if (!valid_material(w, m)) return -1;
    float len = v3_len(v3_sub(b, a));
    int n = (int)roundf(len / w->spacing) + 1;
    if (n < 2) n = 2;
    int obj = new_object(w, OBJ_ROPE);
    if (obj < 0) return -1;
    float step = len / (float)(n - 1);
    w->objects[obj].self_dist = fminf(w->spacing, 0.9f * step);
    int ok = alloc_ids(w, obj, n);
    for (int i = 0; ok && i < n; i++) ok = spawn_into(w, obj, m, v3_lerp(a, b, (float)i / (float)(n - 1)));
    const sl_particle *ids = w->objects[obj].ids;
    for (int i = 0; ok && i + 1 < n; i++) ok = add_dist(w, obj, slot_of(w, ids[i]), slot_of(w, ids[i + 1]), compliance);
    return finish(w, obj, ok);
}

sl_object sl_cloth_create(sl_world *w, sl_material m, sl_vec3 origin, sl_vec3 u, sl_vec3 v,
                          float stretch_compliance, float bend_compliance) {
    if (!valid_material(w, m)) return -1;
    int nu = (int)roundf(v3_len(u) / w->spacing) + 1, nv = (int)roundf(v3_len(v) / w->spacing) + 1;
    if (nu < 2) nu = 2;
    if (nv < 2) nv = 2;
    int obj = new_object(w, OBJ_CLOTH);
    if (obj < 0) return -1;
    object *o = &w->objects[obj];
    o->nu = nu;
    o->nv = nv;
    o->self_dist = fminf(w->spacing, 0.9f * fminf(v3_len(u) / (float)(nu - 1), v3_len(v) / (float)(nv - 1)));
    int ok = alloc_ids(w, obj, nu * nv);
    for (int j = 0; ok && j < nv; j++)
        for (int i = 0; ok && i < nu; i++)
            ok = spawn_into(w, obj, m, v3_add(origin, v3_add(v3_scale(u, (float)i / (float)(nu - 1)), v3_scale(v, (float)j / (float)(nv - 1)))));
    const sl_particle *ids = w->objects[obj].ids;
#define AT(i, j) slot_of(w, ids[(j) * nu + (i)])
    for (int j = 0; ok && j < nv; j++)
        for (int i = 0; ok && i < nu; i++) {
            if (i + 1 < nu) ok = ok && add_dist(w, obj, AT(i, j), AT(i + 1, j), stretch_compliance);
            if (j + 1 < nv) ok = ok && add_dist(w, obj, AT(i, j), AT(i, j + 1), stretch_compliance);
            if (i + 1 < nu && j + 1 < nv) {
                ok = ok && add_dist(w, obj, AT(i, j), AT(i + 1, j + 1), stretch_compliance);
                ok = ok && add_dist(w, obj, AT(i + 1, j), AT(i, j + 1), stretch_compliance);
            }
            if (i + 2 < nu) ok = ok && add_dist(w, obj, AT(i, j), AT(i + 2, j), bend_compliance);
            if (j + 2 < nv) ok = ok && add_dist(w, obj, AT(i, j), AT(i, j + 2), bend_compliance);
        }
#undef AT
    return finish(w, obj, ok);
}

/* Lattice ranges per axis: one block for small sizes, otherwise blocks of 5 that overlap by 3.
   Block c covers [2c, min(2c + 4, n - 1)]. */
static int axis_blocks(int n) { return n <= 5 ? 1 : (n - 4) / 2 + 1; }
static int block_hi(int n, int c) { return 2 * c + 4 < n - 1 ? 2 * c + 4 : n - 1; }

sl_object sl_softbody_create_box(sl_world *w, sl_material m, sl_vec3 min, sl_vec3 max,
                                 float stiffness, float plasticity) {
    if (!valid_material(w, m)) return -1;
    float d = w->spacing, r = w->radius;
    int n[3] = {(int)floorf((max.x - min.x - 2 * r) / d + 1e-4f) + 1,
                (int)floorf((max.y - min.y - 2 * r) / d + 1e-4f) + 1,
                (int)floorf((max.z - min.z - 2 * r) / d + 1e-4f) + 1};
    for (int a = 0; a < 3; a++) if (n[a] < 1) n[a] = 1;
    int obj = new_object(w, OBJ_SOFT);
    if (obj < 0) return -1;
    w->objects[obj].self_dist = 0.9f * d;
    int total = n[0] * n[1] * n[2];
    int ok = alloc_ids(w, obj, total);
    /* A small offset per particle breaks perfect columns, which would never bulge; it stays under the self contact gap. */
    unsigned seed = 0x9e3779b9u;
    for (int k = 0; ok && k < n[2]; k++)
        for (int j = 0; ok && j < n[1]; j++)
            for (int i = 0; ok && i < n[0]; i++) {
                float o[3];
                for (int a = 0; a < 3; a++) { seed = seed * 1664525u + 1013904223u; o[a] = ((float)(seed >> 8) / 16777216.0f - 0.5f) * 0.08f * d; }
                ok = spawn_into(w, obj, m, v3(min.x + r + i * d + o[0], min.y + r + j * d + o[1], min.z + r + k * d + o[2]));
            }

    const sl_particle *ids = w->objects[obj].ids;
    for (int cz = 0; ok && cz < axis_blocks(n[2]); cz++)
        for (int cy = 0; ok && cy < axis_blocks(n[1]); cy++)
            for (int cx = 0; ok && cx < axis_blocks(n[0]); cx++) {
                int lo[3] = {2 * cx, 2 * cy, 2 * cz}, hi[3] = {block_hi(n[0], cx), block_hi(n[1], cy), block_hi(n[2], cz)};
                int size = (hi[0] - lo[0] + 1) * (hi[1] - lo[1] + 1) * (hi[2] - lo[2] + 1);
                if (!sl__grow(w, (void **)&w->clusters, &w->cluster_cap, w->cluster_count + 1, sizeof(cluster))
                    || !sl__grow(w, (void **)&w->members, &w->member_cap, w->member_count + size, sizeof(member))) { ok = 0; break; }
                cluster *c = &w->clusters[w->cluster_count++];
                c->first = w->member_count;
                c->count = size;
                c->obj = obj;
                c->stiffness = stiffness < 0 ? 0 : (stiffness > 1 ? 1 : stiffness);
                c->plasticity = plasticity < 0 ? 0 : (plasticity > 1 ? 1 : plasticity);
                /* Applied every pass, so spread over all passes in a step to match the asked stiffness. */
                c->pull = c->stiffness >= 1 ? 1.0f : 1.0f - powf(1.0f - c->stiffness, 1.0f / (float)(w->substeps * w->iterations));
                c->rot = q_identity();
                sl_vec3 center = v3(0, 0, 0);
                for (int k = lo[2]; k <= hi[2]; k++)
                    for (int j = lo[1]; j <= hi[1]; j++)
                        for (int i = lo[0]; i <= hi[0]; i++) {
                            int s = slot_of(w, ids[(k * n[1] + j) * n[0] + i]);
                            w->members[w->member_count++] = (member){s, w->cluster_count - 1, w->x[s]};
                            center = v3_add(center, w->x[s]);
                        }
                c->center = v3_scale(center, 1.0f / (float)size);
                for (int q = c->first; q < c->first + size; q++) w->members[q].rest = v3_sub(w->members[q].rest, c->center);
            }
    return finish(w, obj, ok);
}

void sl_object_destroy(sl_world *w, sl_object o) {
    if (!w || o < 0 || o >= w->object_count || w->objects[o].alive != 1) return;
    w->objects[o].alive = 2;
    remove_doomed(w, NULL, 0);
}

/* Drops the object's constraints and record; its particles are removed by the caller. */
void objects_drop(sl_world *w, int o) {
    int n = 0;
    for (int k = 0; k < w->dist_count; k++) if (w->dist[k].obj != o) w->dist[n++] = w->dist[k];
    w->dist_count = n;
    int nc = 0, nm = 0;
    for (int c = 0; c < w->cluster_count; c++) {
        cluster cl = w->clusters[c];
        if (cl.obj == o) continue;
        memmove(&w->members[nm], &w->members[cl.first], (size_t)cl.count * sizeof(member));
        for (int m = nm; m < nm + cl.count; m++) w->members[m].cluster = nc;
        cl.first = nm;
        nm += cl.count;
        w->clusters[nc++] = cl;
    }
    w->cluster_count = nc;
    w->member_count = nm;
    sl__free(w, w->objects[o].ids);
    memset(&w->objects[o], 0, sizeof(object));
    w->mem_dirty = 1;
}

int sl_object_particles(const sl_world *w, sl_object o, const sl_particle **ids) {
    if (!w || o < 0 || o >= w->object_count || !w->objects[o].alive) { if (ids) *ids = NULL; return 0; }
    if (ids) *ids = w->objects[o].ids;
    return w->objects[o].count;
}

void sl_object_grid(const sl_world *w, sl_object o, int *nu, int *nv) {
    int ok = w && o >= 0 && o < w->object_count && w->objects[o].alive;
    if (nu) *nu = ok ? w->objects[o].nu : 0;
    if (nv) *nv = ok ? w->objects[o].nv : 0;
}

void objects_remap(sl_world *w, const int *old_to_new) {
    for (int k = 0; k < w->dist_count; k++) {
        w->dist[k].a = old_to_new[w->dist[k].a];
        w->dist[k].b = old_to_new[w->dist[k].b];
    }
    for (int m = 0; m < w->member_count; m++) w->members[m].slot = old_to_new[w->members[m].slot];
    w->mem_dirty = 1;
}

/* For each slot, the cluster members that point at it, so shape matching can gather per particle. */
int objects_membership(sl_world *w) {
    if (!sl__grow(w, (void **)&w->mem_list, &w->mem_list_cap, w->member_count + 1, sizeof(int))) return 0;
    for (int s = 0; s <= w->count; s++) w->mem_off[s] = 0;
    for (int m = 0; m < w->member_count; m++) w->mem_off[w->members[m].slot + 1]++;
    for (int s = 0; s < w->count; s++) w->mem_off[s + 1] += w->mem_off[s];
    int *fill = w->order;
    for (int s = 0; s < w->count; s++) fill[s] = w->mem_off[s];
    for (int m = 0; m < w->member_count; m++) w->mem_list[fill[w->members[m].slot]++] = m;
    return 1;
}

void objects_substep(sl_world *w) {
    for (int k = 0; k < w->dist_count; k++) w->dist_lambda[k] = 0;
}

static void solve_dist_range(sl_world *w, int begin, int end, int chunk, void *ctx) {
    (void)chunk;
    int base = *(int *)ctx;
    float inv_h2 = 1.0f / (w->hs * w->hs);
    for (int k = base + begin; k < base + end; k++) {
        const dist_con *c = &w->dist[k];
        int a = c->a, b = c->b;
        if (!(w->flags[a] & F_AWAKE)) continue;
        float wa = w->inv_mass[a], wb = w->inv_mass[b];
        sl_vec3 d = v3_sub(w->p[a], w->p[b]);
        float len = v3_len(d);
        if (len < 1e-9f) continue;
        float alpha = c->compliance * inv_h2, denom = wa + wb + alpha;
        if (denom <= 0) continue;
        float dl = (-(len - c->rest) - alpha * w->dist_lambda[k]) / denom;
        w->dist_lambda[k] += dl;
        sl_vec3 n = v3_scale(d, 1.0f / len);
        w->p[a] = v3_madd(w->p[a], n, dl * wa);
        w->p[b] = v3_madd(w->p[b], n, -dl * wb);
    }
}

/* Rotation that best maps the rest shape onto the current one, refined from last time (Mueller 2016). */
static quat extract_rotation(const sl_vec3 a[3], quat q) {
    for (int it = 0; it < 4; it++) {
        sl_vec3 r[3] = {q_rotate(q, v3(1, 0, 0)), q_rotate(q, v3(0, 1, 0)), q_rotate(q, v3(0, 0, 1))};
        sl_vec3 omega = v3_add(v3_add(v3_cross(r[0], a[0]), v3_cross(r[1], a[1])), v3_cross(r[2], a[2]));
        omega = v3_scale(omega, 1.0f / (fabsf(v3_dot(r[0], a[0]) + v3_dot(r[1], a[1]) + v3_dot(r[2], a[2])) + 1e-9f));
        float wl = v3_len(omega);
        if (wl < 1e-9f) break;
        q = q_norm(q_mul(q_axis_angle(v3_scale(omega, 1.0f / wl), wl), q));
    }
    return q;
}

static void match_clusters(sl_world *w, int begin, int end, int chunk, void *ctx) {
    (void)chunk; (void)ctx;
    for (int c = begin; c < end; c++) {
        cluster *cl = &w->clusters[c];
        if (!(w->flags[w->members[cl->first].slot] & F_AWAKE)) continue;
        sl_vec3 center = v3(0, 0, 0);
        float mass = 0;
        for (int m = cl->first; m < cl->first + cl->count; m++) {
            int s = w->members[m].slot;
            center = v3_madd(center, w->p[s], w->mass[s]);
            mass += w->mass[s];
        }
        center = v3_scale(center, 1.0f / mass);
        sl_vec3 a[3] = {v3(0, 0, 0), v3(0, 0, 0), v3(0, 0, 0)};
        for (int m = cl->first; m < cl->first + cl->count; m++) {
            int s = w->members[m].slot;
            sl_vec3 d = v3_scale(v3_sub(w->p[s], center), w->mass[s]), r = w->members[m].rest;
            a[0] = v3_madd(a[0], d, r.x);
            a[1] = v3_madd(a[1], d, r.y);
            a[2] = v3_madd(a[2], d, r.z);
        }
        cl->center = center;
        cl->rot = extract_rotation(a, cl->rot);
    }
}

static void match_particles(sl_world *w, int begin, int end, int chunk, void *ctx) {
    (void)chunk; (void)ctx;
    for (int k = begin; k < end; k++) {
        int s = w->active[k];
        int first = w->mem_off[s], last = w->mem_off[s + 1];
        if (first == last || (w->flags[s] & F_KINEMATIC)) continue;
        sl_vec3 goal = v3(0, 0, 0);
        float pull = 0;
        for (int q = first; q < last; q++) {
            const member *mb = &w->members[w->mem_list[q]];
            const cluster *cl = &w->clusters[mb->cluster];
            goal = v3_add(goal, v3_add(cl->center, q_rotate(cl->rot, mb->rest)));
            pull += cl->pull;
        }
        float inv = 1.0f / (float)(last - first);
        w->p[s] = v3_madd(w->p[s], v3_sub(v3_scale(goal, inv), w->p[s]), pull * inv);
    }
}

void objects_solve(sl_world *w) {
    run_colors(w, w->dist_color_off, solve_dist_range);
    if (w->cluster_count) {
        sl__parallel(w, w->cluster_count, match_clusters, NULL);
        sl__parallel(w, w->active_count, match_particles, NULL);
    }
}

/* Plastic clusters adopt part of any large deformation as their new rest shape. */
void objects_plasticity(sl_world *w) {
    float yield = 0.1f * w->spacing;
    for (int c = 0; c < w->cluster_count; c++) {
        cluster *cl = &w->clusters[c];
        if (cl->plasticity <= 0 || !(w->flags[w->members[cl->first].slot] & F_AWAKE)) continue;
        quat inv = q_conj(cl->rot);
        sl_vec3 mean = v3(0, 0, 0);
        for (int m = cl->first; m < cl->first + cl->count; m++) {
            member *mb = &w->members[m];
            sl_vec3 e = v3_sub(q_rotate(inv, v3_sub(w->x[mb->slot], cl->center)), mb->rest);
            if (v3_len(e) > yield) mb->rest = v3_madd(mb->rest, e, cl->plasticity);
            mean = v3_add(mean, mb->rest);
        }
        mean = v3_scale(mean, 1.0f / (float)cl->count);
        for (int m = cl->first; m < cl->first + cl->count; m++) w->members[m].rest = v3_sub(w->members[m].rest, mean);
    }
}
