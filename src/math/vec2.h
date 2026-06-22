#ifndef DGE_VEC2_H
#define DGE_VEC2_H

typedef struct {
    float x, y;
} Vec2;

static inline Vec2 vec2(float x, float y)          { return (Vec2){x, y}; }
static inline Vec2 vec2_add(Vec2 a, Vec2 b)        { return (Vec2){a.x+b.x, a.y+b.y}; }
static inline Vec2 vec2_sub(Vec2 a, Vec2 b)        { return (Vec2){a.x-b.x, a.y-b.y}; }
static inline Vec2 vec2_scale(Vec2 v, float s)     { return (Vec2){v.x*s, v.y*s}; }

#endif /* DGE_VEC2_H */
