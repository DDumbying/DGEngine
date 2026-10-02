#ifndef SLIME_VEC3_H
#define SLIME_VEC3_H

#include <math.h>
#include "slime/slime.h"

static inline sl_vec3 v3(float x, float y, float z) { sl_vec3 r = {x, y, z}; return r; }
static inline sl_vec3 v3_add(sl_vec3 a, sl_vec3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline sl_vec3 v3_sub(sl_vec3 a, sl_vec3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline sl_vec3 v3_scale(sl_vec3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline float v3_dot(sl_vec3 a, sl_vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline float v3_len2(sl_vec3 a) { return v3_dot(a, a); }
static inline float v3_len(sl_vec3 a) { return sqrtf(v3_dot(a, a)); }

static inline sl_vec3 v3_cross(sl_vec3 a, sl_vec3 b) {
    return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

static inline sl_vec3 v3_lerp(sl_vec3 a, sl_vec3 b, float t) {
    return v3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
}

typedef struct { float x, y, z, w; } quat;

static inline quat q_identity(void) { quat q = {0, 0, 0, 1}; return q; }

static inline quat q_from(const float r[4]) {
    if (!r || (r[0] == 0 && r[1] == 0 && r[2] == 0 && r[3] == 0)) return q_identity();
    float n = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2] + r[3] * r[3]);
    quat q = {r[0] / n, r[1] / n, r[2] / n, r[3] / n};
    return q;
}

static inline quat q_conj(quat q) { quat r = {-q.x, -q.y, -q.z, q.w}; return r; }

static inline sl_vec3 q_rotate(quat q, sl_vec3 v) {
    sl_vec3 u = v3(q.x, q.y, q.z);
    sl_vec3 t = v3_scale(v3_cross(u, v), 2.0f);
    return v3_add(v3_add(v, v3_scale(t, q.w)), v3_cross(u, t));
}

static inline quat q_nlerp(quat a, quat b, float t) {
    if (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0) { b.x = -b.x; b.y = -b.y; b.z = -b.z; b.w = -b.w; }
    float r[4] = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
    return q_from(r);
}

#endif
