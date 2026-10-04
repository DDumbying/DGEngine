#include "internal.h"

/* Symmetric 3x3 eigen decomposition by Jacobi rotations; columns of vec are the eigenvectors. */
static void eigen3(float a[3][3], float val[3], float vec[3][3]) {
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) vec[i][j] = i == j ? 1.0f : 0.0f;
    for (int sweep = 0; sweep < 8; sweep++) {
        for (int p = 0; p < 2; p++)
            for (int q = p + 1; q < 3; q++) {
                if (fabsf(a[p][q]) < 1e-12f) continue;
                float theta = 0.5f * (a[q][q] - a[p][p]) / a[p][q];
                float t = (theta >= 0 ? 1.0f : -1.0f) / (fabsf(theta) + sqrtf(theta * theta + 1.0f));
                float c = 1.0f / sqrtf(t * t + 1.0f), s = t * c;
                for (int k = 0; k < 3; k++) {
                    float akp = a[k][p], akq = a[k][q];
                    a[k][p] = c * akp - s * akq;
                    a[k][q] = s * akp + c * akq;
                }
                for (int k = 0; k < 3; k++) {
                    float apk = a[p][k], aqk = a[q][k];
                    a[p][k] = c * apk - s * aqk;
                    a[q][k] = s * apk + c * aqk;
                }
                for (int k = 0; k < 3; k++) {
                    float vkp = vec[k][p], vkq = vec[k][q];
                    vec[k][p] = c * vkp - s * vkq;
                    vec[k][q] = s * vkp + c * vkq;
                }
            }
    }
    for (int i = 0; i < 3; i++) val[i] = a[i][i];
}

/* Yu and Turk anisotropy: each fluid particle becomes an ellipsoid flattened along the local surface. */
static void aniso_range(sl_world *w, int begin, int end, int chunk, void *ctx) {
    (void)chunk; (void)ctx;
    float r = w->radius, h = w->h, h2 = h * h;
    for (int k = begin; k < end; k++) {
        int s = w->active[k];
        sl_vec3 *out = &w->aniso[4 * s], xi = w->x[s];
        out[0] = xi;
        out[1] = v3(r, 0, 0);
        out[2] = v3(0, r, 0);
        out[3] = v3(0, 0, r);
        if (!(w->flags[s] & F_FLUID) || s >= w->built) continue;
        /* One pass, relative to xi: weighted sums of offsets and their products give mean and covariance. */
        float wsum = 0, m2[6] = {0};
        sl_vec3 m1 = v3(0, 0, 0);
        int count = 0;
        for (int n = w->nbr_off[s]; n < w->nbr_off[s + 1]; n++) {
            int j = w->nbr[n];
            if (j >= w->count || !(w->flags[j] & F_FLUID)) continue;
            sl_vec3 e = v3_sub(w->x[j], xi);
            float d2 = v3_len2(e);
            if (d2 >= h2) continue;
            float q = sqrtf(d2) / h, wt = 1.0f - q * q * q;
            m1 = v3_madd(m1, e, wt);
            m2[0] += wt * e.x * e.x; m2[1] += wt * e.x * e.y; m2[2] += wt * e.x * e.z;
            m2[3] += wt * e.y * e.y; m2[4] += wt * e.y * e.z; m2[5] += wt * e.z * e.z;
            wsum += wt;
            count++;
        }
        if (count < 4) continue;
        sl_vec3 m = v3_scale(m1, 1.0f / (wsum + 1.0f)), mean = v3_add(xi, m);
        /* sum wt (e - m)(e - m)^T = M2 - M1 m^T - m M1^T + wsum m m^T */
        float mv[3] = {m.x, m.y, m.z}, sv[3] = {m1.x, m1.y, m1.z};
        int idx[3][3] = {{0, 1, 2}, {1, 3, 4}, {2, 4, 5}};
        float c[3][3];
        for (int a = 0; a < 3; a++)
            for (int b = 0; b < 3; b++) c[a][b] = m2[idx[a][b]] - sv[a] * mv[b] - mv[a] * sv[b] + wsum * mv[a] * mv[b];
        float val[3], vec[3][3];
        eigen3(c, val, vec);
        float top = sl_max(val[0], sl_max(val[1], val[2]));
        if (top <= 1e-12f) continue;
        float s3[3], prod = 1;
        for (int a = 0; a < 3; a++) { s3[a] = sqrtf(sl_max(val[a], top / 16.0f)); prod *= s3[a]; }
        float norm = r / sl_cbrt(prod);
        out[0] = v3_lerp(xi, mean, 0.9f);
        for (int a = 0; a < 3; a++) out[1 + a] = v3_scale(v3(vec[0][a], vec[1][a], vec[2][a]), s3[a] * norm);
    }
}

/* Sleeping particles keep the shape they had; it moves with them when memory is reordered. */
void sl__anisotropy_step(sl_world *w) { sl__parallel(w, w->active_count, aniso_range, NULL); }

static unsigned hash3(unsigned a, unsigned b, unsigned c) {
    unsigned h = a * 0x9e3779b1u ^ b * 0x85ebca77u ^ c * 0xc2b2ae3du;
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    return h;
}

static float rand01(unsigned a, unsigned b, unsigned c) { return (float)(hash3(a, b, c) & 0xffffff) / 16777216.0f; }

static float ramp(float v, float lo, float hi) { return v <= lo ? 0.0f : (v >= hi ? 1.0f : (v - lo) / (hi - lo)); }

/* New spray where fast water folds and churns, judged by trapped air and kinetic energy (as in FleX). */
static void spawn_diffuse(sl_world *w) {
    float h = w->h, h2 = h * h, dt = w->dt, r = w->radius;
    for (int k = 0; k < w->active_count && w->diffuse_count < w->max_diffuse; k++) {
        int i = w->active[k];
        if (!(w->flags[i] & F_FLUID) || i >= w->built) continue;
        sl_vec3 vi = w->v[i];
        float speed2 = v3_len2(vi);
        if (speed2 < 1.0f) continue;
        float trapped = 0;
        for (int n = w->nbr_off[i]; n < w->nbr_off[i + 1]; n++) {
            int j = w->nbr[n];
            if (j >= w->count || !(w->flags[j] & F_FLUID)) continue;
            sl_vec3 d = v3_sub(w->x[i], w->x[j]), dv = v3_sub(vi, w->v[j]);
            float d2 = v3_len2(d), dvl = v3_len(dv);
            if (d2 >= h2 || d2 < 1e-12f || dvl < 1e-6f) continue;
            float dl = sqrtf(d2);
            trapped += dvl * (1.0f - v3_dot(dv, d) / (dvl * dl)) * (1.0f - dl / h);
        }
        float rate = 60.0f * ramp(trapped, 2.0f, 12.0f) * ramp(0.5f * speed2, 1.0f, 8.0f);
        unsigned id = (unsigned)w->id[i], step = w->step_count;
        int spawn = (int)(rate * dt + rand01(id, step, 7));
        sl_vec3 dir = v3_scale(vi, 1.0f / sqrtf(speed2));
        sl_vec3 side = fabsf(dir.y) < 0.9f ? v3_cross(dir, v3(0, 1, 0)) : v3_cross(dir, v3(1, 0, 0));
        side = v3_scale(side, 1.0f / v3_len(side));
        sl_vec3 side2 = v3_cross(dir, side);
        for (int q = 0; q < spawn && w->diffuse_count < w->max_diffuse; q++) {
            float a = 6.2831853f * rand01(id, step, 11u + (unsigned)q), rad = r * sqrtf(rand01(id, step, 23u + (unsigned)q));
            float sa, ca;
            sl_sincos(a, &sa, &ca);
            sl_vec3 off = v3_add(v3_scale(side, ca * rad), v3_scale(side2, sa * rad));
            off = v3_madd(off, vi, dt * rand01(id, step, 31u + (unsigned)q));
            int d = w->diffuse_count++;
            w->dpos[d] = v3_add(w->x[i], off);
            w->dvel[d] = vi;
            w->dlife[d] = 1.5f + 2.5f * rand01(id, step, 41u + (unsigned)q);
            w->dkind[d] = SL_SPRAY;
        }
    }
}

/* Spray flies freely, foam rides the surface, bubbles rise inside; all fade with time. */
static void move_diffuse(sl_world *w, int begin, int end, int chunk, void *ctx) {
    (void)chunk; (void)ctx;
    float dt = w->dt;
    sl_vec3 up = v3_len(w->gravity) > 0 ? v3_scale(w->gravity, -1.0f / v3_len(w->gravity)) : v3(0, 1, 0);
    for (int d = begin; d < end; d++) {
        sl_vec3 avg;
        int n = sl__grid_fluid_near(w, w->dpos[d], &avg);
        unsigned char kind = n < 6 ? SL_SPRAY : (n < 20 ? SL_FOAM : SL_BUBBLE);
        w->dkind[d] = kind;
        if (kind == SL_SPRAY) {
            w->dvel[d] = v3_madd(w->dvel[d], w->gravity, dt);
            w->dlife[d] -= dt * 0.5f;
        } else if (kind == SL_FOAM) {
            w->dvel[d] = avg;
            w->dlife[d] -= dt;
        } else {
            w->dvel[d] = v3_madd(avg, up, 0.4f);
            w->dlife[d] -= dt * 0.5f;
        }
        w->dpos[d] = v3_madd(w->dpos[d], w->dvel[d], dt);
        for (int c = 0; c < w->collider_count; c++) {
            if (!w->colliders[c].enabled) continue;
            sl_vec3 nrm;
            float dist = sl__collider_distance(&w->colliders[c], w->dpos[d], &nrm);
            if (dist < 0) {
                w->dpos[d] = v3_madd(w->dpos[d], nrm, -dist);
                float vn = v3_dot(w->dvel[d], nrm);
                if (vn < 0) w->dvel[d] = v3_madd(w->dvel[d], nrm, -vn);
                w->dlife[d] -= dt;
            }
        }
    }
}

void sl__diffuse_step(sl_world *w) {
    sl__parallel(w, w->diffuse_count, move_diffuse, NULL);
    int n = 0;
    for (int d = 0; d < w->diffuse_count; d++) {
        if (w->dlife[d] <= 0 || !v3_finite(w->dpos[d])) continue;
        w->dpos[n] = w->dpos[d];
        w->dvel[n] = w->dvel[d];
        w->dlife[n] = w->dlife[d];
        w->dkind[n] = w->dkind[d];
        n++;
    }
    w->diffuse_count = n;
    spawn_diffuse(w);
}
