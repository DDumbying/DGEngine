#include "registry.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "../core/log.h"
#include "../renderer/atlas.h"

void registry_init(Registry *r) {
    memset(r, 0, sizeof(*r));
}

Entity entity_create(Registry *r) {
    Entity e;
    if (r->free_count > 0) {
        e = r->free_list[--r->free_count];
    } else {
        if (r->high_water >= MAX_ENTITIES) {
            LOG_ERROR("entity_create: MAX_ENTITIES (%d) exhausted", MAX_ENTITIES);
            return ENTITY_NULL;
        }
        e = (Entity)r->high_water++;
    }
    r->alive[e] = true;
    return e;
}

void entity_destroy(Registry *r, Entity e) {
    if (e == ENTITY_NULL || e >= (Entity)MAX_ENTITIES || !r->alive[e]) return;

    r->alive[e]         = false;
    r->has_transform[e] = false;
    r->has_renderable[e]= false;
    r->has_health[e]    = false;
    r->has_resource[e]  = false;
    r->has_move[e]      = false;
    r->has_task[e]      = false;
    r->has_construction[e] = false;
    r->has_definition[e]   = false;
    r->generation[e]++;

    r->free_list[r->free_count++] = e;
}

bool entity_alive(const Registry *r, Entity e) {
    return e != ENTITY_NULL && e < (Entity)MAX_ENTITIES && r->alive[e];
}

EntityHandle entity_to_handle(const Registry *r, Entity e) {
    if (!entity_alive(r, e)) return ENTITY_HANDLE_NULL;
    return (EntityHandle){e, r->generation[e]};
}

bool entity_handle_valid(const Registry *r, EntityHandle h) {
    return h.index != ENTITY_NULL && h.index < (Entity)MAX_ENTITIES
        && r->alive[h.index] && r->generation[h.index] == h.generation;
}

void entity_add_transform(Registry *r, Entity e, TransformComponent t) {
    if (!entity_alive(r, e)) return;
    r->transform[e]     = t;
    r->has_transform[e] = true;
}

TransformComponent *entity_get_transform(Registry *r, Entity e) {
    if (!entity_alive(r, e) || !r->has_transform[e]) return NULL;
    return &r->transform[e];
}

void entity_add_renderable(Registry *r, Entity e, RenderableComponent c) {
    if (!entity_alive(r, e)) return;
    r->renderable[e]     = c;
    r->has_renderable[e] = true;
}

RenderableComponent *entity_get_renderable(Registry *r, Entity e) {
    if (!entity_alive(r, e) || !r->has_renderable[e]) return NULL;
    return &r->renderable[e];
}

void entity_add_health(Registry *r, Entity e, HealthComponent h) {
    if (!entity_alive(r, e)) return;
    r->health[e]     = h;
    r->has_health[e] = true;
}

HealthComponent *entity_get_health(Registry *r, Entity e) {
    if (!entity_alive(r, e) || !r->has_health[e]) return NULL;
    return &r->health[e];
}

void entity_add_resource(Registry *r, Entity e, ResourceComponent rc) {
    if (!entity_alive(r, e)) return;
    r->resource[e]     = rc;
    r->has_resource[e] = true;
}

ResourceComponent *entity_get_resource(Registry *r, Entity e) {
    if (!entity_alive(r, e) || !r->has_resource[e]) return NULL;
    return &r->resource[e];
}

void entity_add_move(Registry *r, Entity e, MoveComponent m) {
    if (!entity_alive(r, e)) return;
    r->move[e]     = m;
    r->has_move[e] = true;
}

MoveComponent *entity_get_move(Registry *r, Entity e) {
    if (!entity_alive(r, e) || !r->has_move[e]) return NULL;
    return &r->move[e];
}

void entity_add_task(Registry *r, Entity e, TaskComponent t) {
    if (!entity_alive(r, e)) return;
    r->task[e]     = t;
    r->has_task[e] = true;
}

TaskComponent *entity_get_task(Registry *r, Entity e) {
    if (!entity_alive(r, e) || !r->has_task[e]) return NULL;
    return &r->task[e];
}

void entity_add_construction(Registry *r, Entity e, ConstructionComponent c) {
    if (!entity_alive(r, e)) return;
    r->construction[e]     = c;
    r->has_construction[e] = true;
}

ConstructionComponent *entity_get_construction(Registry *r, Entity e) {
    if (!entity_alive(r, e) || !r->has_construction[e]) return NULL;
    return &r->construction[e];
}

void entity_add_definition(Registry *r, Entity e, DefinitionComponent d) {
    if (!entity_alive(r, e)) return;
    r->definition[e]     = d;
    r->has_definition[e] = true;
}

DefinitionComponent *entity_get_definition(Registry *r, Entity e) {
    if (!entity_alive(r, e) || !r->has_definition[e]) return NULL;
    return &r->definition[e];
}

/* ---------------------------------------------------------------------
   Save / load — see the format comment in registry.h. */

#define DGEE_MAGIC   "DGEE"
#define DGEE_VERSION 6u   /* Phase L: DefinitionComponent added; also fixed
                             sprite_id never being written/read for
                             RenderableComponent (latent since Phase E —
                             every save/load round-trip was silently
                             resetting it to whatever garbage happened
                             to be on the stack) */

#define MASK_TRANSFORM     (1u << 0)
#define MASK_RENDERABLE    (1u << 1)
#define MASK_HEALTH        (1u << 2)
#define MASK_RESOURCE      (1u << 3)
#define MASK_MOVE          (1u << 4)
#define MASK_TASK          (1u << 5)
#define MASK_CONSTRUCTION  (1u << 6)
#define MASK_DEFINITION    (1u << 7)

bool registry_save(const Registry *r, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        LOG_ERROR("registry_save: could not open '%s' for writing", path);
        return false;
    }

    unsigned int count = 0;
    for (Entity e = 0; e < (Entity)MAX_ENTITIES; e++)
        if (r->alive[e]) count++;

    unsigned int version = DGEE_VERSION;
    bool ok = true;
    ok &= fwrite(DGEE_MAGIC, 1, 4, f) == 4;
    ok &= fwrite(&version, sizeof(version), 1, f) == 1;
    ok &= fwrite(&count,   sizeof(count),   1, f) == 1;

    for (Entity e = 0; ok && e < (Entity)MAX_ENTITIES; e++) {
        if (!r->alive[e]) continue;

        unsigned char mask = 0;
        if (r->has_transform[e])  mask |= MASK_TRANSFORM;
        if (r->has_renderable[e]) mask |= MASK_RENDERABLE;
        if (r->has_health[e])     mask |= MASK_HEALTH;
        if (r->has_resource[e])   mask |= MASK_RESOURCE;
        if (r->has_move[e])       mask |= MASK_MOVE;
        if (r->has_task[e])       mask |= MASK_TASK;
        if (r->has_construction[e]) mask |= MASK_CONSTRUCTION;
        if (r->has_definition[e])   mask |= MASK_DEFINITION;
        ok &= fwrite(&mask, 1, 1, f) == 1;

        if (ok && (mask & MASK_TRANSFORM)) {
            const TransformComponent *t = &r->transform[e];
            ok &= fwrite(&t->x, sizeof(t->x), 1, f) == 1;
            ok &= fwrite(&t->y, sizeof(t->y), 1, f) == 1;
        }
        if (ok && (mask & MASK_RENDERABLE)) {
            const RenderableComponent *c = &r->renderable[e];
            ok &= fwrite(&c->r, sizeof(c->r), 1, f) == 1;
            ok &= fwrite(&c->g, sizeof(c->g), 1, f) == 1;
            ok &= fwrite(&c->b, sizeof(c->b), 1, f) == 1;
            ok &= fwrite(&c->a, sizeof(c->a), 1, f) == 1;
            ok &= fwrite(&c->w, sizeof(c->w), 1, f) == 1;
            ok &= fwrite(&c->h, sizeof(c->h), 1, f) == 1;
            ok &= fwrite(&c->sprite_id, sizeof(c->sprite_id), 1, f) == 1;
        }
        if (ok && (mask & MASK_HEALTH)) {
            const HealthComponent *h = &r->health[e];
            ok &= fwrite(&h->current, sizeof(h->current), 1, f) == 1;
            ok &= fwrite(&h->max,     sizeof(h->max),     1, f) == 1;
        }
        if (ok && (mask & MASK_RESOURCE)) {
            const ResourceComponent *rc = &r->resource[e];
            unsigned char kind_byte = (unsigned char)rc->kind;
            ok &= fwrite(&kind_byte,        1,                    1, f) == 1;
            ok &= fwrite(&rc->yield_per_hit, sizeof(rc->yield_per_hit), 1, f) == 1;
        }
        if (ok && (mask & MASK_MOVE)) {
            const MoveComponent *m = &r->move[e];
            ok &= fwrite(&m->speed, sizeof(m->speed), 1, f) == 1;
        }
        if (ok && (mask & MASK_TASK)) {
            const TaskComponent *t = &r->task[e];
            unsigned char kind_byte = (unsigned char)t->kind;
            ok &= fwrite(&kind_byte, sizeof(kind_byte), 1, f) == 1;
            ok &= fwrite(&t->target_x, sizeof(t->target_x), 1, f) == 1;
            ok &= fwrite(&t->target_y, sizeof(t->target_y), 1, f) == 1;
        }
        if (ok && (mask & MASK_CONSTRUCTION)) {
            const ConstructionComponent *c = &r->construction[e];
            unsigned char kind_byte = (unsigned char)c->kind;
            unsigned char complete_byte = (unsigned char)(c->complete ? 1 : 0);
            ok &= fwrite(&kind_byte,            1,                          1, f) == 1;
            ok &= fwrite(&c->build_time_total,  sizeof(c->build_time_total), 1, f) == 1;
            ok &= fwrite(&c->build_time_done,   sizeof(c->build_time_done),  1, f) == 1;
            ok &= fwrite(&complete_byte,        1,                          1, f) == 1;
        }
        if (ok && (mask & MASK_DEFINITION)) {
            const DefinitionComponent *d = &r->definition[e];
            ok &= fwrite(d->def_name, 1, OBJDEF_NAME_MAX, f) == OBJDEF_NAME_MAX;
        }
    }

    fclose(f);
    if (!ok) {
        LOG_ERROR("registry_save: write error writing '%s'", path);
        return false;
    }
    LOG_INFO("Entities saved -> '%s' (%u entities)", path, count);
    return true;
}

bool registry_load(Registry *r, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        LOG_ERROR("registry_load: could not open '%s'", path);
        return false;
    }

    char magic[4];
    unsigned int version, count;
    bool ok = true;
    ok &= fread(magic, 1, 4, f) == 4;
    ok &= fread(&version, sizeof(version), 1, f) == 1;
    ok &= fread(&count,   sizeof(count),   1, f) == 1;

    if (!ok || magic[0] != 'D' || magic[1] != 'G' || magic[2] != 'E' || magic[3] != 'E') {
        LOG_ERROR("registry_load: '%s' is not a valid DGEE entity file", path);
        fclose(f);
        return false;
    }
    if (version == 0 || version > DGEE_VERSION) {
        LOG_ERROR("registry_load: '%s' has unsupported version %u (expected <= %u)",
                   path, version, DGEE_VERSION);
        fclose(f);
        return false;
    }
    if (count > (unsigned int)MAX_ENTITIES) {
        LOG_ERROR("registry_load: '%s' has %u entities, more than MAX_ENTITIES (%d)",
                   path, count, MAX_ENTITIES);
        fclose(f);
        return false;
    }

    Registry *loaded = malloc(sizeof(Registry));
    if (!loaded) {
        LOG_ERROR("registry_load: out of memory allocating temporary registry");
        fclose(f);
        return false;
    }
    registry_init(loaded);

    for (unsigned int i = 0; ok && i < count; i++) {
        unsigned char mask;
        if (fread(&mask, 1, 1, f) != 1) { ok = false; break; }

        Entity e = entity_create(loaded);
        if (e == ENTITY_NULL) { ok = false; break; }

        if (mask & MASK_TRANSFORM) {
            TransformComponent t;
            if (fread(&t.x, sizeof(t.x), 1, f) != 1) { ok = false; break; }
            if (fread(&t.y, sizeof(t.y), 1, f) != 1) { ok = false; break; }
            entity_add_transform(loaded, e, t);
        }
        if ((mask & MASK_RENDERABLE)) {
            RenderableComponent c;
            memset(&c, 0, sizeof(c));
            if (fread(&c.r, sizeof(c.r), 1, f) != 1) { ok = false; break; }
            if (fread(&c.g, sizeof(c.g), 1, f) != 1) { ok = false; break; }
            if (fread(&c.b, sizeof(c.b), 1, f) != 1) { ok = false; break; }
            if (fread(&c.a, sizeof(c.a), 1, f) != 1) { ok = false; break; }
            if (fread(&c.w, sizeof(c.w), 1, f) != 1) { ok = false; break; }
            if (fread(&c.h, sizeof(c.h), 1, f) != 1) { ok = false; break; }
            if (version >= 6) {
                /* Versions <6 never wrote this field at all (see the
                   DGEE_VERSION comment) — reading it from a v<6 file
                   would misalign every byte after it, so it's gated on
                   the version actually stored in the file, not just
                   the version this build knows how to write. */
                if (fread(&c.sprite_id, sizeof(c.sprite_id), 1, f) != 1) { ok = false; break; }
            } else {
                c.sprite_id = SPRITE_NONE;
            }
            entity_add_renderable(loaded, e, c);
        }
        if ((mask & MASK_HEALTH)) {
            HealthComponent h;
            if (fread(&h.current, sizeof(h.current), 1, f) != 1) { ok = false; break; }
            if (fread(&h.max,     sizeof(h.max),     1, f) != 1) { ok = false; break; }
            entity_add_health(loaded, e, h);
        }
        if ((mask & MASK_RESOURCE)) {
            unsigned char kind_byte;
            ResourceComponent rc;
            if (fread(&kind_byte,        1,                    1, f) != 1) { ok = false; break; }
            if (fread(&rc.yield_per_hit, sizeof(rc.yield_per_hit), 1, f) != 1) { ok = false; break; }
            rc.kind = (ResourceKind)kind_byte;
            entity_add_resource(loaded, e, rc);
        }
        if ((mask & MASK_MOVE)) {
            MoveComponent m;
            memset(&m, 0, sizeof(m));
            if (fread(&m.speed, sizeof(m.speed), 1, f) != 1) { ok = false; break; }
            m.progress = 0.0f;
            m.moving = false;
            entity_add_move(loaded, e, m);
        }
        if ((mask & MASK_TASK)) {
            TaskComponent t;
            memset(&t, 0, sizeof(t));
            unsigned char kind_byte;
            if (fread(&kind_byte, sizeof(kind_byte), 1, f) != 1) { ok = false; break; }
            if (fread(&t.target_x, sizeof(t.target_x), 1, f) != 1) { ok = false; break; }
            if (fread(&t.target_y, sizeof(t.target_y), 1, f) != 1) { ok = false; break; }
            t.kind = (TaskKind)kind_byte;
            t.path_step = 0;
            t.path.len = 0;
            entity_add_task(loaded, e, t);
        }
        if ((mask & MASK_CONSTRUCTION)) {
            ConstructionComponent c;
            unsigned char kind_byte, complete_byte;
            if (fread(&kind_byte, 1, 1, f) != 1) { ok = false; break; }
            if (fread(&c.build_time_total, sizeof(c.build_time_total), 1, f) != 1) { ok = false; break; }
            if (fread(&c.build_time_done,  sizeof(c.build_time_done),  1, f) != 1) { ok = false; break; }
            if (fread(&complete_byte, 1, 1, f) != 1) { ok = false; break; }
            c.kind     = (BuildingKind)kind_byte;
            c.complete = (complete_byte != 0);
            entity_add_construction(loaded, e, c);
        }
        if ((mask & MASK_DEFINITION)) {
            DefinitionComponent d;
            memset(&d, 0, sizeof(d));
            if (fread(d.def_name, 1, OBJDEF_NAME_MAX, f) != OBJDEF_NAME_MAX) { ok = false; break; }
            d.def_name[OBJDEF_NAME_MAX - 1] = '\0'; /* guarantee NUL-termination regardless of file contents */
            entity_add_definition(loaded, e, d);
        }
    }
    fclose(f);

    if (!ok) {
        LOG_ERROR("registry_load: truncated/corrupt entity data in '%s'", path);
        free(loaded);
        return false;
    }

    *r = *loaded;
    free(loaded);
    LOG_INFO("Entities loaded <- '%s' (%u entities)", path, count);
    return true;
}
