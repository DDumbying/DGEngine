#ifndef SLIME_VEC3_H
#define SLIME_VEC3_H

#include "dmath.h"   /* first, so its float settings cover everything after it */
#include "slime/slime.h"

static inline sl_vec3 v3(float x, float y, float z) { sl_vec3 r = {x, y, z}; return r; }
static inline sl_vec3 v3_add(sl_vec3 a, sl_vec3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline sl_vec3 v3_sub(sl_vec3 a, sl_vec3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline sl_vec3 v3_scale(sl_vec3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline sl_vec3 v3_madd(sl_vec3 a, sl_vec3 b, float s) { return v3(a.x + b.x * s, a.y + b.y * s, a.z + b.z * s); }
static inline float v3_dot(sl_vec3 a, sl_vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline float v3_len2(sl_vec3 a) { return v3_dot(a, a); }
static inline float v3_len(sl_vec3 a) { return sqrtf(v3_dot(a, a)); }
static inline int v3_finite(sl_vec3 a) { return isfinite(a.x) && isfinite(a.y) && isfinite(a.z); }

static inline sl_vec3 v3_cross(sl_vec3 a, sl_vec3 b) {
    return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

static inline sl_vec3 v3_lerp(sl_vec3 a, sl_vec3 b, float t) {
    return v3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
}

typedef struct { float x, y, z, w; } quat;

static inline quat q_identity(void) { quat q = {0, 0, 0, 1}; return q; }

static inline quat q_norm(quat q) {
    float n = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (n < 1e-12f) return q_identity();
    quat r = {q.x / n, q.y / n, q.z / n, q.w / n};
    return r;
}

static inline quat q_from(const float r[4]) {
    if (!r || (r[0] == 0 && r[1] == 0 && r[2] == 0 && r[3] == 0)) return q_identity();
    quat q = {r[0], r[1], r[2], r[3]};
    return q_norm(q);
}

static inline quat q_conj(quat q) { quat r = {-q.x, -q.y, -q.z, q.w}; return r; }

static inline quat q_mul(quat a, quat b) {
    quat r = {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
              a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
              a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
              a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
    return r;
}

static inline quat q_axis_angle(sl_vec3 axis, float angle) {
    float s, c;
    sl_sincos(angle * 0.5f, &s, &c);
    quat r = {axis.x * s, axis.y * s, axis.z * s, c};
    return r;
}

static inline sl_vec3 q_rotate(quat q, sl_vec3 v) {
    sl_vec3 u = v3(q.x, q.y, q.z);
    sl_vec3 t = v3_scale(v3_cross(u, v), 2.0f);
    return v3_add(v3_add(v, v3_scale(t, q.w)), v3_cross(u, t));
}

static inline quat q_nlerp(quat a, quat b, float t) {
    if (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0) { b.x = -b.x; b.y = -b.y; b.z = -b.z; b.w = -b.w; }
    quat r = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
    return q_norm(r);
}

#endif
