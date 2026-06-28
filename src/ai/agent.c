#include "agent.h"
#include "pathfinder.h"
#include "../simulation/harvest.h"
#include "../simulation/construction.h"
#include "../core/log.h"
#include <stdlib.h>
#include <math.h>

void system_update_agents(Registry *reg, const World *world, SpatialGrid *sgrid,
                          ResourceStore *resources, float gdt, float speed_multiplier) {
    for (Entity e = 0; e < (Entity)MAX_ENTITIES; e++) {
        if (!reg->alive[e] || !reg->has_transform[e] || !reg->has_move[e] || !reg->has_task[e])
            continue;

        TransformComponent *t   = &reg->transform[e];
        MoveComponent      *m   = &reg->move[e];
        TaskComponent      *tsk = &reg->task[e];

        /* Current cell coordinate */
        int cx = (int)floorf(t->x + 0.5f);
        int cy = (int)floorf(t->y + 0.5f);

        /* 1. Handle task logic */
        if (tsk->kind == TASK_IDLE) {
            /* If we were mid-hop, finish it first */
            if (m->moving) {
                m->progress += m->speed * speed_multiplier * gdt;
                if (m->progress >= 1.0f) {
                    sgrid_move(sgrid, e, m->src_x, m->src_y, m->dst_x, m->dst_y);
                    t->x = (float)m->dst_x;
                    t->y = (float)m->dst_y;
                    m->progress = 0.0f;
                    m->moving = false;
                } else {
                    t->x = (float)m->src_x + (float)(m->dst_x - m->src_x) * m->progress;
                    t->y = (float)m->src_y + (float)(m->dst_y - m->src_y) * m->progress;
                }
            }
            continue;
        }

        /* Check if we reached MOVE_TO goal */
        if (tsk->kind == TASK_MOVE_TO && cx == tsk->target_x && cy == tsk->target_y && !m->moving) {
            LOG_INFO("Agent %u reached target position (%d, %d)", e, tsk->target_x, tsk->target_y);
            tsk->kind = TASK_IDLE;
            continue;
        }

        /* Check if we are adjacent to HARVEST target */
        if (tsk->kind == TASK_HARVEST && abs(cx - tsk->target_x) <= 1 && abs(cy - tsk->target_y) <= 1 && !m->moving) {
            tsk->timer += gdt;
            if (tsk->timer >= 1.0f) {
                tsk->timer -= 1.0f;
                /* Phase 4: O(1) lookup via spatial grid */
                Entity res_entity = sgrid_at(sgrid, tsk->target_x, tsk->target_y);
                if (res_entity != ENTITY_NULL && reg->has_resource[res_entity]) {
                    EntityHandle res_h = entity_to_handle(reg, res_entity);
                    bool destroyed = system_harvest_entity(reg, res_h, resources);
                    if (destroyed) {
                        sgrid_remove(sgrid, tsk->target_x, tsk->target_y);
                        tsk->kind = TASK_IDLE;
                    }
                } else {
                    LOG_INFO("Agent %u: harvest target at (%d, %d) depleted/gone", e, tsk->target_x, tsk->target_y);
                    tsk->kind = TASK_IDLE;
                }
            }
            continue;
        }

        /* Check if we are adjacent to BUILD target. Unlike HARVEST,
           there's no discrete "hit" to gate on a 1-second timer —
           building is continuous labor, so every frame spent adjacent
           contributes gdt directly via system_build_entity(). */
        if (tsk->kind == TASK_BUILD && abs(cx - tsk->target_x) <= 1 && abs(cy - tsk->target_y) <= 1 && !m->moving) {
            /* Phase 4: O(1) lookup via spatial grid */
            Entity blueprint = sgrid_at(sgrid, tsk->target_x, tsk->target_y);
            if (blueprint != ENTITY_NULL && reg->has_construction[blueprint]
                && !reg->construction[blueprint].complete) {
                if (system_build_entity(reg, blueprint, gdt)) {
                    tsk->kind = TASK_IDLE;
                }
            } else {
                LOG_INFO("Agent %u: build target at (%d, %d) missing/already complete", e, tsk->target_x, tsk->target_y);
                tsk->kind = TASK_IDLE;
            }
            continue;
        }

        /* 2. Pathfinding and Movement */
        bool path_valid = (tsk->path.len > 0) &&
                          (tsk->path.x[tsk->path.len - 1] == tsk->target_x) &&
                          (tsk->path.y[tsk->path.len - 1] == tsk->target_y);

        if (!path_valid && !m->moving) {
            if (pathfinder_find(world, cx, cy, tsk->target_x, tsk->target_y, &tsk->path)) {
                tsk->path_step = 0;
            } else {
                LOG_WARN("Agent %u: no path to (%d, %d)", e, tsk->target_x, tsk->target_y);
                tsk->kind = TASK_IDLE;
                continue;
            }
        }

        /* Execute movement */
        if (m->moving) {
            m->progress += m->speed * speed_multiplier * gdt;
            if (m->progress >= 1.0f) {
                /* Phase 4: commit the hop in the spatial grid */
                sgrid_move(sgrid, e, m->src_x, m->src_y, m->dst_x, m->dst_y);
                t->x = (float)m->dst_x;
                t->y = (float)m->dst_y;
                m->progress = 0.0f;
                m->moving = false;
                cx = m->dst_x;
                cy = m->dst_y;
            } else {
                t->x = (float)m->src_x + (float)(m->dst_x - m->src_x) * m->progress;
                t->y = (float)m->src_y + (float)(m->dst_y - m->src_y) * m->progress;
            }
        }

        if (!m->moving && tsk->path_step < tsk->path.len) {
            int nx = tsk->path.x[tsk->path_step];
            int ny = tsk->path.y[tsk->path_step];

            const Tile *tile = world_get_tile(world, nx, ny);
            if (tile && tile_is_walkable(tile->type)) {
                m->moving = true;
                m->src_x = cx;
                m->src_y = cy;
                m->dst_x = nx;
                m->dst_y = ny;
                m->progress = 0.0f;
                tsk->path_step++;
            } else {
                /* Path blocked — force recalculation next frame */
                tsk->path.len = 0;
            }
        }
    }
}
