#include "internal.h"

static float kernel_grad(float r, float h) { return r < h ? -3.0f * (h - r) * (h - r) : 0.0f; }

static void predict(sl_world *w, float hs) {
    sl_vec3 dv = v3_scale(w->gravity, hs);
    for (int i = 0; i < w->count; i++) {
        w->v[i] = v3_add(w->v[i], dv);
        w->p[i] = v3_add(w->x[i], v3_scale(w->v[i], hs));
    }
}

static sl_vec3 pair_grad(const sl_world *w, const pair *pr, float inv_rest) {
    if (pr->r >= w->h) return v3(0, 0, 0);
    sl_vec3 d = v3_sub(w->p[pr->i], w->p[pr->j]);
    return pr->r > 1e-9f ? v3_scale(d, kernel_grad(pr->r, w->h) * inv_rest / pr->r) : v3(0, 0, 0);
}

/* Position based fluids: push fluid particles apart wherever local density exceeds rest density. */
static void solve_density(sl_world *w) {
    float h = w->h, inv_rest = 1.0f / w->w_rest;
    float eps = 0.05f / (w->spacing * w->spacing);
    float self = kernel(0, h);

    for (int i = 0; i < w->count; i++) {
        w->grad2[i] = 0;
        if (w->fluid[i]) {
            w->rho[i] = self + wall_density(w, i, &w->wall_grad[i]);
            w->grad[i] = v3_scale(w->wall_grad[i], inv_rest);
        } else {
            w->rho[i] = self;
            w->grad[i] = w->wall_grad[i] = v3(0, 0, 0);
        }
    }
    for (int k = 0; k < w->pair_count; k++) {
        pair *pr = &w->pairs[k];
        int i = pr->i, j = pr->j;
        float r2 = v3_len2(v3_sub(w->p[i], w->p[j]));
        pr->r = r2 < h * h ? sqrtf(r2) : h;
        if (pr->r >= h) continue;
        float wk = kernel(pr->r, h);
        w->rho[i] += wk;
        w->rho[j] += wk;
        sl_vec3 g = pair_grad(w, pr, inv_rest);
        float g2 = v3_len2(g);
        w->grad[i] = v3_add(w->grad[i], g);
        w->grad[j] = v3_sub(w->grad[j], g);
        if (w->fluid[j]) w->grad2[i] += g2;
        if (w->fluid[i]) w->grad2[j] += g2;
    }
    for (int i = 0; i < w->count; i++) {
        float c = w->rho[i] * inv_rest - 1.0f;
        w->lambda[i] = w->fluid[i] && c > 0 ? -c / (w->grad2[i] + v3_len2(w->grad[i]) + eps) : 0.0f;
        w->delta[i] = v3_scale(w->wall_grad[i], w->lambda[i] * inv_rest);
    }
    for (int k = 0; k < w->pair_count; k++) {
        const pair *pr = &w->pairs[k];
        int i = pr->i, j = pr->j;
        if (pr->r >= h || (!w->fluid[i] && !w->fluid[j])) continue;
        sl_vec3 g = pair_grad(w, pr, inv_rest);
        float li = w->fluid[i] ? w->lambda[i] : 0.0f, lj = w->fluid[j] ? w->lambda[j] : 0.0f;
        if (w->fluid[i]) w->delta[i] = v3_add(w->delta[i], v3_scale(g, li + lj));
        if (w->fluid[j]) w->delta[j] = v3_sub(w->delta[j], v3_scale(g, li + lj));
    }
    for (int i = 0; i < w->count; i++) w->p[i] = v3_add(w->p[i], w->delta[i]);
}

/* Non-penetration and friction for any pair that involves a grain. */
static void solve_contacts(sl_world *w) {
    float d0 = w->spacing, d02 = d0 * d0;
    for (int k = 0; k < w->contact_count; k++) {
        int i = w->contacts[k].i, j = w->contacts[k].j;
        sl_vec3 d = v3_sub(w->p[i], w->p[j]);
        float r2 = v3_len2(d);
        if (r2 >= d02 || r2 < 1e-18f) continue;
        float r = sqrtf(r2);

        sl_vec3 n = v3_scale(d, 1.0f / r);
        w->touch[i] = w->touch[j] = 1;
        float wi = w->inv_mass[i] * w->contacts[k].r, wj = w->inv_mass[j], ws = wi + wj;
        float pen = d0 - r;
        w->p[i] = v3_add(w->p[i], v3_scale(n, pen * wi / ws));
        w->p[j] = v3_sub(w->p[j], v3_scale(n, pen * wj / ws));

        float mu = 0.5f * (w->materials[w->mat[i]].friction + w->materials[w->mat[j]].friction);
        sl_vec3 rel = v3_sub(v3_sub(w->p[i], w->x[i]), v3_sub(w->p[j], w->x[j]));
        sl_vec3 tan = v3_sub(rel, v3_scale(n, v3_dot(rel, n)));
        float tl = v3_len(tan);
        if (tl <= 1e-9f) continue;
        float f = tl < mu * pen ? 1.0f : fminf(mu * pen / tl, 1.0f);
        sl_vec3 corr = v3_scale(tan, f);
        w->p[i] = v3_sub(w->p[i], v3_scale(corr, wi / ws));
        w->p[j] = v3_add(w->p[j], v3_scale(corr, wj / ws));
    }
}

static void update_velocities(sl_world *w, float hs) {
    float vmax = w->spacing / hs, inv = 1.0f / hs;
    for (int i = 0; i < w->count; i++) {
        sl_vec3 v = v3_scale(v3_sub(w->p[i], w->x[i]), inv);
        float s = v3_len(v);
        if (s > vmax) v = v3_scale(v, vmax / s);
        w->v[i] = v;
        w->x[i] = w->p[i];
    }
}

/* XSPH viscosity and cohesion, both only between fluid particles. */
static void fluid_velocity(sl_world *w, float hs) {
    int visc = 0, cohesion = 0;
    for (int m = 0; m < w->material_count; m++) {
        if (w->materials[m].kind != SL_FLUID) continue;
        visc |= w->materials[m].viscosity > 0;
        cohesion |= w->materials[m].cohesion > 0;
    }
    if (!visc && !cohesion) return;

    float h = w->h, d0 = w->spacing, inv_rest = 1.0f / w->w_rest;
    float band = (h - d0) * 0.5f;
    for (int i = 0; i < w->count; i++) w->delta[i] = w->v[i];
    for (int k = 0; k < w->pair_count; k++) {
        int i = w->pairs[k].i, j = w->pairs[k].j;
        if (w->pairs[k].r >= h || !w->fluid[i] || !w->fluid[j]) continue;
        const sl_material_desc *mi = &w->materials[w->mat[i]], *mj = &w->materials[w->mat[j]];
        sl_vec3 dv = v3_scale(v3_sub(w->v[j], w->v[i]), kernel(w->pairs[k].r, h) * inv_rest);
        w->delta[i] = v3_add(w->delta[i], v3_scale(dv, mi->viscosity));
        w->delta[j] = v3_sub(w->delta[j], v3_scale(dv, mj->viscosity));

        if (!cohesion) continue;
        sl_vec3 d = v3_sub(w->x[j], w->x[i]);
        float r = v3_len(d);
        if (r <= d0 || r >= h) continue;
        sl_vec3 pull = v3_scale(d, hs * (r - d0) * (h - r) / (band * band * r));
        w->delta[i] = v3_add(w->delta[i], v3_scale(pull, mi->cohesion));
        w->delta[j] = v3_sub(w->delta[j], v3_scale(pull, mj->cohesion));
    }
    for (int i = 0; i < w->count; i++) w->v[i] = w->delta[i];
}

void solve_substep(sl_world *w, float hs, float t) {
    predict(w, hs);
    collider_frames(w, t);
    solve_density(w);
    for (int k = 0; k < w->iterations; k++) {
        solve_contacts(w);
        collide_particles(w);
    }
    update_velocities(w, hs);
    fluid_velocity(w, hs);
}
