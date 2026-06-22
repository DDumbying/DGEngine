#ifndef DGE_MATH_UTILS_H
#define DGE_MATH_UTILS_H

#include <math.h>

#define DGE_PI      3.14159265358979323846f
#define DGE_DEG2RAD (DGE_PI / 180.0f)
#define DGE_RAD2DEG (180.0f / DGE_PI)

static inline float dge_clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
static inline float dge_lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

#endif /* DGE_MATH_UTILS_H */
