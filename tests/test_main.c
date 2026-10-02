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
    sl_world *a = mixed_scene(1), *b = mixed_scene(4);
    CHECK(same_state(a, b), "1 and 4 workers differ");
    sl_world_destroy(a);
    sl_world_destroy(b);
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
    CHECK(soft > 0.8f, "soft body did not recover: %f", soft);
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
        {"limits", test_limits},
        {"allocator", test_allocator},
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
