#ifndef DGE_REGISTRY_H
#define DGE_REGISTRY_H

#include <stdbool.h>
#include "entity.h"
#include "components.h"

/*  Storage model: each component type is a flat array of MAX_ENTITIES
    slots, index == entity id, plus a parallel "has" bitset. This is the
    simplest possible sparse set — no packing/swap-removal, no generic
    container, no macros generating type-erased arrays. It wastes some
    memory (every entity reserves a slot in every component array whether
    it uses it or not) but at MAX_ENTITIES=4096 that's a few hundred KB,
    and you can read exactly what's happening with no indirection.
    Revisit if entity counts grow into the hundreds of thousands. */
typedef struct {
    bool alive[MAX_ENTITIES];
    Entity free_list[MAX_ENTITIES];
    int free_count;
    int high_water; /* one past the highest id ever handed out */

    /* Bumped every time the slot is destroyed. Lets an EntityHandle
       captured in a previous frame (e.g. the editor's selection)
       notice its slot was recycled instead of silently reading
       whatever entity now lives there. */
    unsigned int generation[MAX_ENTITIES];

    bool has_transform[MAX_ENTITIES];
    TransformComponent transform[MAX_ENTITIES];

    bool has_renderable[MAX_ENTITIES];
    RenderableComponent renderable[MAX_ENTITIES];

    bool has_health[MAX_ENTITIES];
    HealthComponent health[MAX_ENTITIES];

    bool has_resource[MAX_ENTITIES];
    ResourceComponent resource[MAX_ENTITIES];

    bool has_move[MAX_ENTITIES];
    MoveComponent move[MAX_ENTITIES];

    bool has_task[MAX_ENTITIES];
    TaskComponent task[MAX_ENTITIES];

    bool has_construction[MAX_ENTITIES];
    ConstructionComponent construction[MAX_ENTITIES];

    bool has_definition[MAX_ENTITIES];
    DefinitionComponent definition[MAX_ENTITIES];
} Registry;

void registry_init(Registry *r);

/* Returns ENTITY_NULL if MAX_ENTITIES is exhausted. */
Entity entity_create(Registry *r);
void   entity_destroy(Registry *r, Entity e);
bool   entity_alive(const Registry *r, Entity e);

/* Capture a handle that stays valid (and detectably invalid) across
   frames, unlike a bare Entity. */
EntityHandle entity_to_handle(const Registry *r, Entity e);
bool         entity_handle_valid(const Registry *r, EntityHandle h);

/* Component accessors. add() overwrites if already present.
   get() returns NULL if the entity doesn't have that component. */
void entity_add_transform(Registry *r, Entity e, TransformComponent t);
TransformComponent *entity_get_transform(Registry *r, Entity e);

void entity_add_renderable(Registry *r, Entity e, RenderableComponent c);
RenderableComponent *entity_get_renderable(Registry *r, Entity e);

void entity_add_health(Registry *r, Entity e, HealthComponent h);
HealthComponent *entity_get_health(Registry *r, Entity e);

void entity_add_resource(Registry *r, Entity e, ResourceComponent rc);
ResourceComponent *entity_get_resource(Registry *r, Entity e);

void entity_add_move(Registry *r, Entity e, MoveComponent m);
MoveComponent *entity_get_move(Registry *r, Entity e);

void entity_add_task(Registry *r, Entity e, TaskComponent t);
TaskComponent *entity_get_task(Registry *r, Entity e);

void entity_add_construction(Registry *r, Entity e, ConstructionComponent c);
ConstructionComponent *entity_get_construction(Registry *r, Entity e);

void entity_add_definition(Registry *r, Entity e, DefinitionComponent d);
DefinitionComponent *entity_get_definition(Registry *r, Entity e);

/*  Binary save/load for every alive entity + its components, independent
    of world.dge — terrain and entities are versioned separately so a
    format change to one never forces a re-save of the other.

    Format (little-endian, fields written individually, same reasoning
    as world.c's save format):
      char     magic[4] = "DGEE"
      uint32   version  = 7
      uint32   count    (number of alive entities)
      then count records, each:
        uint8  component_mask   (bit0=Transform bit1=Renderable bit2=Health
                                  bit3=Resource bit4=Move bit5=Task
                                  bit6=Construction bit7=Definition)
        [TransformComponent]    if bit0 set
        [RenderableComponent]   if bit1 set  (now includes sprite_id as
                                  int32 — versions <6 wrote it without
                                  sprite_id and always load as -1/none)
        [HealthComponent]       if bit2 set
        [ResourceComponent]     if bit3 set  (ResourceKind as uint8, yield_per_hit as int32)
        [MoveComponent speed]   if bit4 set  (float speed only — lerp state resets on load)
        [TaskComponent kind+tgt]if bit5 set  (TaskKind as uint8, target_x/y as int32; path recomputed)
        [ConstructionComponent] if bit6 set  (BuildingKind as uint8, build_time_total
                                  and build_time_done as float, complete as uint8,
                                  then -- versions <7 stop here, is_custom defaults
                                  false -- is_custom as uint8, def_name as a fixed
                                  OBJDEF_NAME_MAX-byte block, NUL-padded)
        [DefinitionComponent]   if bit7 set  (def_name as a fixed
                                  OBJDEF_NAME_MAX-byte block, NUL-padded)

    load fully replaces the registry's contents (same contract as
    world_load): on failure the registry is left untouched. Entity ids
    are NOT preserved across a save/load round-trip — load recreates
    fresh entities, so any EntityHandle held before a load is stale
    afterward (entity_handle_valid() will correctly say so). */
bool registry_save(const Registry *r, const char *path);
bool registry_load(Registry *r, const char *path);

#endif /* DGE_REGISTRY_H */
