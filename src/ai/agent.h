#ifndef DGE_AGENT_H
#define DGE_AGENT_H

#include "../ecs/registry.h"
#include "../world/world.h"
#include "../simulation/simulation.h"

/*  Phase 6 — Agent System
    Updates all agents in the registry.
    Agents are entities with:
      - TransformComponent (position)
      - MoveComponent (movement progress/speed)
      - TaskComponent (active task/path)

    This system runs once per frame. It:
      - Processes current tasks (moves along path, handles harvesting)
      - Interacts with SimClock to coordinate movement/tasks in game-time.
      - Automatically re-calculates paths using the pathfinder if the
        task has an empty path but has a target (e.g. after a file load).
*/

void system_update_agents(Registry *reg, const World *world, ResourceStore *resources, float gdt, float speed_multiplier);

#endif /* DGE_AGENT_H */
