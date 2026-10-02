#include "internal.h"

static float kernel_grad(float r, float h) { return r < h ? -3.0f * (h - r) * (h - r) : 0.0f; }

static void predict(sl_world *w, int begin, int end, int chunk, void *ctx) {
    (void)chunk; (void)ctx;
    sl_vec3 dv = v3_scale(w->gravity, w->hs);
    for (int k = begin; k < end; k++) {
        int i = w->active[k];
        if (!(w->flags[i] & F_KINEMATIC)) w->v[i] = v3_add(w->v[i], dv);
        w->p[i] = v3_madd(w->x[i], w->v[i], w->hs);
    }
}

/* Position based fluids: lambda is how hard each fluid particle pushes to get back to rest density. */
static void fluid_lambda(sl_world *w, int begin, int end, int chunk, void *ctx) {
    (void)chunk; (void)ctx;
    float h = w->h, h2 = h * h, inv_rest = 1.0f / w->w_rest, eps = 0.2f / (w->spacing * w->spacing);
    for (int k = begin; k < end; k++) {
        int i = w->active[k];
        w->lambda[i] = 0;
        if (!(w->flags[i] & F_FLUID)) continue;
        sl_vec3 grad_i, pi = w->p[i];
        float rho = kernel(0, h) + wall_density(w, i, &grad_i, 0), grad2 = 0;
        grad_i = v3_scale(grad_i, inv_rest);
        for (int n = w->nbr_off[i]; n < w->nbr_off[i + 1]; n++) {
            int j = w->nbr[n];
            sl_vec3 d = v3_sub(pi, w->p[j]);
            float r2 = v3_len2(d);
            if (r2 >= h2) { w->nbr_r[n] = h; continue; }
            float r = sqrtf(r2);
            w->nbr_r[n] = r;
            rho += kernel(r, h);
            if (r < 1e-9f) continue;
            sl_vec3 g = v3_scale(d, kernel_grad(r, h) * inv_rest / r);
            grad_i = v3_add(grad_i, g);
            if (w->flags[j] & F_FLUID) grad2 += v3_len2(g);
        }
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
                if (r2 >= h2 || r2 < 1e-18f || w->lambda[j] == 0) continue;
                float r = sqrtf(r2);
                dp = v3_madd(dp, d, w->mass[j] / w->mass[i] * w->lambda[j] * kernel_grad(r, h) * inv_rest / r);
            }
        } else {
            float li = w->lambda[i];
            if (li != 0) {
                sl_vec3 wall;
                wall_density(w, i, &wall, li * inv_rest * w->mass[i] / w->hs);
                dp = v3_scale(wall, li * inv_rest);
            }
            for (int n = w->nbr_off[i]; n < w->nbr_off[i + 1]; n++) {
                int j = w->nbr[n];
                float r = w->nbr_r[n];
                if (r >= h || r < 1e-9f || (!(w->flags[j] & F_FLUID) && li == 0)) continue;
                sl_vec3 d = v3_sub(pi, w->p[j]);
                float l = li + ((w->flags[j] & F_FLUID) ? w->lambda[j] : 0.0f);
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
    float d0 = w->spacing;
    for (int k = base + begin; k < base + end; k++) {
        const contact *c = &w->contacts[k];
        int i = c->i, j = c->j;
        if (!(w->flags[i] & F_AWAKE)) continue;
        const sl_material_desc *mi = &w->materials[w->mat[i]], *mj = &w->materials[w->mat[j]];
        float dist = d0, mu = 0.5f * (mi->friction + mj->friction), glue = 0;
        if (w->wet[i] && w->wet[j]) glue = 0.5f * (mi->wet_cohesion + mj->wet_cohesion) * (float)(w->wet[i] < w->wet[j] ? w->wet[i] : w->wet[j]) / 255.0f;
        if (w->obj[i] >= 0 && w->obj[i] == w->obj[j]) {
            dist = w->objects[w->obj[i]].self_dist;
            if (w->objects[w->obj[i]].kind == OBJ_SOFT) mu = 0;
        }
        /* Fluid against solids: no friction, and a little closer, so water can flow between grains. */
        if ((w->flags[i] | w->flags[j]) & F_FLUID) { mu = 0; dist = 0.8f * d0; }
        sl_vec3 d = v3_sub(w->p[i], w->p[j]);
        float r2 = v3_len2(d);
        float wi = w->inv_mass[i] * c->lift, wj = w->inv_mass[j], ws = wi + wj;
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
        w->flags[i] |= F_TOUCH | (w->flags[j] & F_FLUID ? F_WET : 0);
        w->flags[j] |= F_TOUCH | (w->flags[i] & F_FLUID ? F_WET : 0);
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
    float d0 = w->spacing;
    for (int k = base + begin; k < base + end; k++) {
        const contact *c = &w->contacts[k];
        int i = c->i, j = c->j;
        if (!(w->flags[i] & F_AWAKE) || (w->obj[i] >= 0 && w->obj[i] == w->obj[j])) continue;
        float dist = (w->flags[i] | w->flags[j]) & F_FLUID ? 0.8f * d0 : d0;
        sl_vec3 d = v3_sub(w->x[i], w->x[j]);
        float r2 = v3_len2(d), wi = w->inv_mass[i], wj = w->inv_mass[j], ws = wi + wj;
        if (r2 >= dist * dist || r2 < 1e-18f || ws <= 0) continue;
        float r = sqrtf(r2), pen = dist - r;
        sl_vec3 n = v3_scale(d, 1.0f / r);
        w->x[i] = v3_madd(w->x[i], n, pen * wi / ws);
        w->x[j] = v3_madd(w->x[j], n, -pen * wj / ws);
    }
}

void stabilize(sl_world *w) {
    for (int color = 0; color <= SL_MAX_COLORS; color++) {
        int base = w->color_off[color], count = w->color_off[color + 1] - base;
        if (count <= 0) continue;
        if (color == SL_MAX_COLORS || count < SL_CHUNK) stabilize_range(w, 0, count, 0, &base);
        else sl__parallel(w, count, stabilize_range, &base);
    }
}

static void solve_contacts(sl_world *w) {
    for (int color = 0; color <= SL_MAX_COLORS; color++) {
        int base = w->color_off[color], count = w->color_off[color + 1] - base;
        if (count <= 0) continue;
        /* The overflow color has shared particles, so it runs on one thread. */
        if (color == SL_MAX_COLORS || count < SL_CHUNK) solve_contact_range(w, 0, count, 0, &base);
        else sl__parallel(w, count, solve_contact_range, &base);
    }
}

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
    (void)chunk; (void)ctx;
    float h = w->h, h2 = h * h, d0 = w->spacing, inv_rest = 1.0f / w->w_rest, band = (h - d0) * 0.5f;
    float dt = w->dt;
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
            if (vort && r > 1e-9f) eta = v3_madd(eta, d, -(v3_len(w->tmp[j]) - wi) * kernel_grad(r, h) * inv_rest / r);
            if (coh && r > d0) pull = v3_madd(pull, d, (r - d0) * (h - r) / (band * band * r));
        }
        float visc_step = 1.0f - powf(1.0f - fminf(m->viscosity, 1.0f), (float)w->substeps);
        sl_vec3 v = v3_madd(w->v[i], visc, visc_step);
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
void move_grabs(sl_world *w, float t) {
    for (int g = 0; g < w->grab_count; g++) {
        int id = w->grabs[g].id, s = id >= 0 && id < w->next_id ? w->id_slot[id] : -1;
        if (s >= 0 && s < w->count && w->id[s] == id) w->p[s] = v3_lerp(w->grabs[g].from, w->grabs[g].to, t);
    }
}

void solve_substep(sl_world *w, float t) {
    int n = w->active_count;
    collider_frames(w, t);
    sl__parallel(w, n, predict, NULL);
    move_grabs(w, t);

    int fluids = 0;
    for (int m = 0; m < w->material_count; m++) fluids |= w->materials[m].kind == SL_FLUID;
    objects_substep(w);
    for (int it = 0; fluids && it < w->fluid_iterations; it++) {
        sl__parallel(w, n, fluid_lambda, NULL);
        sl__parallel(w, n, fluid_delta, NULL);
        sl__parallel(w, n, apply_delta, NULL);
    }
    /* Without grain contacts or objects there is nothing to iterate, so colliders need a single pass. */
    int passes = w->contact_count || w->dist_count || w->cluster_count ? w->iterations : 1;
    for (int k = 0; k < passes; k++) {
        solve_contacts(w);
        objects_solve(w);
        solve_colliders(w);
    }
    sl__parallel(w, n, update_velocities, NULL);
}

void fluid_step(sl_world *w) {
    int extras = 0, vort = 0, n = w->active_count;
    for (int m = 0; m < w->material_count; m++) {
        const sl_material_desc *md = &w->materials[m];
        if (md->kind != SL_FLUID) continue;
        extras |= md->viscosity > 0 || md->cohesion > 0 || md->vorticity > 0;
        vort |= md->vorticity > 0;
    }
    if (!extras) return;
    if (vort) sl__parallel(w, n, fluid_curl, NULL);
    sl__parallel(w, n, fluid_velocity, NULL);
    sl__parallel(w, n, copy_velocity, NULL);
}
