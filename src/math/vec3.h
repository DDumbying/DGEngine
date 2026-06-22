#ifndef DGE_VEC3_H
#define DGE_VEC3_H

#include <math.h>

typedef struct {
    float x, y, z;
} Vec3;

static inline Vec3 vec3(float x, float y, float z)  { return (Vec3){x, y, z}; }
static inline Vec3 vec3_add(Vec3 a, Vec3 b)          { return (Vec3){a.x+b.x, a.y+b.y, a.z+b.z}; }
static inline Vec3 vec3_sub(Vec3 a, Vec3 b)          { return (Vec3){a.x-b.x, a.y-b.y, a.z-b.z}; }
static inline Vec3 vec3_scale(Vec3 v, float s)       { return (Vec3){v.x*s, v.y*s, v.z*s}; }
static inline float vec3_dot(Vec3 a, Vec3 b)         { return a.x*b.x + a.y*b.y + a.z*b.z; }
static inline float vec3_len(Vec3 v)                 { return sqrtf(vec3_dot(v, v)); }
static inline Vec3 vec3_norm(Vec3 v) {
    float l = vec3_len(v);
    return l > 0.0f ? vec3_scale(v, 1.0f/l) : (Vec3){0,0,0};
}
static inline Vec3 vec3_cross(Vec3 a, Vec3 b) {
    return (Vec3){
        a.y*b.z - a.z*b.y,
        a.z*b.x - a.x*b.z,
        a.x*b.y - a.y*b.x
    };
}

#endif /* DGE_VEC3_H */
