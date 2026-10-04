#include "internal.h"

static void predict(sl_world *w, int begin, int end, int chunk, void *ctx) {
    (void)chunk; (void)ctx;
    sl_vec3 dv = v3_scale(w->gravity, w->hs);
    for (int k = begin; k < end; k++) {
        int i = w->active[k];
        if (!(w->flags[i] & F_KINEMATIC)) w->v[i] = v3_add(w->v[i], dv);
        w->p[i] = v3_madd(w->x[i], w->v[i], w->hs);
    }
}

/* Position based fluids: lambda is how hard each fluid particle pushes to get back to rest density.
   Neighbors inside the kernel are moved to the front of the list with their distance, and w->order[i]
   marks where they end, so the delta pass only visits those. */
static void fluid_lambda(sl_world *w, int begin, int end, int chunk, void *ctx) {
    (void)chunk; (void)ctx;
    float h = w->h, h2 = h * h, inv_rest = 1.0f / w->w_rest, eps = 0.2f / (w->spacing * w->spacing);
    for (int k = begin; k < end; k++) {
        int i = w->active[k];
        w->lambda[i] = 0;
        if (!(w->flags[i] & F_FLUID)) continue;
        sl_vec3 grad_i, pi = w->p[i];
        float rho = kernel(0, h) + sl__wall_density(w, i, &grad_i, 0), grad2 = 0;
        grad_i = v3_scale(grad_i, inv_rest);
        int in = w->nbr_off[i];
        for (int n = in; n < w->nbr_off[i + 1]; n++) {
            int j = w->nbr[n];
            sl_vec3 d = v3_sub(pi, w->p[j]);
            float r2 = v3_len2(d);
            if (r2 >= h2) continue;
            float r = sqrtf(r2);
            if (n != in) { w->nbr[n] = w->nbr[in]; w->nbr[in] = j; }
            w->nbr_r[in++] = r;
            rho += kernel(r, h);
            if (r < 1e-9f) continue;
            sl_vec3 g = v3_scale(d, kernel_grad(r, h) * inv_rest / r);
            grad_i = v3_add(grad_i, g);
            if (w->flags[j] & F_FLUID) grad2 += v3_len2(g);
        }
        w->order[i] = in;
        float c = rho * inv_rest - 1.0f;
        if (c > 0) w->lambda[i] = -c / (grad2 + v3_len2(grad_i) + eps);
    }
}

/* Pressure push from lambda, plus a minimum spacing so two fluid particles never collapse onto one spot.
   Solids get the opposite of the push their fluid neighbors take from them, which is buoyancy. */
static void fluid_delta(sl_world *w, int begin, int end, int chunk, void *ctx) {
    (void)ctx; (void)chunk;
    float h = w->h, h2 = h * h, inv_rest = 1.0f / w->w_rest;
    float near = 0.5f * w->spacing, limit = 0.5f * w->spacing;
    for (int k = begin; k < end; k++) {
        int i = w->active[k];
        w->delta[i] = v3(0, 0, 0);
        sl_vec3 dp = v3(0, 0, 0), pi = w->p[i];
        if (!(w->flags[i] & F_FLUID)) {
            if (w->flags[i] & F_KINEMATIC) continue;
            float wet2 = 1.44f * w->spacing * w->spacing;
            /* Other threads read flags[i] in this pass, so wetness goes to its own byte first. */
            w->near_fluid[i] = 0;
            for (int n = w->nbr_off[i]; n < w->nbr_off[i + 1]; n++) {
                int j = w->nbr[n];
                if (!(w->flags[j] & F_FLUID)) continue;
                sl_vec3 d = v3_sub(pi, w->p[j]);
                float r2 = v3_len2(d);
                if (r2 < wet2) w->near_fluid[i] = 1;
                float lj = live_lambda(w, j);
                if (r2 >= h2 || r2 < 1e-18f || lj == 0) continue;
                float r = sqrtf(r2);
                dp = v3_madd(dp, d, w->mass[j] / w->mass[i] * lj * kernel_grad(r, h) * inv_rest / r);
            }
        } else {
            float li = w->lambda[i];
            if (li != 0) {
                sl_vec3 wall;
                sl__wall_density(w, i, &wall, li * inv_rest * w->mass[i] / w->hs);
                dp = v3_scale(wall, li * inv_rest);
            }
            for (int n = w->nbr_off[i]; n < w->order[i]; n++) {
                int j = w->nbr[n];
                float r = w->nbr_r[n];
                if (r < 1e-9f || (!(w->flags[j] & F_FLUID) && li == 0)) continue;
                sl_vec3 d = v3_sub(pi, w->p[j]);
                float l = li + ((w->flags[j] & F_FLUID) ? live_lambda(w, j) : 0.0f);
                dp = v3_madd(dp, d, l * kernel_grad(r, h) * inv_rest / r);
                if (r < near && (w->flags[j] & F_FLUID)) dp = v3_madd(dp, d, 0.5f * (near - r) / r);
            }
        }
        float len = v3_len(dp);
        w->delta[i] = len > limit ? v3_scale(dp, limit / len) : dp;
    }
}

static void apply_delta(sl_world *w, int begin, int end, int chunk, void *ctx) {
    (void)chunk; (void)ctx;
    for (int k = begin; k < end; k++) {
        int i = w->active[k];
        if (!(w->flags[i] & F_KINEMATIC)) w->p[i] = v3_add(w->p[i], w->delta[i]);
        if (!(w->flags[i] & F_FLUID) && w->near_fluid[i]) w->flags[i] |= F_WET;
    }
}

/* Non-penetration and friction for pairs that involve a grain or a solid; one color at a time. */
static void solve_contact_range(sl_world *w, int begin, int end, int chunk, void *ctx) {
    (void)chunk;
    int base = *(int *)ctx;
    for (int k = base + begin; k < base + end; k++) {
        const contact *c = &w->contacts[k];
        int i = c->i, j = c->j;
        if (!((w->flags[i] | w->flags[j]) & F_AWAKE)) continue;
        float dist = c->dist, mu = c->mu, glue = 0;
        if (w->wet[i] && w->wet[j]) glue = c->glue * (float)(w->wet[i] < w->wet[j] ? w->wet[i] : w->wet[j]);
        sl_vec3 d = v3_sub(w->p[i], w->p[j]);
        float r2 = v3_len2(d);
        float wi = live_inv_mass(w, i) * c->lift, wj = live_inv_mass(w, j), ws = wi + wj;
        if (r2 < 1e-18f || ws <= 0) continue;
        if (r2 >= dist * dist) {
            /* Wet grains a little apart pull together, which is what lets wet sand clump and hold a shape. */
            if (glue > 0 && r2 < 1.5625f * dist * dist) {
                float r = sqrtf(r2), pull = 0.25f * glue * (r - dist) / r;
                w->p[i] = v3_madd(w->p[i], d, -pull * wi / ws);
                w->p[j] = v3_madd(w->p[j], d, pull * wj / ws);
            }
            continue;
        }
        float r = sqrtf(r2), pen = dist - r;
        sl_vec3 n = v3_scale(d, 1.0f / r);
        /* Written only when they change: flags of nearby particles share cache lines across threads. */
        unsigned char fi = F_TOUCH | (w->flags[j] & F_FLUID ? F_WET : 0), fj = F_TOUCH | (w->flags[i] & F_FLUID ? F_WET : 0);
        if (wi > 0 && (w->flags[i] & fi) != fi) w->flags[i] |= fi;
        if (wj > 0 && (w->flags[j] & fj) != fj) w->flags[j] |= fj;
        w->p[i] = v3_madd(w->p[i], n, pen * wi / ws);
        w->p[j] = v3_madd(w->p[j], n, -pen * wj / ws);

        sl_vec3 rel = v3_sub(v3_sub(w->p[i], w->x[i]), v3_sub(w->p[j], w->x[j]));
        sl_vec3 tan = v3_sub(rel, v3_scale(n, v3_dot(rel, n)));
        float tl = v3_len(tan);
        if (tl <= 1e-9f) continue;
        float f = tl < mu * pen ? 1.0f : fminf(mu * pen / tl, 1.0f);
        w->p[i] = v3_madd(w->p[i], tan, -f * wi / ws);
        w->p[j] = v3_madd(w->p[j], tan, f * wj / ws);
    }
}

/* Overlap that already exists at the start of a step is removed in position only, so spawning grains
   into each other cannot launch them; this is pre-stabilization from Macklin's unified particle paper. */
static void stabilize_range(sl_world *w, int begin, int end, int chunk, void *ctx) {
    (void)chunk;
    int base = *(int *)ctx;
    for (int k = base + begin; k < base + end; k++) {
        const contact *c = &w->contacts[k];
        int i = c->i, j = c->j;
        if (w->obj[i] >= 0 && w->obj[i] == w->obj[j]) continue;
        float dist = c->dist;
        sl_vec3 d = v3_sub(w->x[i], w->x[j]);
        float r2 = v3_len2(d), wi = live_inv_mass(w, i), wj = live_inv_mass(w, j), ws = wi + wj;
        if (r2 >= dist * dist || r2 < 1e-18f || ws <= 0) continue;
        float r = sqrtf(r2), pen = dist - r;
        sl_vec3 n = v3_scale(d, 1.0f / r);
        w->x[i] = v3_madd(w->x[i], n, pen * wi / ws);
        w->x[j] = v3_madd(w->x[j], n, -pen * wj / ws);
    }
}

/* Runs fn over each color in turn; ctx is the color's first index. Nothing in a color shares a particle,
   so small chunks spread even short colors over every thread. The overflow color runs on one thread. */
void sl__run_colors(sl_world *w, const int *offsets, sl_range_fn fn) {
    for (int color = 0; color <= SL_MAX_COLORS; color++) {
        int base = offsets[color], count = offsets[color + 1] - base;
        if (count <= 0) continue;
        if (color == SL_MAX_COLORS || count < 2 * SL_COLOR_CHUNK) fn(w, 0, count, 0, &base);
        else sl__parallel_sized(w, count, SL_COLOR_CHUNK, fn, &base);
    }
}

void sl__stabilize(sl_world *w) { sl__run_colors(w, w->color_off, stabilize_range); }

static void update_velocities(sl_world *w, int begin, int end, int chunk, void *ctx) {
    (void)chunk; (void)ctx;
    float vmax = w->spacing / w->hs, inv = 1.0f / w->hs;
    for (int k = begin; k < end; k++) {
        int i = w->active[k];
        sl_vec3 v = v3_scale(v3_sub(w->p[i], w->x[i]), inv);
        float s2 = v3_len2(v);
        if (s2 > vmax * vmax) v = v3_scale(v, vmax / sqrtf(s2));
        if (w->flags[i] & F_PINNED) v = v3(0, 0, 0);
        float damping = w->materials[w->mat[i]].damping;
        if (damping > 0) v = v3_scale(v, 1.0f / (1.0f + damping * w->hs));
        w->v[i] = v;
        w->x[i] = w->p[i];
    }
}

/* Vorticity of each fluid particle, kept in tmp for the force pass. */
static void fluid_curl(sl_world *w, int begin, int end, int chunk, void *ctx) {
    (void)chunk; (void)ctx;
    float h = w->h, h2 = h * h, inv_rest = 1.0f / w->w_rest;
    for (int k = begin; k < end; k++) {
        int i = w->active[k];
        sl_vec3 curl = v3(0, 0, 0);
        if ((w->flags[i] & F_FLUID) && w->materials[w->mat[i]].vorticity > 0)
            for (int n = w->nbr_off[i]; n < w->nbr_off[i + 1]; n++) {
                int j = w->nbr[n];
                if (!(w->flags[j] & F_FLUID)) continue;
                sl_vec3 d = v3_sub(w->x[i], w->x[j]);
                float r2 = v3_len2(d);
                if (r2 >= h2 || r2 < 1e-18f) continue;
                float r = sqrtf(r2);
                curl = v3_add(curl, v3_cross(v3_sub(w->v[j], w->v[i]), v3_scale(d, kernel_grad(r, h) * inv_rest / r)));
            }
        w->tmp[i] = curl;
    }
}

/* XSPH viscosity, vorticity confinement and cohesion between fluid particles, once per step. */
static void fluid_velocity(sl_world *w, int begin, int end, int chunk, void *ctx) {
    (void)chunk;
    float h = w->h, h2 = h * h, d0 = w->spacing, inv_rest = 1.0f / w->w_rest, band = (h - d0) * 0.5f;
    float dt = w->dt, *visc_step = ctx;
    for (int k = begin; k < end; k++) {
        int i = w->active[k];
        w->delta[i] = w->v[i];
        if (!(w->flags[i] & F_FLUID)) continue;
        const sl_material_desc *m = &w->materials[w->mat[i]];
        sl_vec3 visc = v3(0, 0, 0), pull = v3(0, 0, 0), eta = v3(0, 0, 0);
        int vort = m->vorticity > 0, coh = m->cohesion > 0;
        float wi = vort ? v3_len(w->tmp[i]) : 0;
        for (int n = w->nbr_off[i]; n < w->nbr_off[i + 1]; n++) {
            int j = w->nbr[n];
            if (!(w->flags[j] & F_FLUID)) continue;
            sl_vec3 d = v3_sub(w->x[j], w->x[i]);
            float r2 = v3_len2(d);
            if (r2 >= h2) continue;
            float r = sqrtf(r2);
            visc = v3_madd(visc, v3_sub(w->v[j], w->v[i]), kernel(r, h) * inv_rest);
            if (vort && r > 1e-9f) {
                /* tmp holds curl only for awake particles; a sleeping one is still water, with no swirl. */
                float wj = (w->flags[j] & F_AWAKE) ? v3_len(w->tmp[j]) : 0.0f;
                eta = v3_madd(eta, d, -(wj - wi) * kernel_grad(r, h) * inv_rest / r);
            }
            if (coh && r > d0) pull = v3_madd(pull, d, (r - d0) * (h - r) / (band * band * r));
        }
        sl_vec3 v = v3_madd(w->v[i], visc, visc_step[w->mat[i]]);
        v = v3_madd(v, pull, m->cohesion * dt);
        float el = v3_len(eta);
        if (m->vorticity > 0 && el > 1e-9f)
            v = v3_madd(v, v3_cross(v3_scale(eta, 1.0f / el), w->tmp[i]), m->vorticity * dt);
        w->delta[i] = v;
    }
}

static void copy_velocity(sl_world *w, int begin, int end, int chunk, void *ctx) {
    (void)chunk; (void)ctx;
    for (int k = begin; k < end; k++) w->v[w->active[k]] = w->delta[w->active[k]];
}

/* Grabbed particles sweep from last step's target to the new one, so they move smoothly and can be thrown. */
void sl__move_grabs(sl_world *w, float t) {
    for (int g = 0; g < w->grab_count; g++) {
        int s = sl__slot_of(w, w->grabs[g].id);
        if (s >= 0) w->p[s] = v3_lerp(w->grabs[g].from, w->grabs[g].to, t);
    }
}

void sl__solve_substep(sl_world *w, float t) {
    int n = w->active_count;
    PROF(P_PREDICT, sl__collider_frames(w, t); sl__parallel(w, n, predict, NULL); sl__move_grabs(w, t));

    int fluids = 0;
    for (int m = 0; m < w->material_count; m++) fluids |= w->materials[m].kind == SL_FLUID;
    sl__objects_substep(w);
    for (int it = 0; fluids && it < w->fluid_iterations; it++) {
        PROF(P_LAMBDA, sl__parallel(w, n, fluid_lambda, NULL));
        PROF(P_DELTA, sl__parallel(w, n, fluid_delta, NULL));
        PROF(P_APPLY, sl__parallel(w, n, apply_delta, NULL));
    }
    /* Without grain contacts or objects there is nothing to iterate, so colliders need a single pass. */
    int passes = w->contact_count || w->dist_count || w->cluster_count ? w->iterations : 1;
    for (int k = 0; k < passes; k++) {
        PROF(P_SOLIDS, sl__run_colors(w, w->color_off, solve_contact_range));
        PROF(P_OBJECTS, sl__objects_solve(w));
        PROF(P_COLLIDERS, sl__solve_colliders(w));
    }
    PROF(P_VELOCITY, sl__parallel(w, n, update_velocities, NULL));
}

void sl__fluid_step(sl_world *w) {
    int extras = 0, vort = 0, n = w->active_count;
    float visc_step[SL_MAX_MATERIALS];
    for (int m = 0; m < w->material_count; m++) {
        const sl_material_desc *md = &w->materials[m];
        /* Viscosity is applied once per step, so it is compounded over the substeps it stands for. */
        visc_step[m] = 1.0f - powf(1.0f - fminf(fmaxf(md->viscosity, 0.0f), 1.0f), (float)w->substeps);
        if (md->kind != SL_FLUID) continue;
        extras |= md->viscosity > 0 || md->cohesion > 0 || md->vorticity > 0;
        vort |= md->vorticity > 0;
    }
    if (!extras) return;
    if (vort) sl__parallel(w, n, fluid_curl, NULL);
    sl__parallel(w, n, fluid_velocity, visc_step);
    sl__parallel(w, n, copy_velocity, NULL);
}
