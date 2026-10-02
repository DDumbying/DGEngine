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

#define MAX_PARTICLES 60000
#define RADIUS 0.035f
#define NEAR_PLANE 0.05
#define FAR_PLANE 100.0

/* ---------- shaders ---------- */

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
    "    float light = max(dot(normalize(normal), normalize(vec3(0.4, 1.0, 0.3))), 0.0) * 0.75 + 0.25;\n"
    "    finalColor = vec4(colDiffuse.rgb * light, 1.0);\n"
    "}\n";

static const char *SPLAT_VS =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in mat4 instanceTransform;\n"
    "uniform mat4 uView;\n"
    "uniform mat4 uProj;\n"
    "uniform float uRadius;\n"
    "out vec2 corner;\n"
    "out vec3 center;\n"
    "void main() {\n"
    "    vec4 c = uView * vec4(instanceTransform[3].xyz, 1.0);\n"
    "    center = c.xyz;\n"
    "    corner = vertexPosition.xy;\n"
    "    gl_Position = uProj * (c + vec4(vertexPosition.xy * uRadius, 0.0, 0.0));\n"
    "}\n";

/* Writes linear eye depth of the sphere surface, and real depth so the nearest sphere wins. */
static const char *DEPTH_FS =
    "#version 330\n"
    "in vec2 corner;\n"
    "in vec3 center;\n"
    "uniform mat4 uProj;\n"
    "uniform float uRadius;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    float r2 = dot(corner, corner);\n"
    "    if (r2 > 1.0) discard;\n"
    "    vec3 p = center + vec3(corner * uRadius, sqrt(1.0 - r2) * uRadius);\n"
    "    vec4 clip = uProj * vec4(p, 1.0);\n"
    "    gl_FragDepth = clip.z / clip.w * 0.5 + 0.5;\n"
    "    finalColor = vec4(-p.z, 0.0, 0.0, 1.0);\n"
    "}\n";

static const char *THICK_FS =
    "#version 330\n"
    "in vec2 corner;\n"
    "in vec3 center;\n"
    "uniform float uRadius;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    float r2 = dot(corner, corner);\n"
    "    if (r2 > 1.0) discard;\n"
    "    finalColor = vec4((1.0 - r2) * (1.0 - r2) * uRadius * 0.4, 0.0, 0.0, 1.0);\n"
    "}\n";

/* Depth-aware blur: smooths the spheres into one surface without bleeding across edges. */
static const char *BLUR_FS =
    "#version 330\n"
    "uniform sampler2D uSrc;\n"
    "uniform vec2 uRes;\n"
    "uniform vec2 uDir;\n"
    "uniform float uFilter;\n"
    "uniform float uFalloff;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    vec2 uv = gl_FragCoord.xy / uRes;\n"
    "    float d = texture(uSrc, uv).r;\n"
    "    if (d <= 0.0) { finalColor = vec4(0.0); return; }\n"
    "    float radius = clamp(uFilter / d, 1.0, 24.0), sigma = radius * 0.5;\n"
    "    float sum = 0.0, wsum = 0.0;\n"
    "    for (int i = -24; i <= 24; i++) {\n"
    "        float fi = float(i);\n"
    "        if (abs(fi) > radius) continue;\n"
    "        float s = texture(uSrc, uv + uDir * fi / uRes).r;\n"
    "        if (s <= 0.0) continue;\n"
    "        float dz = (s - d) / uFalloff;\n"
    "        float w = exp(-fi * fi / (2.0 * sigma * sigma)) * exp(-dz * dz);\n"
    "        sum += s * w;\n"
    "        wsum += w;\n"
    "    }\n"
    "    finalColor = vec4(sum / wsum, 0.0, 0.0, 1.0);\n"
    "}\n";

/* Shades the smoothed surface over what is behind it: refraction, absorption, fresnel and a sky reflection. */
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
    "    float edge = step(0.05, max(max(abs(xr - d), abs(xl - d)), max(abs(yu - d), abs(yd - d))));\n"
    "    vec3 dx = eye_pos(uv + vec2(px.x, 0.0), xr) - p, dx2 = p - eye_pos(uv - vec2(px.x, 0.0), xl);\n"
    "    if (xr <= 0.0 || (xl > 0.0 && abs(dx2.z) < abs(dx.z))) dx = dx2;\n"
    "    vec3 dy = eye_pos(uv + vec2(0.0, px.y), yu) - p, dy2 = p - eye_pos(uv - vec2(0.0, px.y), yd);\n"
    "    if (yu <= 0.0 || (yd > 0.0 && abs(dy2.z) < abs(dy.z))) dy = dy2;\n"
    "    vec3 n = normalize(cross(dx, dy));\n"
    "    if (n.z < 0.0) n = -n;\n"
    "    vec3 v = normalize(-p);\n"
    "    float thick = texture(uThick, uv).r;\n"
    "    vec3 nw = mat3(uInvView) * n, vw = mat3(uInvView) * v, r = reflect(-vw, nw);\n"
    "    vec3 sky = mix(vec3(0.20, 0.22, 0.28), vec3(0.82, 0.88, 1.0), clamp(r.y * 0.7 + 0.45, 0.0, 1.0));\n"
    "    float fresnel = 0.08 + 0.92 * pow(1.0 - max(dot(n, v), 0.0), 5.0);\n"
    "    vec3 behind = texture(texture0, uv + n.xy * uRefract * min(thick * 4.0, 1.0)).rgb;\n"
    "    vec3 body = mix(uDeep, behind, exp(-uAbsorb * thick));\n"
    "    float spec = pow(max(dot(nw, normalize(normalize(vec3(0.4, 1.0, 0.3)) + vw)), 0.0), uShine);\n"
    "    finalColor = vec4(mix(body, sky, fresnel * (1.0 - 0.7 * edge)) + spec * 0.9 * (1.0 - edge), 1.0);\n"
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

typedef struct {
    target scene, mid, depth, blur, thick;
} targets;

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

/* ---------- scenes ---------- */

typedef struct {
    sl_world *world;
    sl_material water, sand, slime, cloth;
    sl_collider box, ball;
    Vector3 ball_pos;
    sl_object objects[32];
    int kinds[32], object_count, scene;
} sim;

enum { K_CLOTH, K_ROPE, K_SLIME };

static void keep(sim *s, sl_object o, int kind) {
    if (o < 0 || s->object_count >= 32) return;
    s->objects[s->object_count] = o;
    s->kinds[s->object_count++] = kind;
}

static const char *SCENE_NAMES[] = {"water and sand", "rope bridge", "curtain", "slime"};

static void spawn_slime(sim *s, Vector3 c, float size) {
    sl_vec3 lo = {c.x - size, c.y - size, c.z - size}, hi = {c.x + size, c.y + size, c.z + size};
    keep(s, sl_softbody_create_box(s->world, s->slime, lo, hi, 0.12f, 0.15f), K_SLIME);
}

static void pour_water(sim *s) {
    for (int i = -2; i <= 2; i++)
        for (int k = -2; k <= 2; k++)
            sl_spawn(s->world, s->water, (sl_vec3){0.6f + i * 2.1f * RADIUS, 1.85f, k * 2.1f * RADIUS}, (sl_vec3){-1.2f, -1.0f, 0});
}

static void drop_sand(sim *s) {
    sl_spawn_box(s->world, s->sand, (sl_vec3){-0.25f, 1.45f, -0.25f}, (sl_vec3){0.25f, 1.9f, 0.25f});
}

static void pin_corner(sim *s, sl_object o, int index) {
    const sl_particle *ids;
    int n = sl_object_particles(s->world, o, &ids);
    if (index < n) sl_pin(s->world, ids[index], 1);
}

static void scene_load(sim *s, int scene, int workers) {
    if (s->world) sl_world_destroy(s->world);
    memset(s, 0, sizeof *s);
    s->scene = scene;
    sl_world_desc desc = {0};
    desc.max_particles = MAX_PARTICLES;
    desc.particle_radius = RADIUS;
    desc.gravity = (sl_vec3){0, -9.81f, 0};
    desc.workers = workers;
    s->world = sl_world_create(&desc);

    s->water = sl_material_add(s->world, &(sl_material_desc){.kind = SL_FLUID, .density = 1000, .viscosity = 0.02f, .vorticity = 0.05f});
    s->sand = sl_material_add(s->world, &(sl_material_desc){.kind = SL_GRANULAR, .density = 1600, .friction = 0.8f});
    s->slime = sl_material_add(s->world, &(sl_material_desc){.kind = SL_SOLID, .density = 900, .friction = 0.6f, .damping = 0.5f});
    s->cloth = sl_material_add(s->world, &(sl_material_desc){.kind = SL_SOLID, .density = 400, .friction = 0.5f, .damping = 0.8f});

    s->box = sl_collider_add(s->world, &(sl_collider_desc){.shape = SL_BOX, .position = {0, 1.0f, 0},
                                                          .half_extents = {1.2f, 1.0f, 0.8f}, .inside = 1, .friction = 0.4f});
    s->ball_pos = (Vector3){-0.7f, 0.45f, 0};
    s->ball = sl_collider_add(s->world, &(sl_collider_desc){.shape = SL_SPHERE, .radius = 0.22f,
                                                           .position = {s->ball_pos.x, s->ball_pos.y, s->ball_pos.z}, .friction = 0.3f});
    sl_world *w = s->world;
    if (scene == 0) {
        sl_spawn_box(w, s->water, (sl_vec3){0.2f, 0.0f, -0.8f}, (sl_vec3){1.2f, 0.7f, 0.8f});
        drop_sand(s);
    } else if (scene == 1) {
        sl_object deck = sl_cloth_create(w, s->cloth, (sl_vec3){-1.15f, 1.0f, -0.25f}, (sl_vec3){2.3f, 0, 0}, (sl_vec3){0, 0, 0.5f}, 0, 0.02f);
        int nu, nv;
        sl_object_grid(w, deck, &nu, &nv);
        pin_corner(s, deck, 0);
        pin_corner(s, deck, nu - 1);
        pin_corner(s, deck, nu * (nv - 1));
        pin_corner(s, deck, nu * nv - 1);
        keep(s, deck, K_CLOTH);
        for (int side = -1; side <= 1; side += 2) {
            sl_object rail = sl_rope_create(w, s->cloth, (sl_vec3){-1.15f, 1.35f, side * 0.3f}, (sl_vec3){1.15f, 1.35f, side * 0.3f}, 0);
            const sl_particle *ids;
            int n = sl_object_particles(w, rail, &ids);
            sl_pin(w, ids[0], 1);
            sl_pin(w, ids[n - 1], 1);
            keep(s, rail, K_ROPE);
        }
        sl_object swing = sl_rope_create(w, s->cloth, (sl_vec3){0.6f, 1.95f, 0.55f}, (sl_vec3){0.6f, 1.2f, 0.55f}, 0);
        pin_corner(s, swing, 0);
        keep(s, swing, K_ROPE);
        spawn_slime(s, (Vector3){-0.4f, 1.6f, 0}, 0.12f);
        sl_spawn_box(w, s->water, (sl_vec3){-1.2f, 0.0f, -0.8f}, (sl_vec3){1.2f, 0.25f, 0.8f});
    } else if (scene == 2) {
        sl_object curtain = sl_cloth_create(w, s->cloth, (sl_vec3){-0.7f, 1.85f, 0.1f}, (sl_vec3){1.4f, 0, 0}, (sl_vec3){0, -1.1f, 0}, 0, 0.002f);
        int nu, nv;
        sl_object_grid(w, curtain, &nu, &nv);
        for (int i = 0; i < nu; i += 4) pin_corner(s, curtain, i);
        pin_corner(s, curtain, nu - 1);
        keep(s, curtain, K_CLOTH);
        s->ball_pos = (Vector3){0, 0.8f, -0.5f};
    } else {
        sl_spawn_box(w, s->water, (sl_vec3){-1.2f, 0.0f, -0.8f}, (sl_vec3){1.2f, 0.3f, 0.8f});
        spawn_slime(s, (Vector3){-0.6f, 1.3f, 0.1f}, 0.14f);
        spawn_slime(s, (Vector3){0.1f, 1.7f, -0.2f}, 0.12f);
        spawn_slime(s, (Vector3){0.7f, 1.4f, 0.2f}, 0.16f);
    }
}

/* ---------- drawing ---------- */

typedef struct {
    Shader sphere, depth, thick, blur, surface;
    Material sphere_mat, depth_mat, thick_mat;
    Mesh ball_mesh, quad;
    Matrix *xf;
    int loc[32];
} renderer;

enum { L_D_VIEW, L_D_PROJ, L_D_RAD, L_T_VIEW, L_T_PROJ, L_T_RAD, L_B_SRC, L_B_RES, L_B_DIR, L_B_FILTER, L_B_FALL,
       L_S_BGD, L_S_DEPTH, L_S_THICK, L_S_RES, L_S_PS, L_S_NEAR, L_S_FAR, L_S_INV, L_S_ABS, L_S_DEEP, L_S_REF, L_S_SHINE };

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
    r->sphere = LoadShaderFromMemory(SPHERE_VS, SPHERE_FS);
    r->depth = LoadShaderFromMemory(SPLAT_VS, DEPTH_FS);
    r->thick = LoadShaderFromMemory(SPLAT_VS, THICK_FS);
    r->blur = LoadShaderFromMemory(NULL, BLUR_FS);
    r->surface = LoadShaderFromMemory(NULL, SURFACE_FS);
    r->sphere_mat = instanced_material(r->sphere);
    r->depth_mat = instanced_material(r->depth);
    r->thick_mat = instanced_material(r->thick);
    r->ball_mesh = GenMeshSphere(RADIUS, 5, 7);
    r->quad = quad_mesh();
    r->xf = malloc(sizeof(Matrix) * MAX_PARTICLES);
    const char *names[] = {"uView", "uProj", "uRadius", "uView", "uProj", "uRadius", "uSrc", "uRes", "uDir", "uFilter", "uFalloff",
                           "uBgDepth", "uDepth", "uThick", "uRes", "uProjScale", "uNear", "uFar", "uInvView", "uAbsorb", "uDeep", "uRefract", "uShine"};
    Shader owners[] = {r->depth, r->depth, r->depth, r->thick, r->thick, r->thick, r->blur, r->blur, r->blur, r->blur, r->blur,
                       r->surface, r->surface, r->surface, r->surface, r->surface, r->surface, r->surface, r->surface, r->surface, r->surface, r->surface, r->surface};
    for (int i = 0; i <= L_S_SHINE; i++) r->loc[i] = GetShaderLocation(owners[i], names[i]);
}

static int gather(const sim *s, int kind, Matrix *out) {
    const sl_vec3 *p = sl_positions(s->world);
    const sl_material *m = sl_materials(s->world);
    int n = 0;
    for (int i = 0; i < sl_count(s->world); i++)
        if (m[i] == kind) out[n++] = MatrixTranslate(p[i].x, p[i].y, p[i].z);
    return n;
}

static void draw_cloth(const sim *s, sl_object o) {
    int nu, nv;
    sl_object_grid(s->world, o, &nu, &nv);
    const sl_particle *ids;
    sl_object_particles(s->world, o, &ids);
    Vector3 light = Vector3Normalize((Vector3){0.4f, 1.0f, 0.3f});
    for (int j = 0; j + 1 < nv; j++)
        for (int i = 0; i + 1 < nu; i++) {
            sl_vec3 q[4] = {sl_position(s->world, ids[j * nu + i]), sl_position(s->world, ids[j * nu + i + 1]),
                            sl_position(s->world, ids[(j + 1) * nu + i + 1]), sl_position(s->world, ids[(j + 1) * nu + i])};
            Vector3 v[4];
            for (int k = 0; k < 4; k++) v[k] = (Vector3){q[k].x, q[k].y, q[k].z};
            Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(v[1], v[0]), Vector3Subtract(v[3], v[0])));
            float shade = 0.35f + 0.65f * fabsf(Vector3DotProduct(n, light));
            int stripe = ((i / 3) + (j / 3)) % 2;
            Color c = stripe ? (Color){(unsigned char)(200 * shade), (unsigned char)(70 * shade), (unsigned char)(80 * shade), 255}
                             : (Color){(unsigned char)(235 * shade), (unsigned char)(225 * shade), (unsigned char)(210 * shade), 255};
            DrawTriangle3D(v[0], v[1], v[2], c);
            DrawTriangle3D(v[0], v[2], v[3], c);
            DrawTriangle3D(v[0], v[2], v[1], c);
            DrawTriangle3D(v[0], v[3], v[2], c);
        }
}

static void draw_rope(const sim *s, sl_object o) {
    const sl_particle *ids;
    int n = sl_object_particles(s->world, o, &ids);
    for (int i = 0; i + 1 < n; i++) {
        sl_vec3 a = sl_position(s->world, ids[i]), b = sl_position(s->world, ids[i + 1]);
        DrawCylinderEx((Vector3){a.x, a.y, a.z}, (Vector3){b.x, b.y, b.z}, RADIUS * 0.8f, RADIUS * 0.8f, 6, (Color){150, 110, 70, 255});
    }
}

/* Smoothed surface of one material: sphere depth, thickness, then a two-pass blur. */
static int surface_pass(renderer *r, targets *t, const sim *s, int kind, Matrix view, Matrix proj, Camera3D cam) {
    int n = gather(s, kind, r->xf);
    float radius = RADIUS * 1.25f;
    BeginTextureMode(t->depth.rt);
    ClearBackground(BLANK);
    if (n) {
        BeginMode3D(cam);
        SetShaderValueMatrix(r->depth, r->loc[L_D_VIEW], view);
        SetShaderValueMatrix(r->depth, r->loc[L_D_PROJ], proj);
        SetShaderValue(r->depth, r->loc[L_D_RAD], &radius, SHADER_UNIFORM_FLOAT);
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
    float wide = radius * 2.0f;
    SetShaderValue(r->thick, r->loc[L_T_RAD], &wide, SHADER_UNIFORM_FLOAT);
    DrawMeshInstanced(r->quad, r->thick_mat, r->xf, n);
    EndBlendMode();
    EndMode3D();
    EndTextureMode();

    Vector2 res = {(float)t->depth.w, (float)t->depth.h};
    float filter = 2.5f * RADIUS * proj.m5 * res.y * 0.5f, falloff = 4.0f * RADIUS;
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
        SetShaderValue(r->blur, r->loc[L_B_FALL], &falloff, SHADER_UNIFORM_FLOAT);
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

static void draw_floor(void) {
    Color c = {52, 56, 68, 255};
    for (int i = -8; i <= 8; i++) {
        DrawLine3D((Vector3){i * 0.25f, 0, -2}, (Vector3){i * 0.25f, 0, 2}, c);
        DrawLine3D((Vector3){-2, 0, i * 0.25f}, (Vector3){2, 0, i * 0.25f}, c);
    }
}

static void draw_scene(renderer *r, const sim *s) {
    draw_floor();
    int n = gather(s, s->sand, r->xf);
    r->sphere_mat.maps[MATERIAL_MAP_DIFFUSE].color = (Color){222, 184, 105, 255};
    if (n) DrawMeshInstanced(r->ball_mesh, r->sphere_mat, r->xf, n);
    for (int k = 0; k < s->object_count; k++) {
        if (s->kinds[k] == K_CLOTH) draw_cloth(s, s->objects[k]);
        else if (s->kinds[k] == K_ROPE) draw_rope(s, s->objects[k]);
    }
    DrawSphere(s->ball_pos, 0.22f, (Color){205, 95, 95, 255});
    DrawCubeWiresV((Vector3){0, 1.0f, 0}, (Vector3){2.4f, 2.0f, 1.6f}, (Color){170, 180, 200, 255});
}

/* ---------- main ---------- */

int main(int argc, char **argv) {
    int shot_scene = -1, shot_frames = 0, workers = 4;
    const char *shot_path = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--shot") && i + 3 < argc) { shot_scene = atoi(argv[i + 1]) - 1; shot_frames = atoi(argv[i + 2]); shot_path = argv[i + 3]; i += 3; }
        else if (!strcmp(argv[i], "--workers") && i + 1 < argc) workers = atoi(argv[++i]);
    }

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "slime");
    SetTargetFPS(60);
    rlSetClipPlanes(NEAR_PLANE, FAR_PLANE);

    Font font = LoadFontEx(SLIME_DEMO_DIR "/fonts/PatrickHand-Regular.ttf", 64, NULL, 0);
    if (font.texture.id == 0) font = GetFontDefault();
    SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);

    renderer r;
    renderer_init(&r);
    targets t = {0};
    sim s = {0};
    scene_load(&s, shot_scene >= 0 ? shot_scene : 0, workers);

    float yaw = 0.55f, pitch = 0.42f, dist = 4.3f;
    int paused = 0, frame = 0;
    double step_ms = 0;

    while (!WindowShouldClose()) {
        float dt = 1.0f / 60.0f;
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
            Vector2 d = GetMouseDelta();
            yaw -= d.x * 0.005f;
            pitch = Clamp(pitch + d.y * 0.005f, -0.2f, 1.45f);
        }
        dist = Clamp(dist - GetMouseWheelMove() * 0.3f, 1.5f, 10.0f);
        for (int k = 0; k < 4; k++) if (IsKeyPressed(KEY_ONE + k)) scene_load(&s, k, workers);
        if (IsKeyDown(KEY_W)) pour_water(&s);
        if (IsKeyPressed(KEY_S)) drop_sand(&s);
        if (IsKeyPressed(KEY_G)) spawn_slime(&s, (Vector3){0, 1.6f, 0}, 0.12f);
        if (IsKeyPressed(KEY_SPACE)) paused = !paused;
        if (IsKeyPressed(KEY_R)) scene_load(&s, s.scene, workers);

        float speed = 1.2f * dt;
        if (IsKeyDown(KEY_LEFT)) s.ball_pos.x -= speed;
        if (IsKeyDown(KEY_RIGHT)) s.ball_pos.x += speed;
        if (IsKeyDown(KEY_UP)) s.ball_pos.z -= speed;
        if (IsKeyDown(KEY_DOWN)) s.ball_pos.z += speed;
        if (IsKeyDown(KEY_PAGE_UP)) s.ball_pos.y += speed;
        if (IsKeyDown(KEY_PAGE_DOWN)) s.ball_pos.y -= speed;
        if (shot_path && s.scene == 0) s.ball_pos.x = -0.6f + 0.8f * sinf((float)frame * 0.03f);
        if (shot_path && s.scene == 2) {
            s.ball_pos = (Vector3){0.1f, 1.1f, -0.5f + (float)frame * 0.006f};
            if (frame < 90) pour_water(&s);
        }
        s.ball_pos = Vector3Clamp(s.ball_pos, (Vector3){-0.95f, 0.22f, -0.55f}, (Vector3){0.95f, 1.75f, 0.55f});
        sl_collider_move(s.world, s.ball, (sl_vec3){s.ball_pos.x, s.ball_pos.y, s.ball_pos.z}, NULL);

        if (!paused) {
            double t0 = GetTime();
            sl_step(s.world, dt);
            step_ms = step_ms * 0.9 + (GetTime() - t0) * 1000.0 * 0.1;
        }

        int w = GetRenderWidth(), h = GetRenderHeight();
        targets_resize(&t, w, h);
        Camera3D cam = {0};
        cam.target = (Vector3){0, 0.8f, 0};
        cam.position = Vector3Add(cam.target, (Vector3){dist * cosf(pitch) * sinf(yaw), dist * sinf(pitch), dist * cosf(pitch) * cosf(yaw)});
        cam.up = (Vector3){0, 1, 0};
        cam.fovy = 45;
        cam.projection = CAMERA_PERSPECTIVE;
        Matrix view = GetCameraMatrix(cam);
        Matrix proj = MatrixPerspective(cam.fovy * DEG2RAD, (double)w / (double)h, NEAR_PLANE, FAR_PLANE);

        BeginTextureMode(t.scene.rt);
        ClearBackground((Color){30, 32, 40, 255});
        BeginMode3D(cam);
        draw_scene(&r, &s);
        EndMode3D();
        EndTextureMode();

        /* Slime first into mid, then water over it, so slime under water still shows through. */
        target *bg = &t.scene;
        if (surface_pass(&r, &t, &s, s.slime, view, proj, cam)) {
            BeginTextureMode(t.mid.rt);
            ClearBackground(BLANK);
            composite(&r, &t, &t.scene, view, proj, (Vector3){40.0f, 9.0f, 45.0f}, (Vector3){0.30f, 0.78f, 0.22f}, 0.02f, 30.0f);
            EndTextureMode();
            bg = &t.mid;
        }
        int water = surface_pass(&r, &t, &s, s.water, view, proj, cam);

        BeginDrawing();
        ClearBackground(BLACK);
        if (water) composite(&r, &t, bg, view, proj, (Vector3){11.0f, 4.5f, 2.2f}, (Vector3){0.05f, 0.30f, 0.55f}, 0.05f, 120.0f);
        else DrawTextureRec(bg->rt.texture, (Rectangle){0, 0, (float)w, -(float)h}, (Vector2){0, 0}, WHITE);

        sl_stats st;
        sl_get_stats(s.world, &st);
        DrawTextEx(font, "slime", (Vector2){18, 8}, 56, 1, RAYWHITE);
        DrawTextEx(font, TextFormat("%s   |   %d particles, %d awake   |   %.1f ms per step%s", SCENE_NAMES[s.scene], st.particles, st.awake,
                                    step_ms, paused ? "   |   paused" : ""), (Vector2){20, 62}, 26, 1, (Color){210, 215, 225, 255});
        DrawTextEx(font, "1-4 scenes    W pour water    S drop sand    G drop slime    arrows, PgUp, PgDn move the ball    right drag orbit    wheel zoom    space pause    R reset",
                   (Vector2){20, (float)h - 34}, 22, 1, (Color){150, 156, 170, 255});
        EndDrawing();

        if (shot_path && ++frame == shot_frames) {
            TakeScreenshot(shot_path);
            break;
        }
    }

    sl_world_destroy(s.world);
    UnloadFont(font);
    CloseWindow();
    return 0;
}
