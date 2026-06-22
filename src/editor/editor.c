#include "editor.h"

#include <math.h>
#include <SDL2/SDL.h>
#include "../platform/input.h"
#include "../renderer/renderer.h"
#include "../core/log.h"

/* ---------------------------------------------------------------------
   Picking — inverts renderer.c's iso projection.

   Forward (renderer_draw_iso_tile):
     cx = (gx - gy) * tile_w/2
     cy = (gx + gy) * tile_h/2

   Solve for gx, gy given (cx, cy):
     a = 2*cx/tile_w = gx - gy
     b = 2*cy/tile_h = gx + gy
     gx = (a + b) / 2
     gy = (b - a) / 2                                                   */

static bool pick_tile(const Camera *cam, World *world, int *out_gx, int *out_gy) {
    int mx, my;
    input_mouse_pos(&mx, &my);
    Vec2 w = camera_screen_to_world(cam, (float)mx, (float)my);

    float tw, th;
    renderer_get_tile_size(&tw, &th);

    float a = (2.0f * w.x) / tw;
    float b = (2.0f * w.y) / th;
    float fgx = (a + b) * 0.5f;
    float fgy = (b - a) * 0.5f;

    int gx = (int)floorf(fgx + 0.5f);
    int gy = (int)floorf(fgy + 0.5f);

    if (!world_get_tile(world, gx, gy)) return false;
    *out_gx = gx;
    *out_gy = gy;
    return true;
}

/* Linear scan over MAX_ENTITIES, only on click (not per-frame), same
   "simple until it's actually slow" reasoning as everything else in
   this engine. Revisit with a spatial grid if entity counts grow large
   enough for this to matter — Simulation/AI phases are likelier to hit
   that ceiling first, at which point picking can reuse whatever
   structure they build. */
static Entity find_entity_at_tile(Registry *reg, int gx, int gy) {
    for (Entity e = 0; e < (Entity)MAX_ENTITIES; e++) {
        if (!entity_alive(reg, e)) continue;
        TransformComponent *t = entity_get_transform(reg, e);
        if (!t) continue;
        int ex = (int)floorf(t->x + 0.5f);
        int ey = (int)floorf(t->y + 0.5f);
        if (ex == gx && ey == gy) return e;
    }
    return ENTITY_NULL;
}

static void log_entity_info(Registry *reg, Entity e) {
    TransformComponent  *t  = entity_get_transform(reg, e);
    RenderableComponent *rd = entity_get_renderable(reg, e);
    HealthComponent      *h = entity_get_health(reg, e);
    ResourceComponent   *rc = entity_get_resource(reg, e);
    MoveComponent        *m = entity_get_move(reg, e);
    TaskComponent      *tsk = entity_get_task(reg, e);

    LOG_INFO("-- selected entity %u --", e);
    if (t)  LOG_INFO("  Transform : x=%.2f y=%.2f", t->x, t->y);
    if (rd) LOG_INFO("  Renderable: rgba=(%.2f,%.2f,%.2f,%.2f) size=%.0fx%.0f",
                      rd->r, rd->g, rd->b, rd->a, rd->w, rd->h);
    if (h)  LOG_INFO("  Health    : %d / %d", h->current, h->max);
    if (rc) LOG_INFO("  Resource  : %s  yield_per_hit=%d",
                     rc->kind == RESOURCE_WOOD ? "wood" : "stone",
                     rc->yield_per_hit);
    if (m)  LOG_INFO("  Move      : speed=%.1f progress=%.2f moving=%s (src=%d,%d dst=%d,%d)",
                     m->speed, m->progress, m->moving ? "true" : "false",
                     m->src_x, m->src_y, m->dst_x, m->dst_y);
    if (tsk) {
        const char *kind_str = "idle";
        if (tsk->kind == TASK_MOVE_TO) kind_str = "move_to";
        else if (tsk->kind == TASK_HARVEST) kind_str = "harvest";
        LOG_INFO("  Task      : kind=%s target=(%d,%d) path_len=%d step=%d timer=%.2f",
                 kind_str, tsk->target_x, tsk->target_y,
                 tsk->path.len, tsk->path_step, tsk->timer);
    }
    if (!t && !rd && !h && !rc && !m && !tsk) LOG_INFO("  (no components)");
    LOG_INFO("  (H to harvest, M to command worker to hovered tile)");
}


static const char *mode_name(EditorMode m) {
    switch (m) {
        case EDITOR_MODE_PAINT:  return "PAINT";
        case EDITOR_MODE_PLACE:  return "PLACE";
        case EDITOR_MODE_SELECT: return "SELECT";
        default:                 return "?";
    }
}

static const char *terrain_name(TerrainType t) {
    switch (t) {
        case TERRAIN_GRASS: return "grass";
        case TERRAIN_DIRT:  return "dirt";
        case TERRAIN_SAND:  return "sand";
        case TERRAIN_WATER: return "water";
        case TERRAIN_STONE: return "stone";
        default:            return "?";
    }
}

/* ---------------------------------------------------------------------
   Public API */

void editor_init(Editor *ed) {
    ed->mode        = EDITOR_MODE_PAINT;
    ed->brush       = TERRAIN_GRASS;
    ed->prefab      = PREFAB_TREE;
    ed->selected    = ENTITY_HANDLE_NULL;
    ed->hover_valid = false;
    ed->hover_gx = ed->hover_gy = 0;

    LOG_INFO("Editor ready. TAB cycles mode (current: %s). "
             "PAINT: 1-5 pick terrain, LMB drag paints. "
             "PLACE: 1-3 pick prefab, LMB places, RMB deletes. "
             "SELECT: LMB selects (info to log), Delete removes selection, M moves/harvests worker to hovered tile.",
             mode_name(ed->mode));
}

void editor_update(Editor *ed, Registry *reg, World *world, const Camera *cam) {
    ed->hover_valid = pick_tile(cam, world, &ed->hover_gx, &ed->hover_gy);

    if (input_key_pressed(SDL_SCANCODE_TAB)) {
        ed->mode = (EditorMode)((ed->mode + 1) % EDITOR_MODE_COUNT);
        LOG_INFO("Editor mode -> %s", mode_name(ed->mode));
    }

    switch (ed->mode) {

    case EDITOR_MODE_PAINT: {
        static const SDL_Scancode keys[] = {
            SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3,
            SDL_SCANCODE_4, SDL_SCANCODE_5
        };
        int num_keys = (int)(sizeof(keys) / sizeof(keys[0]));
        for (int i = 0; i < num_keys && i < TERRAIN_COUNT; i++) {
            if (input_key_pressed(keys[i])) {
                ed->brush = (TerrainType)i;
                LOG_INFO("Brush -> %s", terrain_name(ed->brush));
            }
        }
        if (ed->hover_valid && input_mouse_button_down(SDL_BUTTON_LEFT)) {
            world_set_tile(world, ed->hover_gx, ed->hover_gy, ed->brush);
        }
        break;
    }

    case EDITOR_MODE_PLACE: {
        if (input_key_pressed(SDL_SCANCODE_1)) {
            ed->prefab = PREFAB_TREE;
            LOG_INFO("Prefab -> %s", prefab_name(ed->prefab));
        }
        if (input_key_pressed(SDL_SCANCODE_2)) {
            ed->prefab = PREFAB_ROCK;
            LOG_INFO("Prefab -> %s", prefab_name(ed->prefab));
        }
        if (input_key_pressed(SDL_SCANCODE_3)) {
            ed->prefab = PREFAB_WORKER;
            LOG_INFO("Prefab -> %s", prefab_name(ed->prefab));
        }
        if (ed->hover_valid && input_mouse_button_pressed(SDL_BUTTON_LEFT)) {
            const Tile *t = world_get_tile(world, ed->hover_gx, ed->hover_gy);
            if (t && tile_is_walkable(t->type)) {
                Entity e = prefab_spawn(reg, ed->prefab,
                                         (float)ed->hover_gx, (float)ed->hover_gy);
                if (e != ENTITY_NULL)
                    LOG_INFO("Placed %s at (%d, %d)", prefab_name(ed->prefab),
                             ed->hover_gx, ed->hover_gy);
                else
                    LOG_WARN("Could not place %s — registry full", prefab_name(ed->prefab));
            } else {
                LOG_WARN("Can't place on unwalkable terrain at (%d, %d)",
                          ed->hover_gx, ed->hover_gy);
            }
        }
        if (ed->hover_valid && input_mouse_button_pressed(SDL_BUTTON_RIGHT)) {
            Entity e = find_entity_at_tile(reg, ed->hover_gx, ed->hover_gy);
            if (e != ENTITY_NULL) {
                /* If the entity being deleted was selected from SELECT
                   mode earlier, drop the (now-stale) handle too. */
                if (ed->selected.index == e)
                    ed->selected = ENTITY_HANDLE_NULL;
                entity_destroy(reg, e);
                LOG_INFO("Removed entity %u at (%d, %d)", e, ed->hover_gx, ed->hover_gy);
            }
        }
        break;
    }

    case EDITOR_MODE_SELECT: {
        if (ed->hover_valid && input_mouse_button_pressed(SDL_BUTTON_LEFT)) {
            Entity e = find_entity_at_tile(reg, ed->hover_gx, ed->hover_gy);
            if (e != ENTITY_NULL) {
                ed->selected = entity_to_handle(reg, e);
                log_entity_info(reg, e);
            } else {
                if (entity_handle_valid(reg, ed->selected))
                    LOG_INFO("Selection cleared");
                ed->selected = ENTITY_HANDLE_NULL;
            }
        }
        if ((input_key_pressed(SDL_SCANCODE_DELETE) ||
             input_key_pressed(SDL_SCANCODE_BACKSPACE)) &&
            entity_handle_valid(reg, ed->selected)) {
            Entity e = ed->selected.index;
            entity_destroy(reg, e);
            ed->selected = ENTITY_HANDLE_NULL;
            LOG_INFO("Deleted selected entity %u", e);
        }
        if (input_key_pressed(SDL_SCANCODE_M) && entity_handle_valid(reg, ed->selected)) {
            Entity worker = ed->selected.index;
            if (reg->has_move[worker] && reg->has_task[worker]) {
                if (ed->hover_valid) {
                    Entity hover_ent = find_entity_at_tile(reg, ed->hover_gx, ed->hover_gy);
                    if (hover_ent != ENTITY_NULL && reg->has_resource[hover_ent]) {
                        TaskComponent *tsk = entity_get_task(reg, worker);
                        tsk->kind = TASK_HARVEST;
                        tsk->target_x = ed->hover_gx;
                        tsk->target_y = ed->hover_gy;
                        tsk->path.len = 0;
                        tsk->path_step = 0;
                        tsk->timer = 0.0f;
                        const char *res_type = (reg->resource[hover_ent].kind == RESOURCE_WOOD) ? "wood" : "stone";
                        LOG_INFO("Worker %u assigned to harvest %s at (%d, %d)",
                                 worker, res_type, ed->hover_gx, ed->hover_gy);
                    } else {
                        const Tile *tile = world_get_tile(world, ed->hover_gx, ed->hover_gy);
                        if (tile && tile_is_walkable(tile->type)) {
                            TaskComponent *tsk = entity_get_task(reg, worker);
                            tsk->kind = TASK_MOVE_TO;
                            tsk->target_x = ed->hover_gx;
                            tsk->target_y = ed->hover_gy;
                            tsk->path.len = 0;
                            tsk->path_step = 0;
                            LOG_INFO("Worker %u assigned to move to (%d, %d)",
                                     worker, ed->hover_gx, ed->hover_gy);
                        } else {
                            LOG_WARN("Cannot assign task: tile (%d, %d) is unwalkable",
                                     ed->hover_gx, ed->hover_gy);
                        }
                    }
                }
            } else {
                LOG_WARN("Selected entity %u is not an agent (cannot accept movement/tasks)", worker);
            }
        }
        break;
    }

    default:
        break;
    }
}

void editor_render(const Editor *ed, Registry *reg) {
    if (ed->hover_valid) {
        /* Soft white wash on the hovered tile — visible on every
           terrain color without fighting any of them. */
        renderer_draw_iso_tile(ed->hover_gx, ed->hover_gy, 1.0f, 1.0f, 1.0f, 0.18f);
    }

    if (entity_handle_valid(reg, ed->selected)) {
        TransformComponent *t = entity_get_transform(reg, ed->selected.index);
        if (t) {
            int sgx = (int)floorf(t->x + 0.5f);
            int sgy = (int)floorf(t->y + 0.5f);
            /* Warm yellow marker, separate from the hover wash above so
               "what I'm pointing at" and "what I've selected" are never
               visually ambiguous when they're the same tile. */
            renderer_draw_iso_tile(sgx, sgy, 1.0f, 0.85f, 0.2f, 0.35f);
        }
    }
}
