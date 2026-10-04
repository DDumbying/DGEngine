# Deterministic multiplayer

slime's identity is particle physics you can network: water, sand, cloth and soft bodies that compute the same
bits on every machine, so they can be part of lockstep and rollback multiplayer instead of client-side effects.
GPU solvers (FleX, PhysX particles, Zibra) and Obi cannot promise this, and game servers have no GPU.

## Goals

- Bit-identical results on x86-64 and ARM64, Linux, macOS and Windows, GCC, Clang and MSVC, and WebAssembly,
  for any worker count. Proven in CI by comparing a state hash after a fixed scene against a committed value.
- Lockstep and rollback: a snapshot of the state, restored on any of those platforms, continues bit-exactly.
- Snapshots are valid for the same slime version and world settings; there is no migration between versions.
- Snapshots and the hash cover gameplay state only. Spray clears and surface ellipsoids are rebuilt on restore.
- No built-in delta snapshots yet; the layout groups fields so general compressors work well.
- Determinism costs at most a few percent of step time; save and restore cost a small part of a step.

32-bit x86 without SSE2 is not supported: x87 arithmetic cannot be made to round like the other targets.

## API

```c
unsigned sl_version(void);                 /* (major << 16) | (minor << 8) | patch */
int sl_deterministic(void);                /* 1 when this build's floating point matches the reference */
size_t sl_snapshot_size(const sl_world *w);
size_t sl_snapshot_save(const sl_world *w, void *buf, size_t cap);    /* 0 if cap is too small */
sl_snapshot_result sl_snapshot_load(sl_world *w, const void *buf, size_t size);
uint64_t sl_state_hash(const sl_world *w); /* hash of exactly what a snapshot holds */
```

The caller owns snapshot memory. Particle, object and collider handles are restored exactly. Load validates
the whole buffer before touching the world and leaves it unchanged on failure. Settings that must match:
particle radius, max_particles (it fixes the id layout), substeps, iterations, fluid iterations, sleep speed and
anisotropy. Workers, task system, allocator and spray capacity may differ. Gravity, materials and colliders are
state and come from the snapshot.

## Making results identical everywhere

- Build: `-ffp-contract=off` (GCC, Clang) and `/fp:precise` (MSVC); the sources refuse `-ffast-math` and x87.
  `sl_deterministic()` evaluates expressions whose bits change under contraction or fast math.
- Math: only + - * / and sqrt are exactly rounded everywhere, so slime carries its own versions of everything
  else it uses (exp2 for contact lift, integer roots for grid sizing, repeated products and fixed-iteration roots
  for viscosity and cluster stiffness, sin and cos for rotations and spray, cbrt for ellipsoids, min and max with
  fixed signed-zero handling).
- Floating point mode: round to nearest with denormals kept is set on entry to every step and around every chunk
  a worker or a user task system runs, and the host's mode is restored after. Engines often enable flush to zero.
- No capacity dependence: anything that shapes results is a function of the state, never of buffer capacities
  (the hash grid's table size follows the particle count, not the slot capacity).

## Snapshot format

Little-endian, written field by field so struct layout never leaks in.

- Header: magic `SLIM`, format number, slime version, settings fingerprint, body size, body checksum.
- Body, in this order: gravity and step counter; materials; colliders (description, transforms of this and the
  last step, enabled and removed flags, force); particles (id allocator, then per slot id, position, velocity,
  position at the last neighbor build, mass, persistent flags, calm counter, material, wetness, object and the
  collider pushes); objects (slot, generation, kind, grid size, particle ids); distance constraints in their
  colored order; clusters with rotation and center, and members with rest offsets; grabs; queued wakes; whether
  a neighbor rebuild is pending.

Caches are not stored. Every cache is a function of the positions at the last neighbor build, so load reruns
that build and gets the same neighbor lists, contacts and coloring. This needs neighbor lists that do not depend
on history: the fluid pass keeps its in-kernel neighbors in a separate index array instead of reordering the
shared list. Sleep islands are rebuilt when next needed.

## Tests

- Rollback: step, snapshot, step more and hash; restore, step the same and compare the hash, with water, sand,
  ropes, cloth, soft bodies, sleeping, moving colliders, grabs, spawning and removal.
- Late join: load into a freshly created world with different capacity and worker count, continue, compare.
- Errors: truncated, corrupt, wrong version and wrong settings are rejected and leave the world untouched.
- Cross-platform: a fixed scene's hash is compared against `tests/golden_hash.txt` on every CI platform.

## Order of work

1. Foundation: CMake that behaves as a subproject, install and find_package, shared library exports, version
   0.6.0, CI on Linux (x86-64, ARM64), macOS, Windows and WebAssembly, the README example built as a test.
2. Snapshots and the state hash, including history-free neighbor lists and capacity independence.
3. Cross-platform determinism: build flags, own math, floating point mode, golden hash in CI.
