#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "raylib.h"
#include "raymath.h"
#include "slime/slime.h"

#define MAX_PARTICLES 30000
#define RADIUS 0.04f

static const char *VS =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in vec3 vertexNormal;\n"
    "in mat4 instanceTransform;\n"
    "uniform mat4 mvp;\n"
    "out vec3 normal;\n"
    "void main() {\n"
    "    normal = mat3(instanceTransform) * vertexNormal;\n"
    "    gl_Position = mvp * instanceTransform * vec4(vertexPosition, 1.0);\n"
    "}\n";

static const char *FS =
    "#version 330\n"
    "in vec3 normal;\n"
    "uniform vec4 colDiffuse;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    float light = max(dot(normalize(normal), normalize(vec3(0.4, 1.0, 0.3))), 0.0) * 0.7 + 0.3;\n"
    "    finalColor = vec4(colDiffuse.rgb * light, colDiffuse.a);\n"
    "}\n";

static const Vector3 BOX_CENTER = {0, 1.0f, 0};
static const Vector3 BOX_HALF = {1.2f, 1.0f, 0.8f};

typedef struct {
    sl_world *world;
    sl_material water, sand;
    sl_collider ball;
    Vector3 ball_pos;
} scene;

static void scene_reset(scene *s) {
    if (s->world) sl_world_destroy(s->world);
    sl_world_desc desc = {0};
    desc.max_particles = MAX_PARTICLES;
    desc.particle_radius = RADIUS;
    desc.gravity = (sl_vec3){0, -9.81f, 0};
    s->world = sl_world_create(&desc);

    sl_material_desc water = {SL_FLUID, 1000, 0.02f, 0, 0};
    sl_material_desc sand = {SL_GRANULAR, 1600, 0, 0, 0.8f};
    s->water = sl_material_add(s->world, &water);
    s->sand = sl_material_add(s->world, &sand);

    sl_collider_desc box = {0};
    box.shape = SL_BOX;
    box.position = (sl_vec3){BOX_CENTER.x, BOX_CENTER.y, BOX_CENTER.z};
    box.half_extents = (sl_vec3){BOX_HALF.x, BOX_HALF.y, BOX_HALF.z};
    box.inside = 1;
    box.friction = 0.4f;
    sl_collider_add(s->world, &box);

    s->ball_pos = (Vector3){-0.6f, 0.5f, 0};
    sl_collider_desc ball = {0};
    ball.shape = SL_SPHERE;
    ball.radius = 0.25f;
    ball.position = (sl_vec3){s->ball_pos.x, s->ball_pos.y, s->ball_pos.z};
    ball.friction = 0.3f;
    s->ball = sl_collider_add(s->world, &ball);
}

static void pour_water(scene *s) {
    for (int i = -2; i <= 2; i++)
        for (int k = -2; k <= 2; k++)
            sl_spawn(s->world, s->water, (sl_vec3){0.7f + i * 2.1f * RADIUS, 1.8f, k * 2.1f * RADIUS}, (sl_vec3){-1.5f, -1.0f, 0});
}

static void drop_sand(scene *s) {
    sl_spawn_box(s->world, s->sand, (sl_vec3){-0.3f, 1.2f, -0.3f}, (sl_vec3){0.3f, 1.9f, 0.3f});
}

static void draw_particles(const scene *s, Mesh mesh, Material mat, Matrix *buf) {
    const sl_vec3 *p = sl_positions(s->world);
    const sl_material *m = sl_materials(s->world);
    sl_material kinds[2] = {s->water, s->sand};
    Color colors[2] = {{40, 120, 230, 255}, {220, 180, 90, 255}};
    for (int k = 0; k < 2; k++) {
        int n = 0;
        for (int i = 0; i < sl_count(s->world); i++)
            if (m[i] == kinds[k]) buf[n++] = MatrixTranslate(p[i].x, p[i].y, p[i].z);
        mat.maps[MATERIAL_MAP_DIFFUSE].color = colors[k];
        if (n) DrawMeshInstanced(mesh, mat, buf, n);
    }
}

int main(int argc, char **argv) {
    const char *shot = NULL;
    int shot_frames = 0;
    if (argc == 4 && strcmp(argv[1], "--shot") == 0) { shot = argv[2]; shot_frames = atoi(argv[3]); }

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "slime");
    SetTargetFPS(60);

    Shader shader = LoadShaderFromMemory(VS, FS);
    shader.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(shader, "mvp");
    shader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocationAttrib(shader, "instanceTransform");
    Material mat = LoadMaterialDefault();
    mat.shader = shader;
    Mesh mesh = GenMeshSphere(RADIUS, 6, 8);
    Matrix *buf = malloc(sizeof(Matrix) * MAX_PARTICLES);

    float yaw = 0.6f, pitch = 0.45f, dist = 4.2f;
    scene s = {0};
    scene_reset(&s);
    int paused = 0, frame = 0;
    double step_ms = 0;

    if (shot) {
        drop_sand(&s);
        sl_spawn_box(s.world, s.water, (sl_vec3){0.3f, 0.0f, -0.8f}, (sl_vec3){1.2f, 0.5f, 0.8f});
    }

    while (!WindowShouldClose()) {
        float dt = 1.0f / 60.0f;
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
            Vector2 d = GetMouseDelta();
            yaw -= d.x * 0.005f;
            pitch = Clamp(pitch + d.y * 0.005f, -0.2f, 1.4f);
        }
        dist = Clamp(dist - GetMouseWheelMove() * 0.3f, 1.5f, 10.0f);

        if (IsKeyDown(KEY_ONE)) pour_water(&s);
        if (IsKeyPressed(KEY_TWO)) drop_sand(&s);
        if (IsKeyPressed(KEY_SPACE)) paused = !paused;
        if (IsKeyPressed(KEY_R)) scene_reset(&s);
        if (IsKeyPressed(KEY_C)) sl_clear(s.world);

        float speed = 1.2f * dt;
        if (IsKeyDown(KEY_LEFT)) s.ball_pos.x -= speed;
        if (IsKeyDown(KEY_RIGHT)) s.ball_pos.x += speed;
        if (IsKeyDown(KEY_UP)) s.ball_pos.z -= speed;
        if (IsKeyDown(KEY_DOWN)) s.ball_pos.z += speed;
        if (IsKeyDown(KEY_PAGE_UP)) s.ball_pos.y += speed;
        if (IsKeyDown(KEY_PAGE_DOWN)) s.ball_pos.y -= speed;
        if (shot) s.ball_pos.x = -0.6f + 0.9f * sinf((float)frame * 0.03f);
        s.ball_pos = Vector3Clamp(s.ball_pos, (Vector3){-0.9f, 0.2f, -0.5f}, (Vector3){0.9f, 1.7f, 0.5f});
        sl_collider_move(s.world, s.ball, (sl_vec3){s.ball_pos.x, s.ball_pos.y, s.ball_pos.z}, NULL);

        if (!paused) {
            double t0 = GetTime();
            sl_step(s.world, dt);
            step_ms = step_ms * 0.9 + (GetTime() - t0) * 1000.0 * 0.1;
        }

        Camera3D cam = {0};
        cam.target = BOX_CENTER;
        cam.position = Vector3Add(BOX_CENTER, (Vector3){dist * cosf(pitch) * sinf(yaw), dist * sinf(pitch), dist * cosf(pitch) * cosf(yaw)});
        cam.up = (Vector3){0, 1, 0};
        cam.fovy = 45;
        cam.projection = CAMERA_PERSPECTIVE;

        BeginDrawing();
        ClearBackground((Color){24, 26, 32, 255});
        BeginMode3D(cam);
        DrawGrid(12, 0.25f);
        draw_particles(&s, mesh, mat, buf);
        DrawSphere(s.ball_pos, 0.25f, (Color){200, 90, 90, 255});
        DrawCubeWiresV(BOX_CENTER, Vector3Scale(BOX_HALF, 2), (Color){180, 190, 210, 255});
        EndMode3D();

        DrawText(TextFormat("%d particles   %.2f ms/step%s", sl_count(s.world), step_ms, paused ? "   PAUSED" : ""), 16, 14, 20, RAYWHITE);
        DrawText("1 pour water   2 drop sand   arrows / PgUp / PgDn move ball   right drag orbit   wheel zoom   space pause   R reset   C clear",
                 16, GetScreenHeight() - 30, 16, GRAY);
        EndDrawing();

        if (shot && ++frame == shot_frames) {
            TakeScreenshot(shot);
            break;
        }
    }

    sl_world_destroy(s.world);
    free(buf);
    UnloadMesh(mesh);
    UnloadShader(shader);
    CloseWindow();
    return 0;
}
