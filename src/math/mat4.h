#ifndef DGE_MAT4_H
#define DGE_MAT4_H

#include <math.h>
#include <string.h>
#include "vec3.h"

/* Column-major 4x4 matrix (matches OpenGL convention).
   m[col][row]  */
typedef struct {
    float m[4][4];
} Mat4;

static inline Mat4 mat4_identity(void) {
    Mat4 r;
    memset(&r, 0, sizeof(r));
    r.m[0][0] = r.m[1][1] = r.m[2][2] = r.m[3][3] = 1.0f;
    return r;
}

static inline Mat4 mat4_mul(Mat4 a, Mat4 b) {
    Mat4 r;
    memset(&r, 0, sizeof(r));
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++)
            for (int k = 0; k < 4; k++)
                r.m[c][row] += a.m[k][row] * b.m[c][k];
    return r;
}

static inline Mat4 mat4_ortho(float left, float right,
                               float bottom, float top,
                               float near, float far) {
    Mat4 r = mat4_identity();
    r.m[0][0] =  2.0f / (right - left);
    r.m[1][1] =  2.0f / (top   - bottom);
    r.m[2][2] = -2.0f / (far   - near);
    r.m[3][0] = -(right + left)   / (right - left);
    r.m[3][1] = -(top   + bottom) / (top   - bottom);
    r.m[3][2] = -(far   + near)   / (far   - near);
    return r;
}

static inline Mat4 mat4_translate(Mat4 m, Vec3 t) {
    Mat4 r = m;
    r.m[3][0] += t.x;
    r.m[3][1] += t.y;
    r.m[3][2] += t.z;
    return r;
}

static inline Mat4 mat4_scale(Mat4 m, Vec3 s) {
    Mat4 r = m;
    r.m[0][0] *= s.x;
    r.m[1][1] *= s.y;
    r.m[2][2] *= s.z;
    return r;
}

/* Returns a pointer to the first element, for passing to glUniformMatrix4fv. */
static inline const float *mat4_ptr(const Mat4 *m) {
    return &m->m[0][0];
}

#endif /* DGE_MAT4_H */
