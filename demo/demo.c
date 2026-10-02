#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "slime/slime.h"

#ifndef SLIME_DEMO_DIR
#define SLIME_DEMO_DIR "demo"
#endif

#define MAX_PARTICLES 120000
#define MAX_DIFFUSE 60000
#define RADIUS 0.025f
#define NEAR_PLANE 0.05
#define FAR_PLANE 100.0
#define MAX_OBJECTS 128
#define MAX_SHAPES 32

/* ---------- shaders ---------- */

#define SKY_GLSL \
    "vec3 sky(vec3 d) {\n" \
    "    vec3 sun = normalize(vec3(0.4, 0.75, 0.3));\n" \
    "    float t = clamp(d.y * 1.4 + 0.15, 0.0, 1.0);\n" \
    "    vec3 col = mix(vec3(0.86, 0.88, 0.90), vec3(0.32, 0.52, 0.82), t);\n" \
    "    if (d.y < 0.0) col = mix(vec3(0.86, 0.88, 0.90), vec3(0.42, 0.40, 0.38), clamp(-d.y * 3.0, 0.0, 1.0));\n" \
    "    return col + vec3(1.0, 0.9, 0.7) * pow(max(dot(d, sun), 0.0), 600.0) * 4.0;\n" \
    "}\n"

static const char *SKY_FS =
    "#version 330\n"
    "uniform vec2 uRes;\n"
    "uniform vec2 uProjScale;\n"
    "uniform mat4 uInvView;\n"
    "out vec4 finalColor;\n"
    SKY_GLSL
    "void main() {\n"
    "    vec2 ndc = gl_FragCoord.xy / uRes * 2.0 - 1.0;\n"
    "    vec3 d = normalize(mat3(uInvView) * vec3(ndc.x / uProjScale.x, ndc.y / uProjScale.y, -1.0));\n"
    "    finalColor = vec4(sky(d), 1.0);\n"
    "}\n";

static const char *FLOOR_VS =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "uniform mat4 mvp;\n"
    "out vec3 world;\n"
    "void main() { world = vertexPosition; gl_Position = mvp * vec4(vertexPosition, 1.0); }\n";

static const char *FLOOR_FS =
    "#version 330\n"
    "in vec3 world;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    vec2 cell = floor(world.xz / 0.5);\n"
    "    float checker = mod(cell.x + cell.y, 2.0);\n"
    "    vec2 f = abs(fract(world.xz / 0.5) - 0.5);\n"
    "    float line = smoothstep(0.49, 0.5, max(f.x, f.y));\n"
    "    vec3 col = mix(vec3(0.80, 0.78, 0.74), vec3(0.72, 0.70, 0.66), checker) * (1.0 - 0.25 * line);\n"
    "    float fade = clamp(1.0 - length(world.xz) / 9.0, 0.0, 1.0);\n"
    "    finalColor = vec4(mix(vec3(0.86, 0.88, 0.90), col, fade), 1.0);\n"
    "}\n";

static const char *SPHERE_VS =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in vec3 vertexNormal;\n"
    "in mat4 instanceTransform;\n"
    "uniform mat4 mvp;\n"
    "out vec3 normal;\n"
    "void main() {\n"
    "    normal = vertexNormal;\n"
    "    gl_Position = mvp * instanceTransform * vec4(vertexPosition, 1.0);\n"
    "}\n";

static const char *SPHERE_FS =
    "#version 330\n"
    "in vec3 normal;\n"
    "uniform vec4 colDiffuse;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    float light = max(dot(normalize(normal), normalize(vec3(0.4, 0.75, 0.3))), 0.0) * 0.55 + 0.5;\n"
    "    finalColor = vec4(colDiffuse.rgb * light, 1.0);\n"
    "}\n";

/* Each instance matrix maps a unit sphere onto the particle's ellipsoid; the quad faces the camera. */
static const char *SPLAT_VS =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in mat4 instanceTransform;\n"
    "uniform mat4 uView;\n"
    "uniform mat4 uProj;\n"
    "out vec3 ray;\n"
    "flat out mat4 inv;\n"
    "void main() {\n"
    "    mat4 m = uView * instanceTransform;\n"
    "    float r = max(length(instanceTransform[0].xyz), max(length(instanceTransform[1].xyz), length(instanceTransform[2].xyz)));\n"
    "    vec3 p = m[3].xyz + vec3(vertexPosition.xy * r, r);\n"
    "    ray = p;\n"
    "    inv = inverse(m);\n"
    "    gl_Position = uProj * vec4(p, 1.0);\n"
    "}\n";

#define ELLIPSOID_HIT \
    "    vec3 dir = normalize(ray);\n" \
    "    vec3 o = (inv * vec4(0.0, 0.0, 0.0, 1.0)).xyz, d = (inv * vec4(dir, 0.0)).xyz;\n" \
    "    float a = dot(d, d), b = dot(o, d), c = dot(o, o) - 1.0, disc = b * b - a * c;\n" \
    "    if (disc < 0.0) discard;\n" \
    "    float t0 = (-b - sqrt(disc)) / a, t1 = (-b + sqrt(disc)) / a;\n"

static const char *DEPTH_FS =
    "#version 330\n"
    "in vec3 ray;\n"
    "flat in mat4 inv;\n"
    "uniform mat4 uProj;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    ELLIPSOID_HIT
    "    vec3 p = dir * t0;\n"
    "    vec4 clip = uProj * vec4(p, 1.0);\n"
    "    gl_FragDepth = clip.z / clip.w * 0.5 + 0.5;\n"
    "    finalColor = vec4(-p.z, 0.0, 0.0, 1.0);\n"
    "}\n";

static const char *THICK_FS =
    "#version 330\n"
    "in vec3 ray;\n"
    "flat in mat4 inv;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    ELLIPSOID_HIT
    "    finalColor = vec4((t1 - t0) * 0.5, 0.0, 0.0, 1.0);\n"
    "}\n";

/* Spray and foam: a soft dot whose size sits in the first matrix entry and fade in the fourth. */
static const char *SPECK_VS =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in mat4 instanceTransform;\n"
    "uniform mat4 uView;\n"
    "uniform mat4 uProj;\n"
    "out vec2 corner;\n"
    "out float alpha;\n"
    "void main() {\n"
    "    vec4 c = uView * vec4(instanceTransform[3].xyz, 1.0);\n"
    "    corner = vertexPosition.xy;\n"
    "    alpha = instanceTransform[0].w;\n"
    "    gl_Position = uProj * (c + vec4(vertexPosition.xy * instanceTransform[0].x, 0.0, 0.0));\n"
    "}\n";

static const char *SPECK_FS =
    "#version 330\n"
    "in vec2 corner;\n"
    "in float alpha;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    float r2 = dot(corner, corner);\n"
    "    if (r2 > 1.0) discard;\n"
    "    finalColor = vec4(vec3(0.97, 0.98, 1.0), alpha * (1.0 - r2));\n"
    "}\n";

/* Narrow-range filter (Truong and Yuksel): smooths the surface without blurring across edges. */
static const char *BLUR_FS =
    "#version 330\n"
    "uniform sampler2D uSrc;\n"
    "uniform vec2 uRes;\n"
    "uniform vec2 uDir;\n"
    "uniform float uFilter;\n"
    "uniform float uRange;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    vec2 uv = gl_FragCoord.xy / uRes;\n"
    "    float d = texture(uSrc, uv).r;\n"
    "    if (d <= 0.0) { finalColor = vec4(0.0); return; }\n"
    "    float radius = clamp(uFilter / d, 1.0, 20.0), sigma = radius * 0.5;\n"
    "    float lo = d - uRange, hi = d + uRange, sum = 0.0, wsum = 0.0;\n"
    "    for (int i = -20; i <= 20; i++) {\n"
    "        float fi = float(i);\n"
    "        if (abs(fi) > radius) continue;\n"
    "        float s = texture(uSrc, uv + uDir * fi / uRes).r;\n"
    "        if (s <= 0.0 || s > hi) continue;\n"
    "        if (s < lo) s = lo;\n"
    "        float w = exp(-fi * fi / (2.0 * sigma * sigma));\n"
    "        sum += s * w;\n"
    "        wsum += w;\n"
    "    }\n"
    "    finalColor = vec4(sum / wsum, 0.0, 0.0, 1.0);\n"
    "}\n";

static const char *SURFACE_FS =
    "#version 330\n"
    "uniform sampler2D texture0;\n"
    "uniform sampler2D uBgDepth;\n"
    "uniform sampler2D uDepth;\n"
    "uniform sampler2D uThick;\n"
    "uniform vec2 uRes;\n"
    "uniform vec2 uProjScale;\n"
    "uniform float uNear;\n"
    "uniform float uFar;\n"
    "uniform mat4 uInvView;\n"
    "uniform vec3 uAbsorb;\n"
    "uniform vec3 uDeep;\n"
    "uniform float uRefract;\n"
    "uniform float uShine;\n"
    "out vec4 finalColor;\n"
    SKY_GLSL
    "float linear_depth(float z) { float n = z * 2.0 - 1.0; return 2.0 * uNear * uFar / (uFar + uNear - n * (uFar - uNear)); }\n"
    "vec3 eye_pos(vec2 uv, float d) { vec2 ndc = uv * 2.0 - 1.0; return vec3(ndc.x * d / uProjScale.x, ndc.y * d / uProjScale.y, -d); }\n"
    "void main() {\n"
    "    vec2 uv = gl_FragCoord.xy / uRes, px = 1.0 / uRes;\n"
    "    vec4 bg = texture(texture0, uv);\n"
    "    float bgz = texture(uBgDepth, uv).r;\n"
    "    float d = texture(uDepth, uv).r;\n"
    "    if (d <= 0.0 || d > linear_depth(bgz)) { finalColor = bg; gl_FragDepth = bgz; return; }\n"
    "    vec3 p = eye_pos(uv, d);\n"
    "    float xr = texture(uDepth, uv + vec2(px.x, 0.0)).r, xl = texture(uDepth, uv - vec2(px.x, 0.0)).r;\n"
    "    float yu = texture(uDepth, uv + vec2(0.0, px.y)).r, yd = texture(uDepth, uv - vec2(0.0, px.y)).r;\n"
    "    vec3 dx = eye_pos(uv + vec2(px.x, 0.0), xr) - p, dx2 = p - eye_pos(uv - vec2(px.x, 0.0), xl);\n"
    "    if (xr <= 0.0 || (xl > 0.0 && abs(dx2.z) < abs(dx.z))) dx = dx2;\n"
    "    vec3 dy = eye_pos(uv + vec2(0.0, px.y), yu) - p, dy2 = p - eye_pos(uv - vec2(0.0, px.y), yd);\n"
    "    if (yu <= 0.0 || (yd > 0.0 && abs(dy2.z) < abs(dy.z))) dy = dy2;\n"
    "    vec3 n = normalize(cross(dx, dy));\n"
    "    if (n.z < 0.0) n = -n;\n"
    "    vec3 v = normalize(-p);\n"
    "    float thick = texture(uThick, uv).r;\n"
    "    vec3 nw = mat3(uInvView) * n, vw = mat3(uInvView) * v;\n"
    "    float fresnel = 0.02 + 0.98 * pow(1.0 - max(dot(n, v), 0.0), 5.0);\n"
    "    vec3 behind = texture(texture0, uv + n.xy * uRefract * min(thick * 6.0, 1.0)).rgb;\n"
    "    vec3 body = mix(uDeep, behind, exp(-uAbsorb * thick));\n"
    "    vec3 refl = sky(reflect(-vw, nw));\n"
    "    float spec = pow(max(dot(nw, normalize(normalize(vec3(0.4, 0.75, 0.3)) + vw)), 0.0), uShine);\n"
    "    finalColor = vec4(mix(body, refl, fresnel) + spec * 1.2, 1.0);\n"
    "    float zn = (uFar + uNear) / (uFar - uNear) - 2.0 * uFar * uNear / ((uFar - uNear) * d);\n"
    "    gl_FragDepth = zn * 0.5 + 0.5;\n"
    "}\n";

/* ---------- render targets ---------- */

typedef struct { RenderTexture2D rt; int w, h; } target;

static target target_make(int w, int h, int format) {
    target t = {0};
    t.w = w;
    t.h = h;
    t.rt.id = rlLoadFramebuffer();
    t.rt.texture = (Texture2D){rlLoadTexture(NULL, w, h, format, 1), w, h, 1, format};
    t.rt.depth = (Texture2D){rlLoadTextureDepth(w, h, false), w, h, 1, 19};
    rlFramebufferAttach(t.rt.id, t.rt.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
    rlFramebufferAttach(t.rt.id, t.rt.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
    if (!rlFramebufferComplete(t.rt.id)) TraceLog(LOG_WARNING, "render target incomplete");
    SetTextureFilter(t.rt.texture, TEXTURE_FILTER_POINT);
    return t;
}

static void target_free(target *t) {
    if (!t->rt.id) return;
    rlUnloadTexture(t->rt.texture.id);
    rlUnloadTexture(t->rt.depth.id);
    rlUnloadFramebuffer(t->rt.id);
    memset(t, 0, sizeof *t);
}

typedef struct { target scene, mid, depth, blur, thick; } targets;

static void targets_resize(targets *t, int w, int h) {
    if (t->scene.w == w && t->scene.h == h) return;
    target *all[] = {&t->scene, &t->mid, &t->depth, &t->blur, &t->thick};
    for (int i = 0; i < 5; i++) target_free(all[i]);
    t->scene = target_make(w, h, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    t->mid = target_make(w, h, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    t->depth = target_make(w, h, PIXELFORMAT_UNCOMPRESSED_R32);
    t->blur = target_make(w, h, PIXELFORMAT_UNCOMPRESSED_R32);
    t->thick = target_make(w, h, PIXELFORMAT_UNCOMPRESSED_R32);
}

/* ---------- simulation state ---------- */

enum { K_CLOTH, K_ROPE, K_SLIME };
enum { SH_BOX, SH_BALL };

typedef struct { sl_collider id; int shape; Vector3 pos, size; } shape;

typedef struct {
    sl_world *world;
    sl_material water, sand, slime, cloth;
    sl_collider floor, container, push;
    int container_on;
    sl_object objects[MAX_OBJECTS];
    int kinds[MAX_OBJECTS], object_count;
    shape shapes[MAX_SHAPES];
    int shape_count, scene, workers;
} sim;

static const char *SCENE_NAMES[] = {"sandbox", "water and sand", "rope bridge", "curtain", "slime"};

static void keep(sim *s, sl_object o, int kind) {
    if (o < 0 || s->object_count >= MAX_OBJECTS) return;
    s->objects[s->object_count] = o;
    s->kinds[s->object_count++] = kind;
}

static sl_vec3 sv(Vector3 v) { return (sl_vec3){v.x, v.y, v.z}; }
static Vector3 rv(sl_vec3 v) { return (Vector3){v.x, v.y, v.z}; }
static sl_vec3 grow(sl_vec3 v, float k) { return (sl_vec3){v.x * k, v.y * k, v.z * k}; }

static void add_shape(sim *s, int kind, Vector3 pos, float size) {
    if (s->shape_count >= MAX_SHAPES) return;
    shape *sh = &s->shapes[s->shape_count];
    sh->shape = kind;
    sh->pos = pos;
    sh->size = (Vector3){size, size, size};
    sl_collider_desc d = {0};
    d.shape = kind == SH_BOX ? SL_BOX : SL_SPHERE;
    d.position = sv(pos);
    d.half_extents = sv(sh->size);
    d.radius = size;
    d.friction = 0.5f;
    sh->id = sl_collider_add(s->world, &d);
    if (sh->id >= 0) s->shape_count++;
}

static void remove_shape(sim *s, int k) {
    sl_collider_remove(s->world, s->shapes[k].id);
    s->shapes[k] = s->shapes[--s->shape_count];
}

static void spawn_slime(sim *s, Vector3 c, float size) {
    sl_vec3 lo = {c.x - size, c.y - size, c.z - size}, hi = {c.x + size, c.y + size, c.z + size};
    keep(s, sl_softbody_create_box(s->world, s->slime, lo, hi, 0.12f, 0.15f), K_SLIME);
}

/* Pours a layer of particles; they leave at one spacing per frame, so layers never overlap. */
static void pour(sim *s, sl_material m, Vector3 at, float radius) {
    float d = 2.0f * RADIUS, speed = d * 60.0f;
    int n = (int)(radius / d);
    for (int i = -n; i <= n; i++)
        for (int k = -n; k <= n; k++) {
            if ((float)(i * i + k * k) * d * d > radius * radius) continue;
            float jx = (float)GetRandomValue(-100, 100) * 0.001f * d, jz = (float)GetRandomValue(-100, 100) * 0.001f * d;
            sl_spawn(s->world, m, (sl_vec3){at.x + i * d + jx, at.y, at.z + k * d + jz}, (sl_vec3){0, -speed, 0});
        }
}

static void pin_index(sim *s, sl_object o, int index) {
    const sl_particle *ids;
    int n = sl_object_particles(s->world, o, &ids);
    if (index >= 0 && index < n) sl_pin(s->world, ids[index], 1);
}

static void set_container(sim *s, int on) {
    s->container_on = on;
    sl_collider_set_enabled(s->world, s->container, on);
}

static void scene_load(sim *s, int scene) {
    int workers = s->workers;
    if (s->world) sl_world_destroy(s->world);
    memset(s, 0, sizeof *s);
    s->workers = workers;
    s->scene = scene;
    sl_world_desc desc = {0};
    desc.max_particles = MAX_PARTICLES;
    desc.particle_radius = RADIUS;
    desc.gravity = (sl_vec3){0, -9.81f, 0};
    desc.workers = workers;
    desc.anisotropy = 1;
    desc.max_diffuse = MAX_DIFFUSE;
    s->world = sl_world_create(&desc);
    sl_world *w = s->world;

    s->water = sl_material_add(w, &(sl_material_desc){.kind = SL_FLUID, .density = 1000, .viscosity = 0.02f, .vorticity = 0.05f});
    s->sand = sl_material_add(w, &(sl_material_desc){.kind = SL_GRANULAR, .density = 1600, .friction = 0.7f, .wet_cohesion = 0.8f});
    s->slime = sl_material_add(w, &(sl_material_desc){.kind = SL_SOLID, .density = 900, .friction = 0.6f, .damping = 0.5f});
    s->cloth = sl_material_add(w, &(sl_material_desc){.kind = SL_SOLID, .density = 400, .friction = 0.5f, .damping = 0.8f});

    s->floor = sl_collider_add(w, &(sl_collider_desc){.shape = SL_PLANE, .normal = {0, 1, 0}, .friction = 0.6f});
    s->container = sl_collider_add(w, &(sl_collider_desc){.shape = SL_BOX, .position = {0, 1.0f, 0},
                                                         .half_extents = {1.2f, 1.0f, 0.8f}, .inside = 1, .friction = 0.4f});
    s->push = sl_collider_add(w, &(sl_collider_desc){.shape = SL_SPHERE, .radius = 0.15f, .position = {0, -5, 0}, .friction = 0.2f});
    sl_collider_set_enabled(w, s->push, 0);
    set_container(s, scene != 0);

    if (scene == 1) {
        sl_spawn_box(w, s->water, (sl_vec3){0.2f, 0.0f, -0.8f}, (sl_vec3){1.2f, 0.6f, 0.8f});
        sl_spawn_box(w, s->sand, (sl_vec3){-0.6f, 0.0f, -0.4f}, (sl_vec3){-0.1f, 0.5f, 0.3f});
        add_shape(s, SH_BALL, (Vector3){-0.7f, 0.35f, 0.45f}, 0.2f);
    } else if (scene == 2) {
        sl_object deck = sl_cloth_create(w, s->cloth, (sl_vec3){-1.15f, 1.0f, -0.25f}, (sl_vec3){2.3f, 0, 0}, (sl_vec3){0, 0, 0.5f}, 0, 0.02f);
        int nu, nv;
        sl_object_grid(w, deck, &nu, &nv);
        pin_index(s, deck, 0);
        pin_index(s, deck, nu - 1);
        pin_index(s, deck, nu * (nv - 1));
        pin_index(s, deck, nu * nv - 1);
        keep(s, deck, K_CLOTH);
        for (int side = -1; side <= 1; side += 2) {
            sl_object rail = sl_rope_create(w, s->cloth, (sl_vec3){-1.15f, 1.35f, side * 0.3f}, (sl_vec3){1.15f, 1.35f, side * 0.3f}, 0);
            pin_index(s, rail, 0);
            pin_index(s, rail, sl_object_particles(w, rail, NULL) - 1);
            keep(s, rail, K_ROPE);
        }
        spawn_slime(s, (Vector3){-0.4f, 1.6f, 0}, 0.12f);
        sl_spawn_box(w, s->water, (sl_vec3){-1.2f, 0.0f, -0.8f}, (sl_vec3){1.2f, 0.2f, 0.8f});
    } else if (scene == 3) {
        sl_object curtain = sl_cloth_create(w, s->cloth, (sl_vec3){-0.7f, 1.85f, 0.1f}, (sl_vec3){1.4f, 0, 0}, (sl_vec3){0, -1.1f, 0}, 0, 0.002f);
        int nu, nv;
        sl_object_grid(w, curtain, &nu, &nv);
        for (int i = 0; i < nu; i += 6) pin_index(s, curtain, i);
        pin_index(s, curtain, nu - 1);
        keep(s, curtain, K_CLOTH);
        add_shape(s, SH_BALL, (Vector3){0, 1.0f, -0.5f}, 0.2f);
    } else if (scene == 4) {
        sl_spawn_box(w, s->water, (sl_vec3){-1.2f, 0.0f, -0.8f}, (sl_vec3){1.2f, 0.3f, 0.8f});
        spawn_slime(s, (Vector3){-0.6f, 1.3f, 0.1f}, 0.14f);
        spawn_slime(s, (Vector3){0.1f, 1.7f, -0.2f}, 0.12f);
        spawn_slime(s, (Vector3){0.7f, 1.4f, 0.2f}, 0.16f);
    }
}

/* ---------- tools ---------- */

enum { T_GRAB, T_PUSH, T_WATER, T_SAND, T_SLIME, T_CLOTH, T_ROPE, T_BOX, T_BALL, T_ERASE, T_COUNT };
static const char *TOOL_NAMES[T_COUNT] = {"grab", "push", "water", "sand", "slime", "cloth", "rope", "box", "ball", "erase"};
static const int TOOL_KEYS[T_COUNT] = {KEY_G, KEY_P, KEY_W, KEY_S, KEY_M, KEY_C, KEY_R, KEY_B, KEY_O, KEY_E};
static const char *TOOL_KEY_NAMES[T_COUNT] = {"G", "P", "W", "S", "M", "C", "R", "B", "O", "E"};

typedef struct {
    int tool;
    float brush;
    int has_point, grabbing, moving_shape, rope_started;
    Vector3 point, normal, rope_start, drag_plane_point, drag_offset;
    sl_particle grabbed;
    float push_radius;
} tools;

typedef struct { int hit; float dist; Vector3 point, normal; int shape; sl_particle particle; } pick;

/* Nearest thing under the mouse: a particle, a placed shape or the floor. */
static pick pick_ray(const sim *s, Ray ray) {
    pick p = {0, 1e9f, {0}, {0, 1, 0}, -1, -1};
    float d;
    sl_particle hit = sl_raycast(s->world, sv(ray.position), sv(ray.direction), 50.0f, &d);
    if (hit >= 0) {
        p.hit = 1;
        p.dist = d;
        p.particle = hit;
        p.point = Vector3Add(ray.position, Vector3Scale(ray.direction, d));
        p.normal = Vector3Normalize(Vector3Subtract(p.point, rv(sl_position(s->world, hit))));
    }
    for (int k = 0; k < s->shape_count; k++) {
        const shape *sh = &s->shapes[k];
        RayCollision rc = sh->shape == SH_BALL ? GetRayCollisionSphere(ray, sh->pos, sh->size.x)
                                               : GetRayCollisionBox(ray, (BoundingBox){Vector3Subtract(sh->pos, sh->size), Vector3Add(sh->pos, sh->size)});
        if (rc.hit && rc.distance < p.dist) { p.hit = 1; p.dist = rc.distance; p.point = rc.point; p.normal = rc.normal; p.particle = -1; p.shape = k; }
    }
    if (ray.direction.y < -1e-4f) {
        float t = -ray.position.y / ray.direction.y;
        if (t > 0 && t < p.dist) { p.hit = 1; p.dist = t; p.point = Vector3Add(ray.position, Vector3Scale(ray.direction, t)); p.normal = (Vector3){0, 1, 0}; p.particle = -1; p.shape = -1; }
    }
    return p;
}

/* Mouse on the plane through a point that faces the camera, for dragging things around. */
static Vector3 drag_point(Ray ray, Vector3 on, Vector3 facing) {
    float denom = Vector3DotProduct(ray.direction, facing);
    if (fabsf(denom) < 1e-5f) return on;
    float t = Vector3DotProduct(Vector3Subtract(on, ray.position), facing) / denom;
    return Vector3Add(ray.position, Vector3Scale(ray.direction, t));
}

static void use_tools(sim *s, tools *t, Camera3D cam, Ray ray, int pressed, int down, int released) {
    pick pk = pick_ray(s, ray);
    t->has_point = pk.hit;
    t->point = pk.point;
    t->normal = pk.normal;
    Vector3 facing = Vector3Normalize(Vector3Subtract(cam.position, cam.target));
    Vector3 above = Vector3Add(pk.point, Vector3Scale(pk.normal, t->brush + 0.05f));

    switch (t->tool) {
    case T_GRAB:
        if (pressed && pk.hit && pk.particle >= 0 && sl_grab_begin(s->world, pk.particle)) {
            t->grabbing = 1;
            t->grabbed = pk.particle;
            t->drag_plane_point = rv(sl_position(s->world, pk.particle));
        } else if (pressed && pk.hit && pk.shape >= 0) {
            t->moving_shape = pk.shape + 1;
            t->drag_plane_point = s->shapes[pk.shape].pos;
            t->drag_offset = Vector3Subtract(s->shapes[pk.shape].pos, drag_point(ray, t->drag_plane_point, facing));
        }
        if (down && t->grabbing) sl_grab_move(s->world, t->grabbed, sv(drag_point(ray, t->drag_plane_point, facing)));
        if (down && t->moving_shape) {
            shape *sh = &s->shapes[t->moving_shape - 1];
            sh->pos = Vector3Add(drag_point(ray, t->drag_plane_point, facing), t->drag_offset);
            if (sh->pos.y < sh->size.y) sh->pos.y = sh->size.y;
            sl_collider_move(s->world, sh->id, sv(sh->pos), NULL);
        }
        if (released) {
            if (t->grabbing) sl_grab_end(s->world, t->grabbed);
            t->grabbing = t->moving_shape = 0;
        }
        break;
    case T_PUSH:
        if (down && pk.hit) {
            Vector3 c = Vector3Add(pk.point, Vector3Scale(pk.normal, t->brush * 0.5f));
            if (!sl_collider_enabled(s->world, s->push) || t->push_radius != t->brush) {
                sl_collider_remove(s->world, s->push);
                s->push = sl_collider_add(s->world, &(sl_collider_desc){.shape = SL_SPHERE, .radius = t->brush, .position = sv(c), .friction = 0.2f});
                t->push_radius = t->brush;
            }
            sl_collider_move(s->world, s->push, sv(c), NULL);
        }
        if (released || !down) sl_collider_set_enabled(s->world, s->push, 0);
        break;
    case T_WATER:
    case T_SAND:
        if (down && pk.hit) pour(s, t->tool == T_WATER ? s->water : s->sand, (Vector3){pk.point.x, pk.point.y + 0.6f, pk.point.z}, t->brush);
        break;
    case T_SLIME:
        if (pressed && pk.hit) spawn_slime(s, (Vector3){above.x, above.y + 0.1f, above.z}, t->brush);
        break;
    case T_CLOTH:
        if (pressed && pk.hit) {
            float size = t->brush * 6.0f;
            sl_object c = sl_cloth_create(s->world, s->cloth, (sl_vec3){pk.point.x - size * 0.5f, pk.point.y + 0.8f, pk.point.z - size * 0.5f},
                                          (sl_vec3){size, 0, 0}, (sl_vec3){0, 0, size}, 0, 0.01f);
            if (IsKeyDown(KEY_LEFT_SHIFT)) {
                int nu, nv;
                sl_object_grid(s->world, c, &nu, &nv);
                pin_index(s, c, 0);
                pin_index(s, c, nu - 1);
            }
            keep(s, c, K_CLOTH);
        }
        break;
    case T_ROPE:
        if (pressed && pk.hit) { t->rope_started = 1; t->rope_start = (Vector3){pk.point.x, pk.point.y + 0.6f, pk.point.z}; }
        if (released && t->rope_started && pk.hit) {
            Vector3 end = {pk.point.x, pk.point.y + 0.6f, pk.point.z};
            if (Vector3Distance(end, t->rope_start) > 4.0f * RADIUS) {
                sl_object r = sl_rope_create(s->world, s->cloth, sv(t->rope_start), sv(end), 0);
                pin_index(s, r, 0);
                pin_index(s, r, sl_object_particles(s->world, r, NULL) - 1);
                keep(s, r, K_ROPE);
            }
            t->rope_started = 0;
        }
        break;
    case T_BOX:
    case T_BALL:
        if (pressed && pk.hit) add_shape(s, t->tool == T_BOX ? SH_BOX : SH_BALL, Vector3Add(pk.point, Vector3Scale(pk.normal, t->brush)), t->brush);
        break;
    case T_ERASE:
        if (pressed && pk.shape >= 0) remove_shape(s, pk.shape);
        else if (down && pk.hit) sl_remove_sphere(s->world, sv(pk.point), t->brush);
        break;
    }
}

/* ---------- drawing ---------- */

typedef struct {
    Shader sky, floor, sphere, depth, thick, speck, blur, surface;
    Material sphere_mat, depth_mat, thick_mat, speck_mat;
    Mesh grain, quad;
    Matrix *xf;
    int loc[40];
} renderer;

enum { L_SKY_RES, L_SKY_PS, L_SKY_INV, L_D_VIEW, L_D_PROJ, L_T_VIEW, L_T_PROJ, L_K_VIEW, L_K_PROJ,
       L_B_SRC, L_B_RES, L_B_DIR, L_B_FILTER, L_B_RANGE,
       L_S_BGD, L_S_DEPTH, L_S_THICK, L_S_RES, L_S_PS, L_S_NEAR, L_S_FAR, L_S_INV, L_S_ABS, L_S_DEEP, L_S_REF, L_S_SHINE, L_COUNT };

static Mesh quad_mesh(void) {
    Mesh m = {0};
    float v[] = {-1, -1, 0, 1, -1, 0, 1, 1, 0, -1, 1, 0};
    unsigned short idx[] = {0, 1, 2, 0, 2, 3};
    m.vertexCount = 4;
    m.triangleCount = 2;
    m.vertices = MemAlloc(sizeof v);
    m.indices = MemAlloc(sizeof idx);
    memcpy(m.vertices, v, sizeof v);
    memcpy(m.indices, idx, sizeof idx);
    UploadMesh(&m, false);
    return m;
}

static Material instanced_material(Shader sh) {
    sh.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(sh, "mvp");
    sh.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocationAttrib(sh, "instanceTransform");
    Material m = LoadMaterialDefault();
    m.shader = sh;
    return m;
}

static void renderer_init(renderer *r) {
    r->sky = LoadShaderFromMemory(NULL, SKY_FS);
    r->floor = LoadShaderFromMemory(FLOOR_VS, FLOOR_FS);
    r->sphere = LoadShaderFromMemory(SPHERE_VS, SPHERE_FS);
    r->depth = LoadShaderFromMemory(SPLAT_VS, DEPTH_FS);
    r->thick = LoadShaderFromMemory(SPLAT_VS, THICK_FS);
    r->speck = LoadShaderFromMemory(SPECK_VS, SPECK_FS);
    r->blur = LoadShaderFromMemory(NULL, BLUR_FS);
    r->surface = LoadShaderFromMemory(NULL, SURFACE_FS);
    r->sphere_mat = instanced_material(r->sphere);
    r->depth_mat = instanced_material(r->depth);
    r->thick_mat = instanced_material(r->thick);
    r->speck_mat = instanced_material(r->speck);
    r->grain = GenMeshSphere(1.0f, 4, 6);
    r->quad = quad_mesh();
    r->xf = malloc(sizeof(Matrix) * (size_t)(MAX_PARTICLES * 5 > MAX_DIFFUSE ? MAX_PARTICLES * 5 : MAX_DIFFUSE));
    struct { Shader *sh; const char *name; } locs[L_COUNT] = {
        {&r->sky, "uRes"}, {&r->sky, "uProjScale"}, {&r->sky, "uInvView"}, {&r->depth, "uView"}, {&r->depth, "uProj"},
        {&r->thick, "uView"}, {&r->thick, "uProj"}, {&r->speck, "uView"}, {&r->speck, "uProj"},
        {&r->blur, "uSrc"}, {&r->blur, "uRes"}, {&r->blur, "uDir"}, {&r->blur, "uFilter"}, {&r->blur, "uRange"},
        {&r->surface, "uBgDepth"}, {&r->surface, "uDepth"}, {&r->surface, "uThick"}, {&r->surface, "uRes"},
        {&r->surface, "uProjScale"}, {&r->surface, "uNear"}, {&r->surface, "uFar"}, {&r->surface, "uInvView"},
        {&r->surface, "uAbsorb"}, {&r->surface, "uDeep"}, {&r->surface, "uRefract"}, {&r->surface, "uShine"}};
    for (int i = 0; i < L_COUNT; i++) r->loc[i] = GetShaderLocation(*locs[i].sh, locs[i].name);
}

static Matrix ellipsoid(const sl_vec3 *a) {
    Matrix m = {0};
    m.m0 = a[1].x; m.m1 = a[1].y; m.m2 = a[1].z;
    m.m4 = a[2].x; m.m5 = a[2].y; m.m6 = a[2].z;
    m.m8 = a[3].x; m.m9 = a[3].y; m.m10 = a[3].z;
    m.m12 = a[0].x; m.m13 = a[0].y; m.m14 = a[0].z; m.m15 = 1;
    return m;
}

static int gather_splats(const sim *s, int kind, Matrix *out) {
    const sl_vec3 *p = sl_positions(s->world), *a = sl_anisotropy(s->world);
    const sl_material *m = sl_materials(s->world);
    float r = RADIUS * 1.3f;
    int n = 0;
    for (int i = 0; i < sl_count(s->world); i++) {
        if (m[i] != kind) continue;
        if (kind == s->water && a) {
            sl_vec3 e[4] = {a[4 * i], grow(a[4 * i + 1], 1.3f), grow(a[4 * i + 2], 1.3f), grow(a[4 * i + 3], 1.3f)};
            out[n++] = ellipsoid(e);
        } else {
            sl_vec3 e[4] = {p[i], {r, 0, 0}, {0, r, 0}, {0, 0, r}};
            out[n++] = ellipsoid(e);
        }
    }
    return n;
}

static void draw_floor(renderer *r) {
    BeginShaderMode(r->floor);
    DrawPlane((Vector3){0, 0, 0}, (Vector2){20, 20}, WHITE);
    EndShaderMode();
}

static unsigned hash_id(unsigned x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; }

/* Sand: every simulated particle is shown as a few small grains in a stable mix of sand colors. */
static void draw_sand(renderer *r, const sim *s) {
    static const Color dry[4] = {{232, 205, 152, 255}, {220, 190, 136, 255}, {240, 218, 172, 255}, {208, 178, 126, 255}};
    static const Color wet[4] = {{176, 142, 96, 255}, {164, 132, 88, 255}, {184, 150, 104, 255}, {152, 122, 82, 255}};
    const sl_vec3 *p = sl_positions(s->world);
    const sl_material *m = sl_materials(s->world);
    const sl_particle *ids = sl_ids(s->world);
    const unsigned char *wetness = sl_wetness(s->world);
    for (int bucket = 0; bucket < 8; bucket++) {
        int n = 0;
        for (int i = 0; i < sl_count(s->world); i++) {
            if (m[i] != s->sand) continue;
            unsigned h = hash_id((unsigned)ids[i]);
            int b = (int)(h & 3) + (wetness[i] > 90 ? 4 : 0);
            if (b != bucket) continue;
            for (int g = 0; g < 5; g++) {
                unsigned q = hash_id(h + (unsigned)g * 7919u);
                float ox = ((float)(q & 255) / 255.0f - 0.5f) * RADIUS * 1.5f, oy = ((float)((q >> 8) & 255) / 255.0f - 0.5f) * RADIUS * 1.5f;
                float oz = ((float)((q >> 16) & 255) / 255.0f - 0.5f) * RADIUS * 1.5f, size = RADIUS * (0.38f + 0.2f * (float)(q >> 24) / 255.0f);
                r->xf[n++] = MatrixMultiply(MatrixScale(size, size, size), MatrixTranslate(p[i].x + ox, p[i].y + oy, p[i].z + oz));
            }
        }
        r->sphere_mat.maps[MATERIAL_MAP_DIFFUSE].color = bucket < 4 ? dry[bucket] : wet[bucket - 4];
        if (n) DrawMeshInstanced(r->grain, r->sphere_mat, r->xf, n);
    }
}

static void draw_cloth(const sim *s, sl_object o) {
    int nu, nv;
    sl_object_grid(s->world, o, &nu, &nv);
    const sl_particle *ids;
    sl_object_particles(s->world, o, &ids);
    Vector3 light = Vector3Normalize((Vector3){0.4f, 0.75f, 0.3f});
    for (int j = 0; j + 1 < nv; j++)
        for (int i = 0; i + 1 < nu; i++) {
            Vector3 v[4] = {rv(sl_position(s->world, ids[j * nu + i])), rv(sl_position(s->world, ids[j * nu + i + 1])),
                            rv(sl_position(s->world, ids[(j + 1) * nu + i + 1])), rv(sl_position(s->world, ids[(j + 1) * nu + i]))};
            Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(v[1], v[0]), Vector3Subtract(v[3], v[0])));
            float shade = 0.4f + 0.6f * fabsf(Vector3DotProduct(n, light));
            int stripe = ((i / 4) + (j / 4)) % 2;
            Color c = stripe ? (Color){(unsigned char)(200 * shade), (unsigned char)(70 * shade), (unsigned char)(80 * shade), 255}
                             : (Color){(unsigned char)(240 * shade), (unsigned char)(232 * shade), (unsigned char)(218 * shade), 255};
            DrawTriangle3D(v[0], v[1], v[2], c);
            DrawTriangle3D(v[0], v[2], v[3], c);
            DrawTriangle3D(v[0], v[2], v[1], c);
            DrawTriangle3D(v[0], v[3], v[2], c);
        }
}

static void draw_rope(const sim *s, sl_object o) {
    const sl_particle *ids;
    int n = sl_object_particles(s->world, o, &ids);
    for (int i = 0; i + 1 < n; i++)
        DrawCylinderEx(rv(sl_position(s->world, ids[i])), rv(sl_position(s->world, ids[i + 1])), RADIUS * 0.9f, RADIUS * 0.9f, 6, (Color){150, 110, 70, 255});
}

static void draw_scene(renderer *r, const sim *s) {
    draw_floor(r);
    draw_sand(r, s);
    for (int k = 0; k < s->object_count; k++) {
        if (!sl_object_particles(s->world, s->objects[k], NULL)) continue;
        if (s->kinds[k] == K_CLOTH) draw_cloth(s, s->objects[k]);
        else if (s->kinds[k] == K_ROPE) draw_rope(s, s->objects[k]);
    }
    for (int k = 0; k < s->shape_count; k++) {
        const shape *sh = &s->shapes[k];
        if (sh->shape == SH_BALL) DrawSphereEx(sh->pos, sh->size.x, 16, 24, (Color){205, 95, 95, 255});
        else { DrawCubeV(sh->pos, Vector3Scale(sh->size, 2), (Color){110, 140, 190, 255}); DrawCubeWiresV(sh->pos, Vector3Scale(sh->size, 2), (Color){60, 80, 120, 255}); }
    }
    if (s->container_on) DrawCubeWiresV((Vector3){0, 1.0f, 0}, (Vector3){2.4f, 2.0f, 1.6f}, (Color){90, 100, 120, 255});
}

static int surface_pass(renderer *r, targets *t, const sim *s, int kind, Matrix view, Matrix proj, Camera3D cam) {
    int n = gather_splats(s, kind, r->xf);
    BeginTextureMode(t->depth.rt);
    ClearBackground(BLANK);
    if (n) {
        BeginMode3D(cam);
        SetShaderValueMatrix(r->depth, r->loc[L_D_VIEW], view);
        SetShaderValueMatrix(r->depth, r->loc[L_D_PROJ], proj);
        DrawMeshInstanced(r->quad, r->depth_mat, r->xf, n);
        EndMode3D();
    }
    EndTextureMode();
    if (!n) return 0;

    BeginTextureMode(t->thick.rt);
    ClearBackground(BLANK);
    BeginMode3D(cam);
    rlDisableDepthTest();
    BeginBlendMode(BLEND_ADDITIVE);
    SetShaderValueMatrix(r->thick, r->loc[L_T_VIEW], view);
    SetShaderValueMatrix(r->thick, r->loc[L_T_PROJ], proj);
    DrawMeshInstanced(r->quad, r->thick_mat, r->xf, n);
    EndBlendMode();
    EndMode3D();
    EndTextureMode();

    Vector2 res = {(float)t->depth.w, (float)t->depth.h};
    float filter = 3.0f * RADIUS * proj.m5 * res.y * 0.5f, range = 6.0f * RADIUS;
    target *src[2] = {&t->depth, &t->blur}, *dst[2] = {&t->blur, &t->depth};
    Vector2 dirs[2] = {{1, 0}, {0, 1}};
    for (int pass = 0; pass < 2; pass++) {
        BeginTextureMode(dst[pass]->rt);
        ClearBackground(BLANK);
        BeginShaderMode(r->blur);
        SetShaderValueTexture(r->blur, r->loc[L_B_SRC], src[pass]->rt.texture);
        SetShaderValue(r->blur, r->loc[L_B_RES], &res, SHADER_UNIFORM_VEC2);
        SetShaderValue(r->blur, r->loc[L_B_DIR], &dirs[pass], SHADER_UNIFORM_VEC2);
        SetShaderValue(r->blur, r->loc[L_B_FILTER], &filter, SHADER_UNIFORM_FLOAT);
        SetShaderValue(r->blur, r->loc[L_B_RANGE], &range, SHADER_UNIFORM_FLOAT);
        DrawRectangle(0, 0, t->depth.w, t->depth.h, WHITE);
        EndShaderMode();
        EndTextureMode();
    }
    return 1;
}

static void composite(renderer *r, targets *t, target *bg, Matrix view, Matrix proj, Vector3 absorb, Vector3 deep, float refract, float shine) {
    Vector2 res = {(float)t->depth.w, (float)t->depth.h}, ps = {proj.m0, proj.m5};
    float near = (float)NEAR_PLANE, far = (float)FAR_PLANE;
    Matrix inv = MatrixInvert(view);
    rlEnableDepthTest();
    BeginShaderMode(r->surface);
    SetShaderValueTexture(r->surface, r->loc[L_S_BGD], bg->rt.depth);
    SetShaderValueTexture(r->surface, r->loc[L_S_DEPTH], t->depth.rt.texture);
    SetShaderValueTexture(r->surface, r->loc[L_S_THICK], t->thick.rt.texture);
    SetShaderValue(r->surface, r->loc[L_S_RES], &res, SHADER_UNIFORM_VEC2);
    SetShaderValue(r->surface, r->loc[L_S_PS], &ps, SHADER_UNIFORM_VEC2);
    SetShaderValue(r->surface, r->loc[L_S_NEAR], &near, SHADER_UNIFORM_FLOAT);
    SetShaderValue(r->surface, r->loc[L_S_FAR], &far, SHADER_UNIFORM_FLOAT);
    SetShaderValueMatrix(r->surface, r->loc[L_S_INV], inv);
    SetShaderValue(r->surface, r->loc[L_S_ABS], &absorb, SHADER_UNIFORM_VEC3);
    SetShaderValue(r->surface, r->loc[L_S_DEEP], &deep, SHADER_UNIFORM_VEC3);
    SetShaderValue(r->surface, r->loc[L_S_REF], &refract, SHADER_UNIFORM_FLOAT);
    SetShaderValue(r->surface, r->loc[L_S_SHINE], &shine, SHADER_UNIFORM_FLOAT);
    DrawTextureRec(bg->rt.texture, (Rectangle){0, 0, (float)bg->w, (float)bg->h}, (Vector2){0, 0}, WHITE);
    EndShaderMode();
    rlDisableDepthTest();
}

static void draw_specks(renderer *r, const sim *s, Matrix view, Matrix proj, Camera3D cam) {
    const sl_vec3 *pos;
    const unsigned char *kind;
    const float *life;
    int n = sl_diffuse(s->world, &pos, NULL, &kind, &life), k = 0;
    for (int i = 0; i < n; i++) {
        float fade = fminf(life[i], 1.0f) * (kind[i] == SL_BUBBLE ? 0.25f : (kind[i] == SL_FOAM ? 0.8f : 0.55f));
        Matrix m = MatrixTranslate(pos[i].x, pos[i].y, pos[i].z);
        m.m0 = RADIUS * (kind[i] == SL_FOAM ? 0.55f : 0.35f);
        m.m3 = fade;
        r->xf[k++] = m;
    }
    if (!k) return;
    BeginMode3D(cam);
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ALPHA);
    SetShaderValueMatrix(r->speck, r->loc[L_K_VIEW], view);
    SetShaderValueMatrix(r->speck, r->loc[L_K_PROJ], proj);
    DrawMeshInstanced(r->quad, r->speck_mat, r->xf, k);
    EndBlendMode();
    rlEnableDepthMask();
    EndMode3D();
}

/* ---------- interface ---------- */

static Rectangle tool_rect(int i) { return (Rectangle){16, 120 + 38.0f * (float)i, 150, 34}; }

static int toolbar(Font font, tools *t, sim *s, Vector2 mouse, int click) {
    int used = 0;
    for (int i = 0; i < T_COUNT; i++) {
        Rectangle rc = tool_rect(i);
        int over = CheckCollisionPointRec(mouse, rc);
        if (over && click) { t->tool = i; used = 1; }
        DrawRectangleRounded(rc, 0.3f, 6, t->tool == i ? (Color){60, 110, 170, 230} : (over ? (Color){60, 66, 80, 200} : (Color){30, 34, 44, 170}));
        DrawTextEx(font, TextFormat("%s  %s", TOOL_KEY_NAMES[i], TOOL_NAMES[i]), (Vector2){rc.x + 12, rc.y + 3}, 27, 1, RAYWHITE);
    }
    Rectangle box = tool_rect(T_COUNT);
    box.y += 12;
    int over = CheckCollisionPointRec(mouse, box);
    if (over && click) { set_container(s, !s->container_on); used = 1; }
    DrawRectangleRounded(box, 0.3f, 6, s->container_on ? (Color){60, 140, 100, 230} : (Color){30, 34, 44, 170});
    DrawTextEx(font, TextFormat("K  container %s", s->container_on ? "on" : "off"), (Vector2){box.x + 12, box.y + 3}, 25, 1, RAYWHITE);
    return used || CheckCollisionPointRec(mouse, (Rectangle){16, 120, 150, 38.0f * (T_COUNT + 1) + 12});
}

/* ---------- main ---------- */

typedef struct { int scene, frames; const char *path; } shot;

/* Scripted input for screenshots: builds a little scene with the same tools a player would use. */
static void script(sim *s, tools *t, int frame, Camera3D cam) {
    (void)cam;
    if (s->scene != 0) return;
    if (frame < 70) { t->brush = 0.12f; pour(s, s->water, (Vector3){0.35f, 0.9f, 0.0f}, 0.12f); }
    if (frame >= 40 && frame < 80) pour(s, s->sand, (Vector3){-0.45f, 0.9f, -0.15f}, 0.08f);
    if (frame == 90) spawn_slime(s, (Vector3){-0.1f, 0.9f, 0.35f}, 0.12f);
    if (frame == 95) add_shape(s, SH_BALL, (Vector3){0.75f, 0.18f, -0.35f}, 0.18f);
    if (frame == 100) {
        sl_object r = sl_rope_create(s->world, s->cloth, (sl_vec3){-0.9f, 0.9f, -0.5f}, (sl_vec3){0.2f, 0.9f, -0.6f}, 0);
        pin_index(s, r, 0);
        pin_index(s, r, sl_object_particles(s->world, r, NULL) - 1);
        keep(s, r, K_ROPE);
    }
    if (frame == 150) {
        const sl_particle *ids;
        int n = sl_object_particles(s->world, s->objects[0], &ids);
        if (n) { t->grabbed = ids[n - 1]; t->grabbing = sl_grab_begin(s->world, t->grabbed); t->drag_plane_point = rv(sl_position(s->world, t->grabbed)); }
    }
    if (t->grabbing && frame > 150 && frame < 200) {
        Vector3 to = Vector3Add(t->drag_plane_point, (Vector3){0, 0.006f * (float)(frame - 150), 0});
        sl_grab_move(s->world, t->grabbed, sv(to));
    }
    if (frame == 200 && t->grabbing) { sl_grab_end(s->world, t->grabbed); t->grabbing = 0; }
}

int main(int argc, char **argv) {
    shot sh = {-1, 0, NULL};
    int workers = 4;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--shot") && i + 3 < argc) { sh.scene = atoi(argv[i + 1]); sh.frames = atoi(argv[i + 2]); sh.path = argv[i + 3]; i += 3; }
        else if (!strcmp(argv[i], "--workers") && i + 1 < argc) workers = atoi(argv[++i]);
    }

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "slime");
    SetTargetFPS(60);
    rlSetClipPlanes(NEAR_PLANE, FAR_PLANE);
    SetRandomSeed(7);

    Font font = LoadFontEx(SLIME_DEMO_DIR "/fonts/PatrickHand-Regular.ttf", 64, NULL, 0);
    if (font.texture.id == 0) font = GetFontDefault();
    SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);

    renderer r;
    renderer_init(&r);
    targets tg = {0};
    sim s = {0};
    s.workers = workers;
    scene_load(&s, sh.scene >= 0 ? sh.scene : 0);
    tools t = {T_GRAB, 0.12f, 0};

    Vector3 focus = {0, 0.5f, 0};
    float yaw = 0.55f, pitch = 0.42f, dist = 3.6f;
    int paused = 0, frame = 0;
    double step_ms = 0;

    while (!WindowShouldClose()) {
        float dt = 1.0f / 60.0f;
        Vector2 mouse = GetMousePosition(), md = GetMouseDelta();
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) { yaw -= md.x * 0.005f; pitch = Clamp(pitch + md.y * 0.005f, -0.1f, 1.45f); }
        float wheel = GetMouseWheelMove();
        if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) t.brush = Clamp(t.brush * (1.0f + wheel * 0.1f), 0.04f, 0.5f);
        else dist = Clamp(dist * (1.0f - wheel * 0.08f), 0.8f, 12.0f);

        Camera3D cam = {0};
        cam.target = focus;
        cam.position = Vector3Add(focus, (Vector3){dist * cosf(pitch) * sinf(yaw), dist * sinf(pitch), dist * cosf(pitch) * cosf(yaw)});
        cam.up = (Vector3){0, 1, 0};
        cam.fovy = 45;
        cam.projection = CAMERA_PERSPECTIVE;
        if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
            Vector3 fwd = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
            Vector3 right = Vector3Normalize(Vector3CrossProduct(fwd, cam.up)), up = Vector3CrossProduct(right, fwd);
            focus = Vector3Add(focus, Vector3Add(Vector3Scale(right, -md.x * dist * 0.0012f), Vector3Scale(up, md.y * dist * 0.0012f)));
        }

        for (int k = 0; k < T_COUNT; k++) if (IsKeyPressed(TOOL_KEYS[k])) t.tool = k;
        for (int k = 0; k < 5; k++) if (IsKeyPressed(KEY_F1 + k)) scene_load(&s, k == 4 ? 0 : k + 1);
        if (IsKeyPressed(KEY_X)) scene_load(&s, 0);
        if (IsKeyPressed(KEY_K)) set_container(&s, !s.container_on);
        if (IsKeyPressed(KEY_SPACE)) paused = !paused;
        if (IsKeyPressed(KEY_LEFT_BRACKET)) t.brush = Clamp(t.brush / 1.2f, 0.04f, 0.5f);
        if (IsKeyPressed(KEY_RIGHT_BRACKET)) t.brush = Clamp(t.brush * 1.2f, 0.04f, 0.5f);

        int w = GetRenderWidth(), h = GetRenderHeight();
        BeginDrawing();
        ClearBackground(BLACK);
        int on_ui = 0;
        {
            /* Toolbar hit test happens before the tools, so clicks on it do not reach the world. */
            Rectangle bar = {16, 120, 150, 38.0f * (T_COUNT + 1) + 12};
            on_ui = CheckCollisionPointRec(mouse, bar);
        }
        int pressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !on_ui;
        int down = IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !on_ui;
        int released = IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
        if (sh.path && getenv("SLIME_TOOL_TEST")) {
            /* Walks every tool through press, hold and release at a moving point, to smoke-test the tools. */
            int tool = (frame / 12) % T_COUNT, phase = frame % 12;
            t.tool = tool;
            Vector2 at = {(float)w * (0.4f + 0.2f * (float)phase / 12.0f), (float)h * 0.6f};
            use_tools(&s, &t, cam, GetScreenToWorldRay(at, cam), phase == 0, phase < 10, phase == 10);
        } else if (sh.path) { script(&s, &t, frame, cam); use_tools(&s, &t, cam, GetScreenToWorldRay((Vector2){(float)w * 0.5f, (float)h * 0.6f}, cam), 0, 0, 0); }
        else use_tools(&s, &t, cam, GetScreenToWorldRay(mouse, cam), pressed, down, released);

        if (!paused) {
            double t0 = GetTime();
            sl_step(s.world, dt);
            step_ms = step_ms * 0.9 + (GetTime() - t0) * 1000.0 * 0.1;
        }

        targets_resize(&tg, w, h);
        Matrix view = GetCameraMatrix(cam);
        Matrix proj = MatrixPerspective(cam.fovy * DEG2RAD, (double)w / (double)h, NEAR_PLANE, FAR_PLANE);
        Matrix inv = MatrixInvert(view);
        Vector2 res = {(float)w, (float)h}, ps = {proj.m0, proj.m5};

        BeginTextureMode(tg.scene.rt);
        ClearBackground(BLACK);
        BeginShaderMode(r.sky);
        SetShaderValue(r.sky, r.loc[L_SKY_RES], &res, SHADER_UNIFORM_VEC2);
        SetShaderValue(r.sky, r.loc[L_SKY_PS], &ps, SHADER_UNIFORM_VEC2);
        SetShaderValueMatrix(r.sky, r.loc[L_SKY_INV], inv);
        DrawRectangle(0, 0, w, h, WHITE);
        EndShaderMode();
        BeginMode3D(cam);
        draw_scene(&r, &s);
        if (t.has_point && !sh.path) {
            Vector3 n = t.normal, axis = Vector3CrossProduct((Vector3){0, 0, 1}, n);
            float angle = acosf(Clamp(n.z, -1, 1)) * RAD2DEG;
            if (Vector3Length(axis) < 1e-4f) axis = (Vector3){1, 0, 0};
            DrawCircle3D(Vector3Add(t.point, Vector3Scale(n, 0.005f)), t.brush, Vector3Normalize(axis), angle, (Color){255, 255, 255, 200});
        }
        EndMode3D();
        EndTextureMode();

        target *bg = &tg.scene;
        if (surface_pass(&r, &tg, &s, s.slime, view, proj, cam)) {
            BeginTextureMode(tg.mid.rt);
            ClearBackground(BLANK);
            composite(&r, &tg, &tg.scene, view, proj, (Vector3){60.0f, 14.0f, 70.0f}, (Vector3){0.30f, 0.78f, 0.22f}, 0.02f, 40.0f);
            EndTextureMode();
            bg = &tg.mid;
        }
        if (surface_pass(&r, &tg, &s, s.water, view, proj, cam))
            composite(&r, &tg, bg, view, proj, (Vector3){7.0f, 2.6f, 1.2f}, (Vector3){0.04f, 0.26f, 0.45f}, 0.04f, 200.0f);
        else DrawTextureRec(bg->rt.texture, (Rectangle){0, 0, (float)w, -(float)h}, (Vector2){0, 0}, WHITE);
        draw_specks(&r, &s, view, proj, cam);

        sl_stats st;
        sl_get_stats(s.world, &st);
        DrawTextEx(font, "slime", (Vector2){18, 6}, 56, 1, (Color){30, 40, 60, 255});
        DrawTextEx(font, TextFormat("%s   |   %d particles, %d awake, %d spray   |   %.1f ms per step%s", SCENE_NAMES[s.scene], st.particles, st.awake,
                                    sl_diffuse(s.world, NULL, NULL, NULL, NULL), step_ms, paused ? "   |   paused" : ""),
                   (Vector2){20, 62}, 26, 1, (Color){40, 50, 70, 255});
        toolbar(font, &t, &s, mouse, IsMouseButtonPressed(MOUSE_BUTTON_LEFT));
        DrawTextEx(font, "left use tool    right drag orbit    middle drag pan    wheel zoom    shift+wheel or [ ] brush    space pause    X clear    F1-F4 examples, F5 sandbox",
                   (Vector2){20, (float)h - 34}, 22, 1, (Color){50, 58, 76, 255});
        EndDrawing();

        if (sh.path && ++frame == sh.frames) { TakeScreenshot(sh.path); break; }
    }

    sl_world_destroy(s.world);
    UnloadFont(font);
    CloseWindow();
    return 0;
}
