#include "editor.h"

#include <math.h>
#include <stdio.h>
#include <SDL2/SDL.h>
#include "../platform/input.h"
#include "../renderer/renderer.h"
#include "../simulation/construction.h"
#include "../renderer/atlas.h"
#include "../world/tileset.h"
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

static bool pick_tile(const Camera *cam, World *world, int ui_panel_width, int ui_top_margin,
                       int ui_right_margin, int *out_gx, int *out_gy) {
    int mx, my;
    input_mouse_pos(&mx, &my);
    if (mx < ui_panel_width)  return false; /* pointer is over the sidebar, not the world */
    if (my < ui_top_margin)   return false; /* pointer is over the tab bar/status row, not the world */
    if (ui_right_margin > 0 && mx >= ui_right_margin) return false; /* pointer is over a right-docked panel (e.g. Shape Pane) */
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

/* Phase 4: entity lookup is now O(1) via the spatial grid — see sgrid_at(). */

static void log_entity_info(Registry *reg, Entity e) {
    TransformComponent  *t  = entity_get_transform(reg, e);
    RenderableComponent *rd = entity_get_renderable(reg, e);
    HealthComponent      *h = entity_get_health(reg, e);
    ResourceComponent   *rc = entity_get_resource(reg, e);
    MoveComponent        *m = entity_get_move(reg, e);
    TaskComponent      *tsk = entity_get_task(reg, e);
    ConstructionComponent *c = entity_get_construction(reg, e);

    LOG_INFO("-- selected entity %u --", e);
    if (t)  LOG_INFO("  Transform : x=%.2f y=%.2f", t->x, t->y);
    if (rd) LOG_INFO("  Renderable: rgba=(%.2f,%.2f,%.2f,%.2f) size=%.0fx%.0f",
                      rd->r, rd->g, rd->b, rd->a, rd->w, rd->h);
    if (h)  LOG_INFO("  Health    : %d / %d", h->current, h->max);
    if (rc) LOG_INFO("  Resource  : %s  yield_per_hit=%d",
                     rc->kind, rc->yield_per_hit);
    if (m)  LOG_INFO("  Move      : speed=%.1f progress=%.2f moving=%s (src=%d,%d dst=%d,%d)",
                     m->speed, m->progress, m->moving ? "true" : "false",
                     m->src_x, m->src_y, m->dst_x, m->dst_y);
    if (tsk) {
        const char *kind_str = "idle";
        if (tsk->kind == TASK_MOVE_TO) kind_str = "move_to";
        else if (tsk->kind == TASK_HARVEST) kind_str = "harvest";
        else if (tsk->kind == TASK_BUILD) kind_str = "build";
        LOG_INFO("  Task      : kind=%s target=(%d,%d) path_len=%d step=%d timer=%.2f",
                 kind_str, tsk->target_x, tsk->target_y,
                 tsk->path.len, tsk->path_step, tsk->timer);
    }
    if (c)  LOG_INFO("  Construct : %s  %.1f/%.1fs  %s",
                     c->def_name, c->build_time_done, c->build_time_total,
                     c->complete ? "COMPLETE" : "in progress");
    if (!t && !rd && !h && !rc && !m && !tsk && !c) LOG_INFO("  (no components)");
    LOG_INFO("  (H to harvest, M to command worker to hovered tile)");
}


const char *editor_mode_name(EditorMode m) {
    switch (m) {
        case EDITOR_MODE_PAINT:  return "PAINT";
        case EDITOR_MODE_PLACE:  return "PLACE";
        case EDITOR_MODE_SELECT: return "SELECT";
        case EDITOR_MODE_SHAPE:  return "SHAPE";
        default:                 return "?";
    }
}

/* There's no terrain_name() anymore — a PAINT-mode brush's name comes
   from tileset_name_for(&world->tileset, ed->brush) directly at each
   call site, since the engine no longer has a fixed list of terrain
   names to draw from. See world/tileset.h. */

/* ---------------------------------------------------------------------
   Public API */

void editor_init(Editor *ed) {
    ed->mode        = EDITOR_MODE_PAINT;
    ed->brush       = -1; /* nothing selected until the project's Tileset has a slot */
    ed->place_def_name[0] = '\0'; /* nothing selected until an ObjectDef exists to place */
    ed->place_sprite_id   = SPRITE_NONE;
    ed->selected    = ENTITY_HANDLE_NULL;
    ed->hover_valid = false;
    ed->hover_gx = ed->hover_gy = 0;

    LOG_INFO("Editor ready. TAB cycles mode (current: %s). "
             "PAINT: pick a tile from the panel's Tileset list (or press "
             "1-9 for the first 9 slots), LMB drag paints. "
             "PLACE: 1-3 pick prefab (free/instant), 4 picks a blueprint "
             "(costs resources, needs a worker to build), LMB places, RMB deletes. "
             "SELECT: LMB selects (info to log), Delete removes selection, "
             "M moves/harvests/builds with the selected worker at the hovered tile. "
             "SHAPE: LMB enables a tile, RMB disables it (hole in the map), "
             "Shift+LMB drag enables a whole rectangle at once.",
             editor_mode_name(ed->mode));
}

void editor_update(Editor *ed, Registry *reg, World *world, const Camera *cam,
                    ResourceStore *resources, SpatialGrid *sgrid,
                    int ui_panel_width, int ui_top_margin, int ui_right_margin) {
    /* Defensive: ed->brush is an index into world->tileset, but nothing
       forces it to stay in range across a world_load() that swaps in a
       different (possibly smaller, possibly empty) Tileset — main.c has
       three separate call sites that can trigger that (F9, PANEL_ACTION_
       LOAD, ENTER_EDITOR), and missing a clamp at any one of them would
       silently leave PAINT mode with an invalid brush selected. Every
       reader already bounds-checks (tileset_name_for/is_walkable/
       sprite_for all return safe defaults for an out-of-range index),
       so this was never a crash risk — but "your brush selection just
       silently stopped meaning anything" is still worth closing at one
       shared point instead of three, since a future new load path would
       otherwise need to remember this too. */
    if (ed->brush >= world->tileset.count) ed->brush = world->tileset.count - 1;

    ed->hover_valid = pick_tile(cam, world, ui_panel_width, ui_top_margin, ui_right_margin, &ed->hover_gx, &ed->hover_gy);

    /* Outside the playable area (a "hole" cut by Shape mode), painting
       and placing should behave like there's no tile there at all.
       SELECT and SHAPE itself need the raw hover (SHAPE has to be able
       to hover disabled tiles to re-enable them), so this only narrows
       hover_valid for the two modes that actually mutate map content. */
    if (ed->hover_valid && (ed->mode == EDITOR_MODE_PAINT || ed->mode == EDITOR_MODE_PLACE)
        && !world_tile_playable(world, ed->hover_gx, ed->hover_gy)) {
        ed->hover_valid = false;
    }

    if (!input_keyboard_consumed()) {
        if (input_key_pressed(SDL_SCANCODE_TAB)) {
            ed->mode = (EditorMode)((ed->mode + 1) % EDITOR_MODE_COUNT);
            LOG_INFO("Editor mode -> %s", editor_mode_name(ed->mode));
        }
    }

    switch (ed->mode) {

    case EDITOR_MODE_PAINT: {
        if (!input_keyboard_consumed()) {
            static const SDL_Scancode keys[] = {
                SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3,
                SDL_SCANCODE_4, SDL_SCANCODE_5, SDL_SCANCODE_6,
                SDL_SCANCODE_7, SDL_SCANCODE_8, SDL_SCANCODE_9,
            };
            int num_keys = (int)(sizeof(keys) / sizeof(keys[0]));
            /* Bounded by however many slots the project has actually
               defined, not a fixed enum count — a Tileset with 3 slots
               only responds to 1-3, one with 9+ responds to all of 1-9
               (beyond 9 needs the panel's palette, same as any editor
               that runs out of number-row shortcuts). */
            for (int i = 0; i < num_keys && i < world->tileset.count; i++) {
                if (input_key_pressed(keys[i])) {
                    ed->brush = i;
                    LOG_INFO("Brush -> %s", tileset_name_for(&world->tileset, ed->brush));
                }
            }
        }
        if (ed->hover_valid && input_mouse_button_down(SDL_BUTTON_LEFT)) {
            world_set_tile(world, ed->hover_gx, ed->hover_gy, ed->brush);
        }
        break;
    }

    case EDITOR_MODE_PLACE: {
        /* Phase 2 (ObjectDef consolidation): the old number-key
           shortcuts (1-3 for prefabs, 4 for the campfire blueprint)
           retired along with PrefabKind/BuildingKind — a project can
           define any number of placeable objects now, not a fixed
           four, so a fixed four number keys stopped making sense as
           the primary selection method. Selection happens in the
           panel's ObjectDef list (see ui/panel.c), the same way PAINT
           mode's Tileset brush is selected from its own palette list
           rather than a hardcoded key row. */
        if (ed->hover_valid && input_mouse_button_pressed(SDL_BUTTON_LEFT)) {
            const Tile *t = world_get_tile(world, ed->hover_gx, ed->hover_gy);
            if (!t || !world_tile_walkable(world, t)) {
                LOG_WARN("Can't place on unwalkable terrain at (%d, %d)",
                          ed->hover_gx, ed->hover_gy);
            } else if (ed->place_def_name[0] == '\0') {
                LOG_WARN("No object selected to place — pick one in the panel first");
            } else {
                ObjectDef def;
                char path[OBJDEF_PATH_MAX];
                snprintf(path, sizeof(path), "objects/%s.obj", ed->place_def_name);
                if (!objdef_load_file(&def, path)) {
                    LOG_WARN("Could not reload object definition '%s' (was it deleted/renamed "
                             "in the Objects tab?) — placement cancelled", ed->place_def_name);
                } else if (objdef_is_buildable(&def)) {
                    if (!objdef_try_pay_build_cost(resources, &def)) {
                        char ck[RESOURCE_NAME_MAX]; int cost; float bt;
                        objdef_get_build_spec(&def, ck, sizeof(ck), &cost, &bt);
                        LOG_WARN("Not enough resources to build '%s' (need %d %s)",
                                 def.name, cost, ck);
                    } else {
                        Entity e = construction_place_blueprint_objdef(
                            reg, &def, ed->place_sprite_id,
                            (float)ed->hover_gx, (float)ed->hover_gy);
                        if (e != ENTITY_NULL) {
                            sgrid_insert(sgrid, e, ed->hover_gx, ed->hover_gy);
                            LOG_INFO("Blueprint placed: '%s' at (%d, %d) — assign a worker "
                                     "(SELECT mode, M) to build it", def.name,
                                     ed->hover_gx, ed->hover_gy);
                        } else {
                            /* Registry full after we already paid — refund
                               so the player isn't charged for a placement
                               that didn't happen. construction_place_
                               blueprint_objdef() deliberately doesn't
                               touch the ResourceStore itself (see its doc
                               comment), so the refund is editor.c's job
                               here. */
                            objdef_refund_build_cost(resources, &def);
                            LOG_WARN("Could not place blueprint — registry full (cost refunded)");
                        }
                    }
                } else {
                    Entity e = objdef_spawn_instance(reg, &def, ed->place_sprite_id,
                                                      (float)ed->hover_gx, (float)ed->hover_gy);
                    if (e != ENTITY_NULL) {
                        sgrid_insert(sgrid, e, ed->hover_gx, ed->hover_gy);
                        LOG_INFO("Placed '%s' at (%d, %d)", def.name,
                                 ed->hover_gx, ed->hover_gy);
                    } else
                        LOG_WARN("Could not place '%s' — registry full", def.name);
                }
            }
        }
        if (ed->hover_valid && input_mouse_button_pressed(SDL_BUTTON_RIGHT)) {
            Entity e = sgrid_at(sgrid, ed->hover_gx, ed->hover_gy);
            if (e != ENTITY_NULL) {
                /* Deleting an unfinished blueprint refunds its cost — a
                   misclick or change of mind shouldn't be an unrecoverable
                   resource sink. Completed buildings (and everything
                   else placeable) just delete with no refund, same as
                   before.

                   Phase 2 fix: this used to read c->kind unconditionally
                   via the old BuildingKind-only building_cost_kind()/
                   building_cost_amount() — correct for the one hardcoded
                   BUILDING_CAMPFIRE, silently wrong for any ObjectDef
                   blueprint (whose kind field was never set, so it read
                   as campfire's cost regardless of the real object's
                   cost). Now that every blueprint carries a real
                   def_name, the refund always resolves the actual cost
                   by reloading that def — no more silent wrong-amount
                   refund possible. */
                ConstructionComponent *c = entity_get_construction(reg, e);
                if (c && !c->complete) {
                    ObjectDef def;
                    char path[OBJDEF_PATH_MAX];
                    snprintf(path, sizeof(path), "objects/%s.obj", c->def_name);
                    if (objdef_load_file(&def, path)) {
                        objdef_refund_build_cost(resources, &def);
                        LOG_INFO("Removed unfinished '%s' blueprint at (%d, %d) — cost refunded",
                                 c->def_name, ed->hover_gx, ed->hover_gy);
                    } else {
                        LOG_WARN("Removed unfinished '%s' blueprint at (%d, %d) — could not "
                                 "reload its definition to refund cost (renamed/deleted?)",
                                 c->def_name, ed->hover_gx, ed->hover_gy);
                    }
                } else {
                    LOG_INFO("Removed entity %u at (%d, %d)", e, ed->hover_gx, ed->hover_gy);
                }
                /* If the entity being deleted was selected from SELECT
                   mode earlier, drop the (now-stale) handle too. */
                if (ed->selected.index == e)
                    ed->selected = ENTITY_HANDLE_NULL;
                sgrid_remove(sgrid, ed->hover_gx, ed->hover_gy);
                entity_destroy(reg, e);
            }
        }
        break;
    }

    case EDITOR_MODE_SELECT: {
        if (ed->hover_valid && input_mouse_button_pressed(SDL_BUTTON_LEFT)) {
            Entity e = sgrid_at(sgrid, ed->hover_gx, ed->hover_gy);
            if (e != ENTITY_NULL) {
                ed->selected = entity_to_handle(reg, e);
                log_entity_info(reg, e);
            } else {
                if (entity_handle_valid(reg, ed->selected))
                    LOG_INFO("Selection cleared");
                ed->selected = ENTITY_HANDLE_NULL;
            }
        }
        if (!input_keyboard_consumed()) {
            if ((input_key_pressed(SDL_SCANCODE_DELETE) ||
                 input_key_pressed(SDL_SCANCODE_BACKSPACE)) &&
                entity_handle_valid(reg, ed->selected)) {
                Entity e = ed->selected.index;
                TransformComponent *t = entity_get_transform(reg, e);
                if (t) sgrid_remove(sgrid, (int)(t->x + 0.5f), (int)(t->y + 0.5f));
                entity_destroy(reg, e);
                ed->selected = ENTITY_HANDLE_NULL;
                LOG_INFO("Deleted selected entity %u", e);
            }
            if (input_key_pressed(SDL_SCANCODE_M) && entity_handle_valid(reg, ed->selected)) {
                Entity worker = ed->selected.index;
                if (reg->has_move[worker] && reg->has_task[worker]) {
                    if (ed->hover_valid) {
                        Entity hover_ent = sgrid_at(sgrid, ed->hover_gx, ed->hover_gy);
                        if (hover_ent != ENTITY_NULL && reg->has_resource[hover_ent]) {
                            TaskComponent *tsk = entity_get_task(reg, worker);
                            tsk->kind = TASK_HARVEST;
                            tsk->target_x = ed->hover_gx;
                            tsk->target_y = ed->hover_gy;
                            tsk->path.len = 0;
                            tsk->path_step = 0;
                            tsk->timer = 0.0f;
                            LOG_INFO("Worker %u assigned to harvest %s at (%d, %d)",
                                     worker, reg->resource[hover_ent].kind,
                                     ed->hover_gx, ed->hover_gy);
                        } else if (hover_ent != ENTITY_NULL && reg->has_construction[hover_ent]
                                   && !reg->construction[hover_ent].complete) {
                            TaskComponent *tsk = entity_get_task(reg, worker);
                            tsk->kind = TASK_BUILD;
                            tsk->target_x = ed->hover_gx;
                            tsk->target_y = ed->hover_gy;
                            tsk->path.len = 0;
                            tsk->path_step = 0;
                            tsk->timer = 0.0f;
                            LOG_INFO("Worker %u assigned to build '%s' at (%d, %d)",
                                     worker, reg->construction[hover_ent].def_name,
                                     ed->hover_gx, ed->hover_gy);
                        } else {
                            const Tile *tile = world_get_tile(world, ed->hover_gx, ed->hover_gy);
                            if (tile && world_tile_walkable(world, tile)) {
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
        }
        break;
    }

    case EDITOR_MODE_SHAPE: {
        /* Shape editing now happens exclusively in the docked flat-grid
           Shape Pane (ui/shape_pane.c) — diamond hit-testing on the iso
           view was a genuinely worse way to reason about a 2D boolean
           mask than a plain square grid, so that path was removed
           rather than left as a second, redundant way to do the same
           thing through two different input surfaces. The iso view
           still shows the *result* live (world_render() reads the same
           WorldShape the pane writes), it just doesn't take paint input
           of its own anymore. Nothing to do here but make sure the mask
           exists, since Settings' "World Shape" toggle and the panel's
           mode button can both be the first thing that activates it. */
        if (!world->shape.active)
            world_shape_activate(&world->shape, world->width, world->height);
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
