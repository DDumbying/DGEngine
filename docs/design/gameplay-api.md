# Gameplay API

What a game needs beyond stepping: change the world while it runs, ask what is where, and build
shapes other than boxes and straight lines. Everything stays deterministic and lives in snapshots.

## Runtime control

```c
void sl_set_gravity(sl_world *w, sl_vec3 gravity);
sl_vec3 sl_gravity(const sl_world *w);
int sl_material_set(sl_world *w, sl_material m, const sl_material_desc *desc);
int sl_material_get(const sl_world *w, sl_material m, sl_material_desc *out);
int sl_collider_set(sl_world *w, sl_collider c, const sl_collider_desc *desc);
int sl_remove_many(sl_world *w, const sl_particle *ids, int count);
```

- `sl_material_set` changes the material for every particle made of it, validated like
  `sl_material_add`. A new density changes their mass. A new kind changes how loose particles behave;
  particles of ropes, cloth and soft bodies stay solid. Affected particles wake.
- `sl_collider_set` replaces shape, size, normal, inside and friction at once; position and rotation
  become the target for the next step, as with `sl_collider_move`, so the change sweeps smoothly.
  Particles around the old and new shape wake.
- `sl_remove_many` removes loose particles in one pass instead of one compaction per particle.

## Queries

```c
typedef struct { int count; float mass; sl_vec3 center, velocity; } sl_query_result;
int sl_query(const sl_world *w, const sl_collider_desc *shape, unsigned materials,
             sl_particle *ids, int cap, sl_query_result *out);
```

The region is described like a collider (plane, box, sphere, capsule, rotated; `inside` flips it), so
there is one shape vocabulary. `materials` is a bit mask, 0 for all. Ids come in slot order, up to
`cap`; the full count is returned. The summary gives total mass and mass-weighted center and velocity:
enough for "is this under water", "how much sand is in the bucket" and drag or buoyancy on a game's
own bodies. A query is one pass over the particles, read-only and safe on another thread between steps.

## Custom shapes

```c
sl_object sl_rope_create_path(sl_world *w, sl_material m, const sl_vec3 *points, int count, float compliance);
sl_object sl_softbody_create(sl_world *w, sl_material m, const sl_vec3 *points, int count,
                             float stiffness, float plasticity);
```

- A rope along a polyline, resampled at particle spacing.
- A soft body from any set of particle positions, for example a mesh sampled at twice the radius.
  Clusters use the same overlapping blocks of 5 cells as the box version, over the lattice cells the
  points snap to, so any shape gets the same stiffness behavior.

## Moving colliders

Particles are swept against colliders in the collider's own frame, from the frame at the start of the
substep to the frame at its end, so a fast collider can no longer jump over a particle in one substep.
