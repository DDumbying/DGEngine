#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "slime/slime.h"

static int failures, checks;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; \
    printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const float DT = 1.0f / 60.0f;
static const float R = 0.05f;

static sl_world *make_world(int max) {
    sl_world_desc d = {0};
    d.max_particles = max;
    d.particle_radius = R;
    d.gravity = (sl_vec3){0, -9.81f, 0};
    return sl_world_create(&d);
}

static sl_material water(sl_world *w) {
    sl_material_desc m = {SL_FLUID, 1000, 0.01f, 0, 0};
    return sl_material_add(w, &m);
}

static sl_material sand(sl_world *w) {
    sl_material_desc m = {SL_GRANULAR, 1600, 0, 0, 0.9f};
    return sl_material_add(w, &m);
}

static void add_floor(sl_world *w, float friction) {
    sl_collider_desc c = {0};
    c.shape = SL_PLANE;
    c.normal = (sl_vec3){0, 1, 0};
    c.friction = friction;
    sl_collider_add(w, &c);
}

static sl_collider add_container(sl_world *w, sl_vec3 half) {
    sl_collider_desc c = {0};
    c.shape = SL_BOX;
    c.half_extents = half;
    c.inside = 1;
    c.friction = 0.3f;
    return sl_collider_add(w, &c);
}

static int all_finite(const sl_world *w) {
    const sl_vec3 *p = sl_positions(w);
    for (int i = 0; i < sl_count(w); i++)
        if (!isfinite(p[i].x) || !isfinite(p[i].y) || !isfinite(p[i].z)) return 0;
    return 1;
}

static float max_speed(const sl_world *w) {
    const sl_vec3 *v = sl_velocities(w);
    float m = 0;
    for (int i = 0; i < sl_count(w); i++) {
        float s = sqrtf(v[i].x * v[i].x + v[i].y * v[i].y + v[i].z * v[i].z);
        if (s > m) m = s;
    }
    return m;
}

static float max_height(const sl_world *w) {
    const sl_vec3 *p = sl_positions(w);
    float m = -1e9f;
    for (int i = 0; i < sl_count(w); i++) if (p[i].y > m) m = p[i].y;
    return m;
}

static void test_free_fall(void) {
    sl_world *w = make_world(4);
    sl_spawn(w, water(w), (sl_vec3){0, 10, 0}, (sl_vec3){0, 0, 0});
    for (int i = 0; i < 60; i++) sl_step(w, DT);
    float y = sl_positions(w)[0].y, expect = 10.0f - 0.5f * 9.81f;
    CHECK(fabsf(y - expect) < 0.05f, "free fall y=%f expected %f", y, expect);
    sl_world_destroy(w);
}

static void test_settle_on_floor(void) {
    sl_world *w = make_world(2000);
    add_floor(w, 0.5f);
    sl_spawn_box(w, sand(w), (sl_vec3){-0.25f, 0.2f, -0.25f}, (sl_vec3){0.25f, 0.7f, 0.25f});
    for (int i = 0; i < 300; i++) sl_step(w, DT);
    const sl_vec3 *p = sl_positions(w);
    float lowest = 1e9f;
    for (int i = 0; i < sl_count(w); i++) if (p[i].y < lowest) lowest = p[i].y;
    CHECK(lowest > R * 0.9f, "particle sank into floor: y=%f", lowest);
    CHECK(max_speed(w) < 0.05f, "pile did not settle: max speed %f", max_speed(w));
    CHECK(all_finite(w), "non-finite positions");
    sl_world_destroy(w);
}

static void test_container_holds(void) {
    sl_world *w = make_world(3000);
    sl_material m = water(w);
    add_container(w, (sl_vec3){0.6f, 0.6f, 0.6f});
    unsigned seed = 1;
    for (int i = 0; i < 2000; i++) {
        seed = seed * 1664525u + 1013904223u;
        float a = (float)(seed >> 8) / 16777216.0f * 6.2831853f;
        sl_vec3 pos = {(float)(i % 12) * 0.08f - 0.45f, (float)(i / 144) * 0.08f - 0.5f, (float)(i / 12 % 12) * 0.08f - 0.45f};
        sl_spawn(w, m, pos, (sl_vec3){cosf(a) * 6, 4, sinf(a) * 6});
    }
    int escaped = 0;
    for (int s = 0; s < 2000 && !escaped; s++) {
        sl_step(w, DT);
        const sl_vec3 *p = sl_positions(w);
        for (int i = 0; i < sl_count(w); i++)
            if (fabsf(p[i].x) > 0.6f || fabsf(p[i].y) > 0.6f || fabsf(p[i].z) > 0.6f) { escaped = 1; break; }
    }
    CHECK(!escaped, "particle escaped the container");
    CHECK(all_finite(w), "non-finite positions");
    sl_world_destroy(w);
}

static void test_fluid_levels_out(void) {
    sl_world *w = make_world(5000);
    add_container(w, (sl_vec3){0.5f, 1.0f, 0.5f});
    int n = sl_spawn_box(w, water(w), (sl_vec3){-0.5f, -1.0f, -0.5f}, (sl_vec3){0.0f, 0.2f, 0.5f});
    for (int i = 0; i < 600; i++) sl_step(w, DT);

    const sl_vec3 *p = sl_positions(w);
    double mean_y = 0, left = 0;
    for (int i = 0; i < n; i++) { mean_y += p[i].y + 1.0f; left += p[i].x < 0; }
    mean_y /= n;
    float d = 2 * R, expect_h = (float)n * d * d * d / 1.0f;
    float h = (float)(2.0 * mean_y);
    CHECK(fabsf(h / expect_h - 1.0f) < 0.08f, "fluid height %f vs rest height %f", h, expect_h);
    CHECK(fabs(left / n - 0.5) < 0.1, "fluid did not spread: %.2f on the left", left / n);
    CHECK(max_speed(w) < 0.3f, "fluid still moving: %f", max_speed(w));
    sl_world_destroy(w);
}

static float drop_block(int granular) {
    sl_world *w = make_world(3000);
    add_floor(w, 0.6f);
    sl_material m = granular ? sand(w) : water(w);
    sl_spawn_box(w, m, (sl_vec3){-0.3f, 0.0f, -0.3f}, (sl_vec3){0.3f, 0.6f, 0.3f});
    for (int i = 0; i < 400; i++) sl_step(w, DT);
    float hgt = max_height(w);
    sl_world_destroy(w);
    return hgt;
}

static void test_sand_piles_fluid_spreads(void) {
    float s = drop_block(1), f = drop_block(0);
    CHECK(s > 0.3f, "sand collapsed to %f", s);
    CHECK(f < 0.2f, "fluid did not spread, height %f", f);
}

static void test_moving_sphere_pushes(void) {
    sl_world *w = make_world(5000);
    add_container(w, (sl_vec3){0.6f, 0.6f, 0.6f});
    sl_spawn_box(w, water(w), (sl_vec3){-0.6f, -0.6f, -0.6f}, (sl_vec3){0.6f, -0.1f, 0.6f});
    sl_collider_desc c = {0};
    c.shape = SL_SPHERE;
    c.radius = 0.2f;
    c.position = (sl_vec3){-0.4f, -0.35f, 0};
    sl_collider ball = sl_collider_add(w, &c);
    for (int s = 0; s < 120; s++) {
        sl_collider_move(w, ball, (sl_vec3){-0.4f + 0.8f * (float)(s + 1) / 120.0f, -0.35f, 0}, NULL);
        sl_step(w, DT);
    }
    const sl_vec3 *p = sl_positions(w);
    int inside = 0;
    for (int i = 0; i < sl_count(w); i++) {
        float dx = p[i].x - 0.4f, dy = p[i].y + 0.35f, dz = p[i].z;
        if (sqrtf(dx * dx + dy * dy + dz * dz) < 0.2f + R * 0.5f) inside++;
    }
    CHECK(inside == 0, "%d particles inside the moving sphere", inside);
    CHECK(all_finite(w), "non-finite positions");
    sl_world_destroy(w);
}

static sl_world *mixed_scene(void) {
    sl_world *w = make_world(3000);
    add_container(w, (sl_vec3){0.5f, 0.8f, 0.5f});
    sl_spawn_box(w, water(w), (sl_vec3){-0.5f, -0.8f, -0.5f}, (sl_vec3){0.5f, -0.4f, 0.5f});
    sl_spawn_box(w, sand(w), (sl_vec3){-0.2f, 0.0f, -0.2f}, (sl_vec3){0.2f, 0.4f, 0.2f});
    for (int i = 0; i < 200; i++) sl_step(w, DT);
    return w;
}

static void test_determinism(void) {
    sl_world *a = mixed_scene(), *b = mixed_scene();
    CHECK(sl_count(a) == sl_count(b), "counts differ");
    CHECK(memcmp(sl_positions(a), sl_positions(b), sizeof(sl_vec3) * (size_t)sl_count(a)) == 0, "runs differ");
    CHECK(all_finite(a), "non-finite positions");
    sl_world_destroy(a);
    sl_world_destroy(b);
}

static void test_limits(void) {
    CHECK(sl_world_create(NULL) == NULL, "null desc accepted");
    sl_world_desc bad = {0};
    CHECK(sl_world_create(&bad) == NULL, "zero desc accepted");

    sl_world *w = make_world(10);
    sl_material m = water(w);
    CHECK(sl_spawn(w, 5, (sl_vec3){0, 0, 0}, (sl_vec3){0, 0, 0}) == -1, "invalid material accepted");
    CHECK(sl_spawn_box(w, m, (sl_vec3){0, 0, 0}, (sl_vec3){1, 1, 1}) == 10, "spawn_box ignored the limit");
    CHECK(sl_spawn(w, m, (sl_vec3){0, 0, 0}, (sl_vec3){0, 0, 0}) == -1, "spawn past the limit");

    sl_vec3 last = sl_positions(w)[9];
    sl_remove(w, -1);
    sl_remove(w, 10);
    sl_remove(w, 3);
    CHECK(sl_count(w) == 9, "remove count %d", sl_count(w));
    CHECK(memcmp(&sl_positions(w)[3], &last, sizeof last) == 0, "remove did not move the last particle");
    sl_clear(w);
    CHECK(sl_count(w) == 0, "clear failed");

    for (int i = 1; i < SL_MAX_MATERIALS; i++) water(w);
    CHECK(water(w) == -1, "material limit not enforced");
    sl_collider_desc c = {0};
    for (int i = 0; i < SL_MAX_COLLIDERS; i++) sl_collider_add(w, &c);
    CHECK(sl_collider_add(w, &c) == -1, "collider limit not enforced");
    sl_collider_move(w, 999, (sl_vec3){0, 0, 0}, NULL);
    sl_step(w, DT);
    sl_world_destroy(w);
    sl_world_destroy(NULL);
    sl_step(NULL, DT);
    CHECK(sl_count(NULL) == 0, "null count");
}

static int live_allocs, total_allocs;
static void *count_alloc(size_t size, void *user) { (void)user; live_allocs++; total_allocs++; return malloc(size); }
static void count_free(void *ptr, void *user) { (void)user; live_allocs--; free(ptr); }

static void test_allocator(void) {
    sl_world_desc d = {0};
    d.max_particles = 100;
    d.particle_radius = R;
    d.allocator = (sl_allocator){count_alloc, count_free, NULL};
    sl_world *w = sl_world_create(&d);
    CHECK(w != NULL, "create with allocator failed");
    CHECK(total_allocs > 0, "custom allocator not used");
    sl_world_destroy(w);
    CHECK(live_allocs == 0, "%d allocations leaked", live_allocs);
}

typedef struct { const char *name; void (*fn)(void); } test;

int main(void) {
    test tests[] = {
        {"free fall", test_free_fall},
        {"settle on floor", test_settle_on_floor},
        {"container holds", test_container_holds},
        {"fluid levels out", test_fluid_levels_out},
        {"sand piles, fluid spreads", test_sand_piles_fluid_spreads},
        {"moving sphere pushes", test_moving_sphere_pushes},
        {"determinism", test_determinism},
        {"limits", test_limits},
        {"allocator", test_allocator},
    };
    for (size_t i = 0; i < sizeof tests / sizeof tests[0]; i++) {
        int before = failures;
        tests[i].fn();
        printf("%s %s\n", failures == before ? "ok  " : "FAIL", tests[i].name);
    }
    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
