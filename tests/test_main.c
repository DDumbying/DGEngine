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

static sl_world *make_world_n(int max, int workers) {
    sl_world_desc d = {0};
    d.max_particles = max;
    d.particle_radius = R;
    d.gravity = (sl_vec3){0, -9.81f, 0};
    d.workers = workers;
    return sl_world_create(&d);
}

static sl_world *make_world(int max) { return make_world_n(max, 1); }

static sl_material water(sl_world *w) {
    sl_material_desc m = {SL_FLUID, 1000, 0.01f, 0, 0, 0};
    return sl_material_add(w, &m);
}

static sl_material sand(sl_world *w) {
    sl_material_desc m = {SL_GRANULAR, 1600, 0, 0, 0.9f, 0};
    return sl_material_add(w, &m);
}

static sl_material solid(sl_world *w) {
    sl_material_desc m = {SL_SOLID, 800, 0, 0, 0.5f, 0, 1.0f};
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

static float dist(sl_vec3 a, sl_vec3 b) {
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

static void steps(sl_world *w, int n) { for (int i = 0; i < n; i++) sl_step(w, DT); }

static void test_free_fall(void) {
    sl_world *w = make_world(4);
    sl_spawn(w, water(w), (sl_vec3){0, 10, 0}, (sl_vec3){0, 0, 0});
    steps(w, 60);
    float y = sl_positions(w)[0].y, expect = 10.0f - 0.5f * 9.81f;
    CHECK(fabsf(y - expect) < 0.05f, "free fall y=%f expected %f", y, expect);
    sl_world_destroy(w);
}

static void test_settle_on_floor(void) {
    sl_world *w = make_world(2000);
    add_floor(w, 0.5f);
    sl_spawn_box(w, sand(w), (sl_vec3){-0.25f, 0.2f, -0.25f}, (sl_vec3){0.25f, 0.7f, 0.25f});
    steps(w, 300);
    const sl_vec3 *p = sl_positions(w);
    float lowest = 1e9f;
    for (int i = 0; i < sl_count(w); i++) if (p[i].y < lowest) lowest = p[i].y;
    CHECK(lowest > R * 0.9f, "particle sank into floor: y=%f", lowest);
    CHECK(max_speed(w) < 0.05f, "pile did not settle: max speed %f", max_speed(w));
    CHECK(all_finite(w), "non-finite positions");
    sl_world_destroy(w);
}

static void spray(sl_world *w, sl_material m, int n, float speed) {
    unsigned seed = 1;
    for (int i = 0; i < n; i++) {
        seed = seed * 1664525u + 1013904223u;
        float a = (float)(seed >> 8) / 16777216.0f * 6.2831853f;
        sl_vec3 pos = {(float)(i % 12) * 0.08f - 0.45f, (float)(i / 144) * 0.08f - 0.5f, (float)(i / 12 % 12) * 0.08f - 0.45f};
        sl_spawn(w, m, pos, (sl_vec3){cosf(a) * speed, 4, sinf(a) * speed});
    }
}

static int escaped(const sl_world *w, float half) {
    const sl_vec3 *p = sl_positions(w);
    for (int i = 0; i < sl_count(w); i++)
        if (fabsf(p[i].x) > half || fabsf(p[i].y) > half || fabsf(p[i].z) > half) return 1;
    return 0;
}

static void test_container_holds(void) {
    sl_world *w = make_world(3000);
    sl_material m = water(w);
    add_container(w, (sl_vec3){0.6f, 0.6f, 0.6f});
    spray(w, m, 2000, 6);
    int out = 0;
    for (int s = 0; s < 2000 && !out; s++) { sl_step(w, DT); out = escaped(w, 0.6f); }
    CHECK(!out, "particle escaped the container");
    CHECK(all_finite(w), "non-finite positions");
    sl_world_destroy(w);
}

static void test_fluid_levels_out(void) {
    sl_world *w = make_world(5000);
    add_container(w, (sl_vec3){0.5f, 1.0f, 0.5f});
    int n = sl_spawn_box(w, water(w), (sl_vec3){-0.5f, -1.0f, -0.5f}, (sl_vec3){0.0f, 0.2f, 0.5f});
    steps(w, 600);
    const sl_vec3 *p = sl_positions(w);
    double mean_y = 0, left = 0;
    for (int i = 0; i < n; i++) { mean_y += p[i].y + 1.0f; left += p[i].x < 0; }
    mean_y /= n;
    float d = 2 * R, expect_h = (float)n * d * d * d / 1.0f, h = (float)(2.0 * mean_y);
    CHECK(fabsf(h / expect_h - 1.0f) < 0.05f, "fluid height %f vs rest height %f", h, expect_h);
    CHECK(fabs(left / n - 0.5) < 0.1, "fluid did not spread: %.2f on the left", left / n);
    CHECK(max_speed(w) < 0.3f, "fluid still moving: %f", max_speed(w));
    sl_world_destroy(w);
}

static float drop_block(int granular) {
    sl_world *w = make_world(3000);
    add_floor(w, 0.6f);
    sl_material m = granular ? sand(w) : water(w);
    sl_spawn_box(w, m, (sl_vec3){-0.3f, 0.0f, -0.3f}, (sl_vec3){0.3f, 0.6f, 0.3f});
    steps(w, 400);
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
    for (int i = 0; i < sl_count(w); i++)
        if (dist(p[i], (sl_vec3){0.4f, -0.35f, 0}) < 0.2f + R * 0.5f) inside++;
    CHECK(inside == 0, "%d particles inside the moving sphere", inside);
    CHECK(all_finite(w), "non-finite positions");
    sl_world_destroy(w);
}

static sl_world *mixed_scene(int workers) {
    sl_world *w = make_world_n(4000, workers);
    add_container(w, (sl_vec3){0.5f, 0.8f, 0.5f});
    sl_spawn_box(w, water(w), (sl_vec3){-0.5f, -0.8f, -0.5f}, (sl_vec3){0.5f, -0.4f, 0.5f});
    sl_spawn_box(w, sand(w), (sl_vec3){-0.2f, 0.0f, -0.2f}, (sl_vec3){0.2f, 0.4f, 0.2f});
    sl_material s = solid(w);
    sl_object rope = sl_rope_create(w, s, (sl_vec3){-0.4f, 0.6f, 0}, (sl_vec3){0.4f, 0.6f, 0}, 0);
    const sl_particle *ids;
    int n = sl_object_particles(w, rope, &ids);
    sl_pin(w, ids[0], 1);
    sl_pin(w, ids[n - 1], 1);
    sl_softbody_create_box(w, s, (sl_vec3){0.1f, 0.2f, 0.1f}, (sl_vec3){0.4f, 0.5f, 0.4f}, 0.8f, 0.2f);
    steps(w, 200);
    return w;
}

static int same_state(const sl_world *a, const sl_world *b) {
    return sl_count(a) == sl_count(b)
        && memcmp(sl_positions(a), sl_positions(b), sizeof(sl_vec3) * (size_t)sl_count(a)) == 0
        && memcmp(sl_ids(a), sl_ids(b), sizeof(sl_particle) * (size_t)sl_count(a)) == 0;
}

static void test_determinism(void) {
    sl_world *a = mixed_scene(1), *b = mixed_scene(1);
    CHECK(same_state(a, b), "two single threaded runs differ");
    CHECK(all_finite(a), "non-finite positions");
    sl_world_destroy(a);
    sl_world_destroy(b);
}

static void test_thread_determinism(void) {
    sl_world *a = mixed_scene(1);
    for (int k = 2; k <= 4; k++) {
        sl_world *b = mixed_scene(k);
        CHECK(same_state(a, b), "1 and %d workers differ", k);
        sl_world_destroy(b);
    }
    sl_world_destroy(a);
}

/* A job system that runs chunks backwards, to prove chunk order does not change results. */
static void backwards(sl_task_fn *task, int count, void *ctx, void *user) {
    (void)user;
    for (int c = count - 1; c >= 0; c--) task(c, c + 1, ctx);
}

static void test_task_system(void) {
    sl_world_desc d = {0};
    d.max_particles = 4000;
    d.particle_radius = R;
    d.gravity = (sl_vec3){0, -9.81f, 0};
    d.tasks.parallel_for = backwards;
    sl_world *a = sl_world_create(&d), *b = make_world(4000);
    sl_world *ws[2] = {a, b};
    for (int k = 0; k < 2; k++) {
        add_container(ws[k], (sl_vec3){0.5f, 0.8f, 0.5f});
        sl_spawn_box(ws[k], water(ws[k]), (sl_vec3){-0.5f, -0.8f, -0.5f}, (sl_vec3){0.5f, -0.3f, 0.5f});
        steps(ws[k], 100);
    }
    CHECK(same_state(a, b), "custom task system changed the result");
    sl_world_destroy(a);
    sl_world_destroy(b);
}

static void test_stable_ids(void) {
    sl_world *w = make_world(3000);
    add_container(w, (sl_vec3){0.5f, 0.5f, 0.5f});
    sl_material m = water(w);
    sl_particle tracked = sl_spawn(w, m, (sl_vec3){0.3f, 0.4f, 0.3f}, (sl_vec3){0, 0, 0});
    sl_spawn_box(w, m, (sl_vec3){-0.5f, -0.5f, -0.5f}, (sl_vec3){0.5f, 0.0f, 0.5f});
    sl_vec3 last = sl_position(w, tracked);
    int jumps = 0;
    for (int s = 0; s < 200; s++) {
        sl_step(w, DT);
        sl_vec3 now = sl_position(w, tracked);
        if (dist(now, last) > 0.2f) jumps++;
        last = now;
    }
    CHECK(jumps == 0, "tracked particle jumped %d times while memory was reordered", jumps);
    const sl_particle *ids = sl_ids(w);
    const sl_vec3 *p = sl_positions(w);
    int bad = 0;
    for (int s = 0; s < sl_count(w); s++) if (dist(sl_position(w, ids[s]), p[s]) != 0) bad++;
    CHECK(bad == 0, "%d slots disagree with their ids", bad);

    sl_particle victim = ids[sl_count(w) / 2];
    int before = sl_count(w);
    CHECK(sl_remove(w, victim) == 1, "remove failed");
    CHECK(!sl_alive(w, victim) && sl_count(w) == before - 1, "removed particle still alive");
    CHECK(sl_alive(w, tracked), "unrelated particle died");
    CHECK(sl_remove(w, victim) == 0, "double remove succeeded");
    sl_world_destroy(w);
}

static void test_sleeping(void) {
    sl_world *w = make_world(3000);
    add_floor(w, 0.6f);
    sl_spawn_box(w, sand(w), (sl_vec3){-0.3f, 0.0f, -0.3f}, (sl_vec3){0.3f, 0.5f, 0.3f});
    steps(w, 300);
    sl_stats st;
    sl_get_stats(w, &st);
    CHECK(st.awake == 0, "%d of %d particles still awake after settling", st.awake, st.particles);
    sl_vec3 before = sl_positions(w)[0];
    steps(w, 30);
    CHECK(dist(before, sl_positions(w)[0]) == 0, "sleeping particle moved");

    sl_collider_desc c = {0};
    c.shape = SL_SPHERE;
    c.radius = 0.15f;
    c.position = (sl_vec3){-1.0f, 0.2f, 0};
    sl_collider ball = sl_collider_add(w, &c);
    for (int s = 0; s < 60; s++) {
        sl_collider_move(w, ball, (sl_vec3){-1.0f + 1.0f * (float)(s + 1) / 60.0f, 0.2f, 0}, NULL);
        sl_step(w, DT);
    }
    sl_get_stats(w, &st);
    CHECK(st.awake > 0, "moving collider did not wake the pile");
    CHECK(all_finite(w), "non-finite positions");
    sl_world_destroy(w);
}

static void test_rope(void) {
    sl_world *w = make_world(1000);
    sl_object rope = sl_rope_create(w, solid(w), (sl_vec3){0, 2, 0}, (sl_vec3){1.5f, 2, 0}, 0);
    const sl_particle *ids;
    int n = sl_object_particles(w, rope, &ids);
    CHECK(n >= 10, "rope has only %d particles", n);
    sl_pin(w, ids[0], 1);
    steps(w, 300);
    float len = 0;
    for (int i = 0; i + 1 < n; i++) len += dist(sl_position(w, ids[i]), sl_position(w, ids[i + 1]));
    CHECK(fabsf(len / 1.5f - 1.0f) < 0.02f, "rope length %f, rest 1.5", len);
    sl_vec3 top = sl_position(w, ids[0]), end = sl_position(w, ids[n - 1]);
    CHECK(dist(top, (sl_vec3){0, 2, 0}) < 1e-6f, "pinned end moved");
    CHECK(fabsf(end.x) < 0.3f && end.y < 0.8f, "rope did not hang down: end (%f, %f)", end.x, end.y);
    CHECK(sl_remove(w, ids[3]) == 0, "rope particle removed directly");
    sl_object_destroy(w, rope);
    CHECK(sl_count(w) == 0, "destroy left %d particles", sl_count(w));
    sl_world_destroy(w);
}

static void test_cloth(void) {
    sl_world *w = make_world(2000);
    sl_object cloth = sl_cloth_create(w, solid(w), (sl_vec3){-0.5f, 2, 0}, (sl_vec3){1, 0, 0}, (sl_vec3){0, 0, 1}, 0, 0.001f);
    int nu, nv;
    sl_object_grid(w, cloth, &nu, &nv);
    const sl_particle *ids;
    sl_object_particles(w, cloth, &ids);
    sl_pin(w, ids[0], 1);
    sl_pin(w, ids[nu - 1], 1);
    steps(w, 300);
    float worst = 0;
    for (int j = 0; j < nv; j++)
        for (int i = 0; i + 1 < nu; i++) {
            float l = dist(sl_position(w, ids[j * nu + i]), sl_position(w, ids[j * nu + i + 1]));
            float stretch = fabsf(l / (1.0f / (float)(nu - 1)) - 1.0f);
            if (stretch > worst) worst = stretch;
        }
    CHECK(worst < 0.1f, "cloth edge stretched by %.0f%%", worst * 100);
    CHECK(max_speed(w) < 0.5f, "cloth still swinging hard: %f", max_speed(w));
    CHECK(sl_position(w, ids[nu * (nv - 1)]).y < 1.3f, "cloth did not hang");
    CHECK(all_finite(w), "non-finite positions");
    sl_world_destroy(w);
}

static float soft_drop(float stiffness, float plasticity) {
    sl_world *w = make_world(2000);
    add_floor(w, 0.8f);
    sl_object body = sl_softbody_create_box(w, solid(w), (sl_vec3){-0.3f, 1.5f, -0.3f}, (sl_vec3){0.3f, 2.1f, 0.3f}, stiffness, plasticity);
    steps(w, 300);
    const sl_particle *ids;
    int n = sl_object_particles(w, body, &ids);
    float lo = 1e9f, hi = -1e9f;
    for (int i = 0; i < n; i++) {
        float y = sl_position(w, ids[i]).y;
        if (y < lo) lo = y;
        if (y > hi) hi = y;
    }
    sl_world_destroy(w);
    return (hi - lo) / 0.5f;
}

static void test_softbody(void) {
    float stiff = soft_drop(1.0f, 0), soft = soft_drop(0.03f, 0), plastic = soft_drop(0.03f, 0.5f);
    CHECK(fabsf(stiff - 1.0f) < 0.1f, "stiff body height ratio %f", stiff);
    /* A body this soft settles into one of two shapes, about 0.80 or 0.94 tall, depending on the landing. */
    CHECK(soft > 0.75f, "soft body did not recover: %f", soft);
    CHECK(plastic < soft - 0.2f, "plastic body kept no dent: %f vs %f", plastic, soft);
}

static void test_buoyancy(void) {
    sl_world *w = make_world(8000);
    add_container(w, (sl_vec3){0.6f, 0.8f, 0.6f});
    sl_spawn_box(w, water(w), (sl_vec3){-0.6f, -0.8f, -0.6f}, (sl_vec3){0.6f, 0.0f, 0.6f});
    sl_material light = sl_material_add(w, &(sl_material_desc){.kind = SL_SOLID, .density = 500, .friction = 0.3f, .damping = 1});
    sl_material heavy = sl_material_add(w, &(sl_material_desc){.kind = SL_SOLID, .density = 3000, .friction = 0.3f, .damping = 1});
    sl_object cork = sl_softbody_create_box(w, light, (sl_vec3){-0.45f, 0.1f, -0.1f}, (sl_vec3){-0.15f, 0.4f, 0.2f}, 1, 0);
    sl_object rock = sl_softbody_create_box(w, heavy, (sl_vec3){0.15f, 0.1f, -0.1f}, (sl_vec3){0.45f, 0.4f, 0.2f}, 1, 0);
    steps(w, 600);
    const sl_particle *ids;
    float top = max_height(w), cork_y = 0, rock_y = 0;
    int n = sl_object_particles(w, cork, &ids);
    for (int i = 0; i < n; i++) cork_y += sl_position(w, ids[i]).y / (float)n;
    n = sl_object_particles(w, rock, &ids);
    for (int i = 0; i < n; i++) rock_y += sl_position(w, ids[i]).y / (float)n;
    CHECK(cork_y > top - 0.25f, "light body did not float: center %f, water top %f", cork_y, top);
    CHECK(rock_y < -0.5f, "heavy body did not sink: center %f", rock_y);
    sl_world_destroy(w);
}

static void test_collider_force(void) {
    sl_world *w = make_world(20000);
    add_container(w, (sl_vec3){0.6f, 0.6f, 0.6f});
    sl_spawn_box(w, water(w), (sl_vec3){-0.6f, -0.6f, -0.6f}, (sl_vec3){0.6f, 0.0f, 0.6f});
    sl_collider_desc c = {0};
    c.shape = SL_BOX;
    c.half_extents = (sl_vec3){0.2f, 0.2f, 0.2f};
    c.position = (sl_vec3){0, 0.05f, 0};
    sl_collider box = sl_collider_add(w, &c);
    steps(w, 400);
    sl_vec3 f = {0, 0, 0};
    for (int s = 0; s < 60; s++) { sl_step(w, DT); sl_vec3 g = sl_collider_force(w, box); f.y += g.y / 60; f.x += g.x / 60; }
    float top = max_height(w);
    float depth = top + R - (0.05f - 0.2f);
    float expect = 1000.0f * 9.81f * 0.4f * 0.4f * (depth < 0.4f ? depth : 0.4f);
    CHECK(f.y > 0.6f * expect && f.y < 1.4f * expect, "buoyancy %f N, expected about %f N", f.y, expect);
    CHECK(fabsf(f.x) < 0.2f * expect, "sideways force %f N", f.x);
    sl_world_destroy(w);
}

static void test_no_tunneling(void) {
    sl_world_desc d = {0};
    d.max_particles = 10;
    d.particle_radius = R;
    sl_world *w = sl_world_create(&d);
    sl_collider_desc c = {0};
    c.shape = SL_BOX;
    c.half_extents = (sl_vec3){0.01f, 1, 1};
    sl_collider_add(w, &c);
    sl_particle p = sl_spawn(w, sand(w), (sl_vec3){-1, 0, 0}, (sl_vec3){30, 0, 0});
    steps(w, 60);
    CHECK(sl_position(w, p).x < 0, "fast particle went through a thin wall: x=%f", sl_position(w, p).x);
    sl_world_destroy(w);
}

static void test_large_dt(void) {
    sl_world *w = make_world(4000);
    add_container(w, (sl_vec3){0.6f, 0.6f, 0.6f});
    spray(w, water(w), 1500, 8);
    sl_spawn_box(w, sand(w), (sl_vec3){-0.2f, 0.2f, -0.2f}, (sl_vec3){0.2f, 0.5f, 0.2f});
    int out = 0;
    for (int s = 0; s < 300 && !out; s++) { sl_step(w, 1.0f / 30.0f); out = escaped(w, 0.6f); }
    CHECK(!out, "particle escaped at dt 1/30");
    CHECK(all_finite(w), "non-finite positions");
    sl_world_destroy(w);
}

static void test_drop_from_height(void) {
    sl_world *w = make_world(6000);
    add_container(w, (sl_vec3){1.0f, 6.0f, 1.0f});
    sl_spawn_box(w, water(w), (sl_vec3){-0.6f, 3.0f, -0.6f}, (sl_vec3){0.6f, 4.0f, 0.6f});
    sl_spawn_box(w, sand(w), (sl_vec3){-0.3f, 4.5f, -0.3f}, (sl_vec3){0.3f, 5.0f, 0.3f});
    int out = 0;
    for (int s = 0; s < 600 && !out; s++) { sl_step(w, DT); out = escaped(w, 6.0f); }
    CHECK(!out, "particle escaped after a 5 m drop");
    CHECK(all_finite(w), "non-finite positions");
    CHECK(max_height(w) < 0.0f, "particles did not come down: top %f", max_height(w));
    sl_world_destroy(w);
}

static void test_no_energy_gain(void) {
    sl_world *w = make_world(3000);
    add_container(w, (sl_vec3){0.6f, 0.6f, 0.6f});
    spray(w, water(w), 1500, 3);
    float peak = 0;
    for (int s = 0; s < 3000; s++) {
        sl_step(w, DT);
        if (s > 600 && max_speed(w) > peak) peak = max_speed(w);
    }
    CHECK(peak < 1.0f, "fluid kept gaining speed: peak %f after settling", peak);
    sl_world_destroy(w);
}

static void test_raycast(void) {
    sl_world *w = make_world(100);
    sl_material m = sand(w);
    sl_particle near = sl_spawn(w, m, (sl_vec3){0, 0, -1}, (sl_vec3){0, 0, 0});
    sl_spawn(w, m, (sl_vec3){0, 0, -3}, (sl_vec3){0, 0, 0});
    float d = 0;
    CHECK(sl_raycast(w, (sl_vec3){0, 0, 0}, (sl_vec3){0, 0, -1}, 10, &d) == near, "ray missed the nearest particle");
    CHECK(fabsf(d - (1 - R)) < 1e-4f, "hit distance %f", d);
    CHECK(sl_raycast(w, (sl_vec3){0, 0, 0}, (sl_vec3){0, 1, 0}, 10, NULL) == -1, "ray hit empty space");
    CHECK(sl_raycast(w, (sl_vec3){0, 0, 0}, (sl_vec3){0, 0, -1}, 0.5f, NULL) == -1, "ray ignored max distance");
    sl_world_destroy(w);
}

static void test_grab_and_throw(void) {
    sl_world *w = make_world(2000);
    add_floor(w, 0.5f);
    sl_object body = sl_softbody_create_box(w, solid(w), (sl_vec3){-0.15f, 0, -0.15f}, (sl_vec3){0.15f, 0.3f, 0.15f}, 0.8f, 0);
    steps(w, 60);
    const sl_particle *ids;
    int n = sl_object_particles(w, body, &ids);
    sl_particle handle = ids[n - 1];
    CHECK(sl_grab_begin(w, handle) == 1, "grab failed");
    sl_vec3 start = sl_position(w, handle);
    for (int s = 1; s <= 60; s++) {
        sl_grab_move(w, handle, (sl_vec3){start.x + 0.02f * (float)s, start.y + 0.015f * (float)s, start.z});
        sl_step(w, DT);
    }
    float cy = 0;
    for (int i = 0; i < n; i++) cy += sl_position(w, ids[i]).y / (float)n;
    CHECK(cy > 0.5f, "body did not follow the grab: center y %f", cy);
    sl_vec3 v = sl_velocity(w, handle);
    sl_grab_end(w, handle);
    CHECK(v.x > 0.8f, "grabbed particle has no throw velocity: %f", v.x);
    sl_step(w, DT);
    CHECK(sl_velocity(w, handle).x > 0.5f, "release lost the throw: %f", sl_velocity(w, handle).x);
    steps(w, 120);
    CHECK(all_finite(w), "non-finite positions");
    sl_world_destroy(w);
}

static void test_collider_toggle(void) {
    sl_world *w = make_world(10);
    sl_collider_desc c = {0};
    c.shape = SL_PLANE;
    sl_collider floor = sl_collider_add(w, &c);
    sl_particle p = sl_spawn(w, sand(w), (sl_vec3){0, 0.5f, 0}, (sl_vec3){0, 0, 0});
    steps(w, 60);
    CHECK(sl_position(w, p).y > 0, "floor did not hold");
    sl_collider_set_enabled(w, floor, 0);
    CHECK(!sl_collider_enabled(w, floor), "collider still enabled");
    steps(w, 30);
    CHECK(sl_position(w, p).y < -0.2f, "disabled floor still holds: %f", sl_position(w, p).y);
    sl_collider_remove(w, floor);
    sl_collider again = sl_collider_add(w, &c);
    CHECK(again == floor, "removed slot not reused");
    sl_world_destroy(w);
}

static void test_remove_sphere(void) {
    sl_world *w = make_world(3000);
    sl_spawn_box(w, sand(w), (sl_vec3){-0.5f, 0, -0.5f}, (sl_vec3){0.5f, 0.1f, 0.5f});
    sl_object rope = sl_rope_create(w, solid(w), (sl_vec3){-1, 1, 0}, (sl_vec3){1, 1, 0}, 0);
    int before = sl_count(w), rope_n = sl_object_particles(w, rope, NULL);
    int gone = sl_remove_sphere(w, (sl_vec3){0, 0.05f, 0}, 0.22f);
    CHECK(gone > 10 && sl_count(w) == before - gone, "removed %d, count %d of %d", gone, sl_count(w), before);
    gone = sl_remove_sphere(w, (sl_vec3){0.9f, 1, 0}, 0.06f);
    CHECK(gone == rope_n, "rope not removed whole: %d of %d", gone, rope_n);
    CHECK(sl_object_particles(w, rope, NULL) == 0, "rope still alive");
    steps(w, 10);
    sl_world_destroy(w);
}

static void test_anisotropy(void) {
    sl_world_desc d = {0};
    d.max_particles = 8000;
    d.particle_radius = R;
    d.gravity = (sl_vec3){0, -9.81f, 0};
    d.anisotropy = 1;
    sl_world *w = sl_world_create(&d);
    add_container(w, (sl_vec3){0.5f, 1.0f, 0.5f});
    sl_spawn_box(w, water(w), (sl_vec3){-0.5f, -1.0f, -0.5f}, (sl_vec3){0.5f, -0.4f, 0.5f});
    steps(w, 200);
    const sl_vec3 *a = sl_anisotropy(w), *p = sl_positions(w);
    CHECK(a != NULL, "no anisotropy output");
    float top = max_height(w), flat_top = 0, flat_mid = 0;
    int n_top = 0, n_mid = 0;
    for (int s = 0; s < sl_count(w); s++) {
        if (fabsf(p[s].x) > 0.3f || fabsf(p[s].z) > 0.3f) continue;
        float shortest = 1e9f, longest = 0, vertical = 0;
        for (int k = 1; k <= 3; k++) {
            float l = sqrtf(a[4 * s + k].x * a[4 * s + k].x + a[4 * s + k].y * a[4 * s + k].y + a[4 * s + k].z * a[4 * s + k].z);
            if (l < shortest) { shortest = l; vertical = fabsf(a[4 * s + k].y) / l; }
            if (l > longest) longest = l;
        }
        if (p[s].y > top - 0.02f) { flat_top += (longest / shortest) * vertical; n_top++; }
        else if (p[s].y < -0.8f && p[s].y > -0.85f) { flat_mid += longest / shortest; n_mid++; }
    }
    CHECK(n_top > 0 && flat_top / (float)n_top > 1.5f, "surface not flattened upward: %f", n_top ? flat_top / (float)n_top : 0);
    CHECK(n_mid > 0 && flat_mid / (float)n_mid < 1.6f, "interior not round: %f", n_mid ? flat_mid / (float)n_mid : 0);
    sl_world_destroy(w);
}

static void test_diffuse(void) {
    sl_world_desc d = {0};
    d.max_particles = 8000;
    d.particle_radius = R;
    d.gravity = (sl_vec3){0, -9.81f, 0};
    d.max_diffuse = 20000;
    sl_world *w = sl_world_create(&d);
    add_container(w, (sl_vec3){0.8f, 1.0f, 0.5f});
    sl_spawn_box(w, water(w), (sl_vec3){-0.8f, -1.0f, -0.5f}, (sl_vec3){-0.3f, 0.6f, 0.5f});
    int peak = 0;
    for (int s = 0; s < 120; s++) { sl_step(w, DT); int n = sl_diffuse(w, NULL, NULL, NULL, NULL); if (n > peak) peak = n; }
    CHECK(peak > 50, "a dam break made only %d spray particles", peak);
    for (int s = 0; s < 900; s++) sl_step(w, DT);
    CHECK(sl_diffuse(w, NULL, NULL, NULL, NULL) < peak / 4, "spray did not fade: %d of %d", sl_diffuse(w, NULL, NULL, NULL, NULL), peak);
    sl_world_destroy(w);
}

static float pile_height(float friction) {
    sl_world *w = make_world(3000);
    add_floor(w, 0.8f);
    sl_material m = sl_material_add(w, &(sl_material_desc){.kind = SL_GRANULAR, .density = 1600, .friction = friction});
    unsigned seed = 7;
    for (int s = 0; s < 900; s++) {
        if (s % 2 == 0 && s < 500) {
            seed = seed * 1664525u + 1013904223u;
            float jx = (float)(seed >> 8) / 16777216.0f - 0.5f;
            seed = seed * 1664525u + 1013904223u;
            float jz = (float)(seed >> 8) / 16777216.0f - 0.5f;
            sl_spawn(w, m, (sl_vec3){jx * 0.05f, 1.5f, jz * 0.05f}, (sl_vec3){0, -1, 0});
        }
        sl_step(w, DT);
    }
    float hgt = max_height(w);
    sl_world_destroy(w);
    return hgt;
}

static void test_pile_angle(void) {
    float slick = pile_height(0.2f), rough = pile_height(0.8f);
    CHECK(rough > slick + 0.2f, "rough pile %f not taller than slick %f", rough, slick);
}

static int lifted_with(float cohesion) {
    sl_world *w = make_world(2000);
    add_floor(w, 0.8f);
    sl_material m = sl_material_add(w, &(sl_material_desc){.kind = SL_GRANULAR, .density = 1600, .friction = 0.5f, .wet_cohesion = cohesion});
    sl_material wm = water(w);
    sl_spawn_box(w, m, (sl_vec3){-0.15f, 0.0f, -0.15f}, (sl_vec3){0.15f, 0.3f, 0.15f});
    sl_spawn_box(w, wm, (sl_vec3){-0.15f, 0.32f, -0.15f}, (sl_vec3){0.15f, 0.42f, 0.15f});
    steps(w, 40);
    for (int s = sl_count(w) - 1; s >= 0; s--)
        if (sl_materials(w)[s] == wm) sl_remove(w, sl_ids(w)[s]);
    steps(w, 10);
    sl_particle top = sl_raycast(w, (sl_vec3){0, 2, 0}, (sl_vec3){0, -1, 0}, 5, NULL);
    sl_vec3 start = sl_position(w, top);
    sl_grab_begin(w, top);
    for (int s = 1; s <= 40; s++) { sl_grab_move(w, top, (sl_vec3){start.x, start.y + 0.008f * (float)s, start.z}); sl_step(w, DT); }
    int lifted = 0;
    for (int s = 0; s < sl_count(w); s++) if (sl_positions(w)[s].y > 0.3f + 0.08f) lifted++;
    sl_world_destroy(w);
    return lifted;
}

static void test_wet_sand(void) {
    sl_world *w = make_world(1000);
    sl_material m = sand(w), wm = water(w);
    add_floor(w, 0.5f);
    sl_spawn_box(w, m, (sl_vec3){-0.1f, 0, -0.1f}, (sl_vec3){0.1f, 0.1f, 0.1f});
    sl_spawn_box(w, wm, (sl_vec3){-0.1f, 0.15f, -0.1f}, (sl_vec3){0.1f, 0.35f, 0.1f});
    steps(w, 60);
    const unsigned char *wet = sl_wetness(w);
    int soaked = 0;
    for (int s = 0; s < sl_count(w); s++) if (sl_materials(w)[s] == m && wet[s] > 200) soaked++;
    CHECK(soaked > 0, "no grain got wet");
    sl_world_destroy(w);
    int dry = lifted_with(0), damp = lifted_with(1);
    CHECK(damp > dry + 2, "wet clump lifted %d grains, dry %d", damp, dry);
}

static void test_limits(void) {
    CHECK(sl_world_create(NULL) == NULL, "null desc accepted");
    sl_world_desc bad = {0};
    CHECK(sl_world_create(&bad) == NULL, "zero desc accepted");

    sl_world *w = make_world(10);
    sl_material m = water(w);
    CHECK(sl_spawn(w, 5, (sl_vec3){0, 0, 0}, (sl_vec3){0, 0, 0}) == -1, "invalid material accepted");
    CHECK(sl_spawn(w, m, (sl_vec3){NAN, 0, 0}, (sl_vec3){0, 0, 0}) == -1, "NaN position accepted");
    CHECK(sl_spawn_box(w, m, (sl_vec3){0, 0, 0}, (sl_vec3){1, 1, 1}) == 10, "spawn_box ignored the limit");
    CHECK(sl_spawn(w, m, (sl_vec3){0, 0, 0}, (sl_vec3){0, 0, 0}) == -1, "spawn past the limit");
    CHECK(sl_remove(w, -1) == 0 && sl_remove(w, 99) == 0, "bad ids removed");
    CHECK(sl_rope_create(w, m, (sl_vec3){0, 0, 0}, (sl_vec3){1, 0, 0}, 0) == -1, "rope created past the limit");
    sl_clear(w);
    CHECK(sl_count(w) == 0, "clear failed");

    for (int i = 1; i < SL_MAX_MATERIALS; i++) water(w);
    CHECK(water(w) == -1, "material limit not enforced");
    sl_collider_desc c = {0};
    for (int i = 0; i < SL_MAX_COLLIDERS; i++) sl_collider_add(w, &c);
    CHECK(sl_collider_add(w, &c) == -1, "collider limit not enforced");
    sl_collider_move(w, 999, (sl_vec3){0, 0, 0}, NULL);
    sl_object_destroy(w, 42);
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
    d.max_particles = 5000;
    d.particle_radius = R;
    d.workers = 3;
    d.allocator = (sl_allocator){count_alloc, count_free, NULL};
    sl_world *w = sl_world_create(&d);
    CHECK(w != NULL, "create with allocator failed");
    sl_spawn_box(w, water(w), (sl_vec3){-0.5f, 0, -0.5f}, (sl_vec3){0.5f, 0.3f, 0.5f});
    sl_softbody_create_box(w, solid(w), (sl_vec3){0, 1, 0}, (sl_vec3){0.3f, 1.3f, 0.3f}, 0.5f, 0);
    steps(w, 5);
    CHECK(total_allocs > 0, "custom allocator not used");
    sl_world_destroy(w);
    CHECK(live_allocs == 0, "%d allocations leaked", live_allocs);
}

/* A grab must die with its particle, or a new particle that reuses its slot gets dragged. */
static void test_grab_dies_with_particle(void) {
    sl_world *w = make_world(200);
    sl_material m = sand(w), s = solid(w);
    sl_particle a = sl_spawn(w, m, (sl_vec3){0, 1, 0}, (sl_vec3){0, 0, 0});
    sl_grab_begin(w, a);
    sl_remove(w, a);
    sl_particle b = sl_spawn(w, m, (sl_vec3){3, 1, 0}, (sl_vec3){0, 0, 0});
    sl_grab_move(w, a, (sl_vec3){3, 5, 0});
    sl_grab_move(w, b, (sl_vec3){3, 5, 0});
    steps(w, 2);
    CHECK(sl_position(w, b).y < 1.0f, "new particle was dragged by an old grab: y %f", sl_position(w, b).y);

    sl_object rope = sl_rope_create(w, s, (sl_vec3){-1, 2, 0}, (sl_vec3){1, 2, 0}, 0);
    const sl_particle *ids;
    int n = sl_object_particles(w, rope, &ids);
    sl_particle end = ids[n - 1];
    sl_grab_begin(w, end);
    sl_object_destroy(w, rope);
    for (int k = 0; k < n; k++) sl_spawn(w, m, (sl_vec3){-3 + 0.2f * (float)k, 1, 2}, (sl_vec3){0, 0, 0});
    sl_grab_move(w, end, (sl_vec3){0, 6, 0});
    steps(w, 2);
    CHECK(max_height(w) < 1.0f, "particle reusing a rope slot was dragged: top %f", max_height(w));
    sl_world_destroy(w);
}

/* A fast particle resting on a ball must keep sliding, not be put back where it started. */
static void test_fast_slide_on_ball(void) {
    sl_world *w = make_world(4);
    sl_collider_desc ball = {0};
    ball.shape = SL_SPHERE;
    ball.radius = 2.0f;
    sl_collider_add(w, &ball);
    sl_particle p = sl_spawn(w, sand(w), (sl_vec3){0, 2.0f + 0.9f * R, 0}, (sl_vec3){20, 0, 0});
    sl_step(w, DT);
    CHECK(sl_position(w, p).x > 0.15f, "particle stuck on the ball: moved %f", sl_position(w, p).x);
    sl_world_destroy(w);
}

/* Sleeping water keeps its surface shape on the right particle when memory is reordered. */
static void test_anisotropy_after_reorder(void) {
    sl_world_desc d = {0};
    d.max_particles = 4000;
    d.particle_radius = R;
    d.gravity = (sl_vec3){0, -9.81f, 0};
    d.anisotropy = 1;
    sl_world *w = sl_world_create(&d);
    add_container(w, (sl_vec3){0.4f, 1.0f, 0.4f});
    sl_spawn_box(w, water(w), (sl_vec3){-0.4f, -1.0f, -0.4f}, (sl_vec3){0.4f, -0.7f, 0.4f});
    sl_stats st;
    for (int i = 0; i < 600; i++) { sl_step(w, DT); sl_get_stats(w, &st); if (!st.awake) break; }
    CHECK(st.awake == 0, "pool never slept");
    sl_material m = sand(w);
    for (int k = 0; k < 50; k++) sl_spawn(w, m, (sl_vec3){0.3f * (float)(k % 5) - 0.6f, 0.5f, 0.1f * (float)(k / 5) - 0.3f}, (sl_vec3){0, 0, 0});
    steps(w, 3);
    const sl_vec3 *a = sl_anisotropy(w), *p = sl_positions(w);
    float worst = 0;
    for (int s = 0; s < sl_count(w); s++) worst = fmaxf(worst, dist(a[4 * s], p[s]));
    CHECK(worst < 2 * R, "ellipsoid left its particle by %f", worst);
    sl_world_destroy(w);
}

static void sand_scene(sl_world *w, sl_material m) {
    add_floor(w, 0.5f);
    sl_spawn_box(w, m, (sl_vec3){-0.3f, 0.2f, -0.3f}, (sl_vec3){0.3f, 0.6f, 0.3f});
}

/* After sl_clear a world must behave exactly like a new one. */
static void test_clear_is_fresh(void) {
    sl_world *a = make_world(4000), *b = make_world(4000);
    sl_material sa = sand(a), ca = solid(a);
    sl_material sb = sand(b);
    solid(b);
    sl_object cloth = sl_cloth_create(a, ca, (sl_vec3){-0.5f, 1, -0.5f}, (sl_vec3){1, 0, 0}, (sl_vec3){0, 0, 1}, 0, 0.01f);
    CHECK(cloth >= 0, "cloth not made");
    steps(a, 20);
    sl_clear(a);
    sl_stats st;
    sl_get_stats(a, &st);
    CHECK(st.particles == 0 && st.awake == 0 && st.contacts == 0 && st.pairs == 0, "stats not reset: %d awake, %d contacts", st.awake, st.contacts);
    sand_scene(a, sa);
    sand_scene(b, sb);
    steps(a, 60);
    steps(b, 60);
    /* Ids differ, since a cleared world never hands out an old id again, so compare slot by slot. */
    float worst = sl_count(a) == sl_count(b) ? 0 : 1e9f;
    for (int k = 0; k < sl_count(a) && k < sl_count(b); k++) worst = fmaxf(worst, dist(sl_positions(a)[k], sl_positions(b)[k]));
    CHECK(sl_count(a) == sl_count(b) && worst == 0, "cleared world differs from a new one by %f", worst);
    sl_world_destroy(a);
    sl_world_destroy(b);
}

static void test_removed_collider(void) {
    sl_world *w = make_world(2000);
    sl_collider_desc box = {0};
    box.shape = SL_BOX;
    box.half_extents = (sl_vec3){0.5f, 0.1f, 0.5f};
    sl_collider c = sl_collider_add(w, &box);
    sl_spawn_box(w, sand(w), (sl_vec3){-0.3f, 0.1f, -0.3f}, (sl_vec3){0.3f, 0.3f, 0.3f});
    steps(w, 30);
    CHECK(sl_collider_force(w, c).y < 0, "no load on the box");
    sl_collider_remove(w, c);
    sl_collider_move(w, c, (sl_vec3){0, 5, 0}, NULL);
    sl_vec3 f = sl_collider_force(w, c);
    CHECK(f.x == 0 && f.y == 0 && f.z == 0, "removed collider still reports force %f", f.y);
    steps(w, 10);
    CHECK(max_height(w) < 0.3f, "sand still held by a removed box");
    sl_world_destroy(w);
}

/* The grid must find exactly the pairs a brute-force search finds, packed (dense grid) or spread (hash). */
static int brute_pairs(const sl_vec3 *p, int n, float range) {
    int k = 0;
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++) k += dist(p[i], p[j]) < range;
    return k;
}

static void test_neighbor_search(void) {
    for (int c = 0; c < 2; c++) {
        sl_world_desc d = {0};
        d.max_particles = 4000;
        d.particle_radius = R;
        d.sleep_speed = -1;
        sl_world *w = sl_world_create(&d);
        sl_material m = sand(w);
        unsigned seed = 12345u;
        /* A jittered lattice with no overlaps, so nothing moves during the tiny step; a far copy forces the hash grid. */
        for (int copy = 0; copy <= c; copy++)
            for (int i = 0; i < 1331; i++) {
                float q[3];
                for (int a = 0; a < 3; a++) { seed = seed * 1664525u + 1013904223u; q[a] = ((float)(seed >> 8) / 16777216.0f - 0.5f) * 0.008f; }
                sl_spawn(w, m, (sl_vec3){0.11f * (float)(i % 11) + q[0] + 80.0f * (float)copy, 0.11f * (float)(i / 11 % 11) + q[1], 0.11f * (float)(i / 121) + q[2]},
                         (sl_vec3){0, 0, 0});
            }
        static sl_vec3 before[4000];
        int n = sl_count(w);
        for (int i = 0; i < n; i++) before[i] = sl_positions(w)[i];
        sl_step(w, 1e-6f);
        sl_stats st;
        sl_get_stats(w, &st);
        int want = brute_pairs(before, n, 2 * R * 2.1f);
        CHECK(st.pairs == want && st.rebuilds == 1, "%s grid found %d pairs, brute force %d", c ? "hash" : "dense", st.pairs, want);
        sl_world_destroy(w);
    }
}

/* Grains still get wet when sleeping is turned off. */
static void test_wet_without_sleep(void) {
    sl_world_desc d = {0};
    d.max_particles = 1000;
    d.particle_radius = R;
    d.gravity = (sl_vec3){0, -9.81f, 0};
    d.sleep_speed = -1;
    sl_world *w = sl_world_create(&d);
    sl_material m = sand(w), wm = water(w);
    add_floor(w, 0.5f);
    sl_spawn_box(w, m, (sl_vec3){-0.1f, 0, -0.1f}, (sl_vec3){0.1f, 0.1f, 0.1f});
    sl_spawn_box(w, wm, (sl_vec3){-0.1f, 0.15f, -0.1f}, (sl_vec3){0.1f, 0.35f, 0.1f});
    steps(w, 60);
    int soaked = 0;
    for (int s = 0; s < sl_count(w); s++) soaked += sl_materials(w)[s] == m && sl_wetness(w)[s] > 200;
    CHECK(soaked > 0, "no grain got wet with sleeping off");
    sl_world_destroy(w);
}

/* A soft body more than 120 particles long still holds together: kicking one end drags its neighbor. */
static void test_long_softbody(void) {
    sl_world_desc d = {0};
    d.max_particles = 1000;
    d.particle_radius = R;
    d.sleep_speed = -1;
    sl_world *w = sl_world_create(&d);
    sl_object bar = sl_softbody_create_box(w, solid(w), (sl_vec3){0, 0, 0}, (sl_vec3){0.1f * 125, 0.1f, 0.1f}, 1.0f, 0);
    const sl_particle *ids;
    int n = sl_object_particles(w, bar, &ids);
    CHECK(n == 125, "bar has %d particles", n);
    float before = sl_position(w, ids[1]).y;
    sl_set_velocity(w, ids[0], (sl_vec3){0, 4, 0});
    steps(w, 15);
    float moved = sl_position(w, ids[1]).y - before;
    CHECK(moved > 0.02f, "neighbor of a kicked end moved only %f", moved);
    sl_world_destroy(w);
}

/* A removed particle's id stays dead even when its slot is handed out again. */
static void test_stale_ids(void) {
    sl_world *w = make_world(100);
    sl_material m = sand(w);
    sl_particle a = sl_spawn(w, m, (sl_vec3){0, 0, 0}, (sl_vec3){0, 0, 0});
    sl_remove(w, a);
    sl_particle b = sl_spawn(w, m, (sl_vec3){5, 5, 5}, (sl_vec3){0, 0, 0});
    CHECK(b >= 0 && b != a, "new particle got the old id %d", b);
    CHECK(!sl_alive(w, a), "removed id is alive again");
    CHECK(sl_position(w, a).x == 0, "removed id reads the new particle");
    CHECK(sl_remove(w, a) == 0 && sl_alive(w, b), "removing a stale id hit the new particle");
    CHECK(sl_grab_begin(w, a) == 0, "grabbed through a stale id");

    sl_clear(w);
    sl_particle c = sl_spawn(w, m, (sl_vec3){1, 1, 1}, (sl_vec3){0, 0, 0});
    CHECK(c >= 0 && !sl_alive(w, b) && sl_alive(w, c), "id from before sl_clear still alive");

    sl_object rope = sl_rope_create(w, m, (sl_vec3){0, 1, 0}, (sl_vec3){1, 1, 0}, 0);
    const sl_particle *ids;
    int n = sl_object_particles(w, rope, &ids);
    sl_particle end = ids[n - 1];
    sl_object_destroy(w, rope);
    for (int k = 0; k < n; k++) sl_spawn(w, m, (sl_vec3){(float)k, 3, 0}, (sl_vec3){0, 0, 0});
    CHECK(!sl_alive(w, end), "rope id came back to life");
    sl_world_destroy(w);
}

/* Plane colliders turn with their rotation: a floor turned 90 degrees about z is a wall facing -x. */
static void test_plane_rotation(void) {
    float turn[4] = {0, 0, 0.70710678f, 0.70710678f};
    for (int how = 0; how < 2; how++) {
        sl_world *w = make_world(10);
        sl_collider_desc c = {0};
        c.shape = SL_PLANE;
        c.normal = (sl_vec3){0, 1, 0};
        if (how == 0) memcpy(c.rotation, turn, sizeof turn);
        sl_collider wall = sl_collider_add(w, &c);
        if (how == 1) sl_collider_move(w, wall, (sl_vec3){0, 0, 0}, turn);
        sl_particle p = sl_spawn(w, sand(w), (sl_vec3){-0.5f, 0.5f, 0}, (sl_vec3){3, 0, 0});
        steps(w, 60);
        sl_vec3 at = sl_position(w, p);
        CHECK(at.x < -R * 0.9f, "%s: particle went through the wall to x=%f", how ? "move" : "add", at.x);
        CHECK(at.y < -1.0f, "%s: old floor still holds at y=%f", how ? "move" : "add", at.y);
        sl_world_destroy(w);
    }
}

/* Values that would break the solver are refused instead of simulated. */
static void test_validation(void) {
    sl_world *w = make_world(10);
    sl_material_desc bad[] = {
        {.kind = SL_GRANULAR, .density = 1600, .damping = -1},
        {.kind = SL_GRANULAR, .density = 1600, .friction = -0.5f},
        {.kind = SL_FLUID, .density = 1000, .cohesion = -1},
        {.kind = SL_FLUID, .density = 1000, .vorticity = -1},
        {.kind = SL_FLUID, .density = 1000, .viscosity = -1},
        {.kind = SL_GRANULAR, .density = 1600, .wet_cohesion = -1},
        {.kind = SL_GRANULAR, .density = -5},
        {.kind = SL_GRANULAR, .density = 1600, .friction = NAN},
        {.kind = (sl_kind)7, .density = 1000},
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) CHECK(sl_material_add(w, &bad[i]) == -1, "bad material %d accepted", (int)i);
    CHECK(sand(w) >= 0, "good material refused");

    sl_collider_desc cbad[] = {
        {.shape = (sl_shape)9},
        {.shape = SL_SPHERE, .radius = -1},
        {.shape = SL_SPHERE, .radius = 1, .position = {NAN, 0, 0}},
        {.shape = SL_BOX, .half_extents = {1, -1, 1}},
        {.shape = SL_CAPSULE, .radius = 0.1f, .half_extents = {0, INFINITY, 0}},
        {.shape = SL_PLANE, .normal = {0, NAN, 0}},
        {.shape = SL_SPHERE, .radius = 1, .rotation = {NAN, 0, 0, 1}},
        {.shape = SL_SPHERE, .radius = 1, .friction = -1},
    };
    for (size_t i = 0; i < sizeof cbad / sizeof cbad[0]; i++) CHECK(sl_collider_add(w, &cbad[i]) == -1, "bad collider %d accepted", (int)i);
    sl_collider ball = sl_collider_add(w, &(sl_collider_desc){.shape = SL_SPHERE, .radius = 0.5f});
    CHECK(ball >= 0, "good collider refused");
    float nan_rot[4] = {NAN, 0, 0, 1};
    sl_collider_move(w, ball, (sl_vec3){0, 0, 0}, nan_rot);
    sl_particle p = sl_spawn(w, 0, (sl_vec3){0, 0.6f, 0}, (sl_vec3){0, 0, 0});
    steps(w, 10);
    CHECK(all_finite(w) && sl_position(w, p).y > 0.5f, "a NaN rotation broke the collider: y=%f", sl_position(w, p).y);
    sl_world_destroy(w);
}

typedef struct { const char *name; void (*fn)(void); } test;

int main(int argc, char **argv) {
    test tests[] = {
        {"free fall", test_free_fall},
        {"settle on floor", test_settle_on_floor},
        {"container holds", test_container_holds},
        {"fluid levels out", test_fluid_levels_out},
        {"sand piles, fluid spreads", test_sand_piles_fluid_spreads},
        {"moving sphere pushes", test_moving_sphere_pushes},
        {"determinism", test_determinism},
        {"thread determinism", test_thread_determinism},
        {"custom task system", test_task_system},
        {"stable ids", test_stable_ids},
        {"sleeping", test_sleeping},
        {"rope", test_rope},
        {"cloth", test_cloth},
        {"soft body", test_softbody},
        {"buoyancy", test_buoyancy},
        {"collider force", test_collider_force},
        {"no tunneling", test_no_tunneling},
        {"large dt", test_large_dt},
        {"drop from height", test_drop_from_height},
        {"no energy gain", test_no_energy_gain},
        {"raycast", test_raycast},
        {"grab and throw", test_grab_and_throw},
        {"collider toggle", test_collider_toggle},
        {"remove sphere", test_remove_sphere},
        {"anisotropy", test_anisotropy},
        {"diffuse", test_diffuse},
        {"pile angle", test_pile_angle},
        {"wet sand", test_wet_sand},
        {"limits", test_limits},
        {"allocator", test_allocator},
        {"grab dies with particle", test_grab_dies_with_particle},
        {"fast slide on ball", test_fast_slide_on_ball},
        {"anisotropy after reorder", test_anisotropy_after_reorder},
        {"clear is fresh", test_clear_is_fresh},
        {"removed collider", test_removed_collider},
        {"neighbor search", test_neighbor_search},
        {"wet without sleep", test_wet_without_sleep},
        {"long soft body", test_long_softbody},
        {"stale ids", test_stale_ids},
        {"plane rotation", test_plane_rotation},
        {"validation", test_validation},
    };
    for (size_t i = 0; i < sizeof tests / sizeof tests[0]; i++) {
        if (argc > 1 && !strstr(tests[i].name, argv[1])) continue;
        int before = failures;
        tests[i].fn();
        printf("%s %s\n", failures == before ? "ok  " : "FAIL", tests[i].name);
        fflush(stdout);
    }
    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
