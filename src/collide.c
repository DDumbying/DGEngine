#include "internal.h"

/* Signed distance and outward normal in the shape's local frame. */
static float sdf_local(const sl_collider_desc *d, sl_vec3 p, sl_vec3 *n) {
    switch (d->shape) {
    case SL_SPHERE: {
        float len = v3_len(p);
        *n = len > 1e-8f ? v3_scale(p, 1.0f / len) : v3(0, 1, 0);
        return len - d->radius;
    }
    case SL_CAPSULE: {
        float hy = d->half_extents.y;
        float cy = p.y < -hy ? -hy : (p.y > hy ? hy : p.y);
        sl_vec3 diff = v3(p.x, p.y - cy, p.z);
        float len = v3_len(diff);
        *n = len > 1e-8f ? v3_scale(diff, 1.0f / len) : v3(1, 0, 0);
        return len - d->radius;
    }
    case SL_BOX: {
        sl_vec3 b = d->half_extents;
        sl_vec3 q = v3(fabsf(p.x) - b.x, fabsf(p.y) - b.y, fabsf(p.z) - b.z);
        sl_vec3 s = v3(p.x < 0 ? -1.0f : 1.0f, p.y < 0 ? -1.0f : 1.0f, p.z < 0 ? -1.0f : 1.0f);
        if (q.x > 0 || q.y > 0 || q.z > 0) {
            sl_vec3 o = v3(q.x > 0 ? q.x : 0, q.y > 0 ? q.y : 0, q.z > 0 ? q.z : 0);
            float len = v3_len(o);
            *n = v3(o.x * s.x / len, o.y * s.y / len, o.z * s.z / len);
            return len;
        }
        if (q.x >= q.y && q.x >= q.z) { *n = v3(s.x, 0, 0); return q.x; }
        if (q.y >= q.z) { *n = v3(0, s.y, 0); return q.y; }
        *n = v3(0, 0, s.z);
        return q.z;
    }
    default:
        *n = d->normal;
        return v3_dot(p, d->normal);
    }
}

static void frame(const collider *col, float t, sl_vec3 *pos, quat *rot) {
    *pos = v3_lerp(col->prev_pos, col->desc.position, t);
    *rot = col->desc.shape == SL_PLANE ? q_identity() : q_nlerp(col->prev_rot, col->rot, t);
}

void collider_frames(sl_world *w, float t) {
    float t0 = t - 1.0f / (float)w->substeps;
    for (int c = 0; c < w->collider_count; c++) {
        collider *col = &w->colliders[c];
        frame(col, t, &col->pos_t, &col->rot_t);
        frame(col, t0, &col->pos_t0, &col->rot_t0);
    }
}

/* Density a wall adds, as if the fluid lattice continued behind it; z is the gap to that first hidden layer. */
void wall_table_init(sl_world *w) {
    float d = w->spacing, h = w->h;
    int n = (int)ceilf(h / d);
    for (int s = 0; s <= SL_WALL_SAMPLES; s++) {
        float z = h * (float)s / SL_WALL_SAMPLES, sum = 0;
        for (float layer = z; layer < h; layer += d)
            for (int a = -n; a <= n; a++)
                for (int b = -n; b <= n; b++)
                    sum += kernel(sqrtf(layer * layer + (float)(a * a + b * b) * d * d), h);
        w->wall_table[s] = sum;
    }
}

static float wall_sample(const sl_world *w, float z, float *slope) {
    float f = z / w->h * SL_WALL_SAMPLES;
    if (f >= SL_WALL_SAMPLES) { *slope = 0; return 0; }
    if (f < 0) f = 0;
    int k = (int)f;
    float a = w->wall_table[k], b = w->wall_table[k + 1];
    *slope = (b - a) * SL_WALL_SAMPLES / w->h;
    return a + (b - a) * (f - (float)k);
}

static void add_wall(const sl_world *w, float dist, sl_vec3 n, float *rho, sl_vec3 *grad) {
    float slope, v = wall_sample(w, dist + w->radius, &slope);
    if (v <= 0) return;
    *rho += v;
    *grad = v3_add(*grad, v3_scale(n, slope));
}

float wall_density(const sl_world *w, int i, sl_vec3 *grad) {
    float rho = 0;
    *grad = v3(0, 0, 0);
    for (int c = 0; c < w->collider_count; c++) {
        const collider *col = &w->colliders[c];
        const sl_collider_desc *d = &col->desc;
        sl_vec3 pos = col->pos_t, n;
        quat rot = col->rot_t;
        sl_vec3 local = q_rotate(q_conj(rot), v3_sub(w->p[i], pos));
        if (d->shape == SL_BOX && d->inside) {
            const float *lp = &local.x, *b = &d->half_extents.x;
            for (int a = 0; a < 3; a++) {
                n = v3(0, 0, 0);
                (&n.x)[a] = lp[a] > 0 ? -1.0f : 1.0f;
                add_wall(w, b[a] - fabsf(lp[a]), q_rotate(rot, n), &rho, grad);
            }
            continue;
        }
        float dist = sdf_local(d, local, &n);
        if (d->inside) { dist = -dist; n = v3_scale(n, -1.0f); }
        add_wall(w, dist, q_rotate(rot, n), &rho, grad);
    }
    return rho;
}

static void contact(sl_world *w, int i, sl_vec3 n, float pen, sl_vec3 surf_move, float mu) {
    w->touch[i] = 1;
    w->p[i] = v3_add(w->p[i], v3_scale(n, pen));
    sl_vec3 rel = v3_sub(v3_sub(w->p[i], w->x[i]), surf_move);
    sl_vec3 tan = v3_sub(rel, v3_scale(n, v3_dot(rel, n)));
    float tl = v3_len(tan);
    if (tl <= 1e-9f) return;
    float f = tl < mu * pen ? 1.0f : fminf(mu * pen / tl, 1.0f);
    w->p[i] = v3_sub(w->p[i], v3_scale(tan, f));
}

void collide_particles(sl_world *w) {
    float r = w->radius;
    for (int c = 0; c < w->collider_count; c++) {
        const collider *col = &w->colliders[c];
        const sl_collider_desc *d = &col->desc;
        int container = d->shape == SL_BOX && d->inside;
        sl_vec3 pos = col->pos_t, pos0 = col->pos_t0;
        quat rot = col->rot_t, rot0 = col->rot_t0;
        quat inv = q_conj(rot);

        for (int i = 0; i < w->count; i++) {
            sl_vec3 local = q_rotate(inv, v3_sub(w->p[i], pos));
            sl_vec3 surf_move = v3_sub(v3_add(pos, q_rotate(rot, local)), v3_add(pos0, q_rotate(rot0, local)));

            /* Inside a box each wall is its own contact, so edges and corners hold too. */
            if (container) {
                const float *lp = &local.x, *b = &d->half_extents.x;
                for (int a = 0; a < 3; a++) {
                    float lim = b[a] - r, pen = fabsf(lp[a]) - lim;
                    if (pen <= 0) continue;
                    sl_vec3 n = v3(0, 0, 0);
                    (&n.x)[a] = lp[a] > 0 ? -1.0f : 1.0f;
                    contact(w, i, q_rotate(rot, n), pen, surf_move, d->friction);
                }
                continue;
            }

            sl_vec3 n;
            float dist = sdf_local(d, local, &n);
            if (d->inside) { dist = -dist; n = v3_scale(n, -1.0f); }
            float pen = r - dist;
            if (pen > 0) contact(w, i, q_rotate(rot, n), pen, surf_move, d->friction);
        }
    }
}
