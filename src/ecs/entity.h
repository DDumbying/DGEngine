#ifndef DGE_ENTITY_H
#define DGE_ENTITY_H

/*  An entity is just an index. Used on its own anywhere the reference
    doesn't outlive the current frame (e.g. system_render_entities
    iterates ids 0..MAX_ENTITIES fresh every frame — nothing is stored). */
typedef unsigned int Entity;

#define ENTITY_NULL ((Entity)0xFFFFFFFFu)
#define MAX_ENTITIES 4096

/*  Phase 4 (Editor) is the first thing that holds an Entity across
    frames — the currently-selected entity. That's exactly the case
    this file used to warn about: if the selected entity is destroyed
    and a new one created, the free-list can hand out the same index,
    and a bare Entity would silently now point at the wrong thing.
    EntityHandle pairs the index with the generation counter Registry
    bumps on every entity_destroy(), so a stale handle can be detected
    instead of misinterpreted. Use Entity for same-frame iteration,
    EntityHandle for anything held onto across frames. */
typedef struct {
    Entity       index;
    unsigned int generation;
} EntityHandle;

#define ENTITY_HANDLE_NULL ((EntityHandle){ENTITY_NULL, 0u})

#endif /* DGE_ENTITY_H */
