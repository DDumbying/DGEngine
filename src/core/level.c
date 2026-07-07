#include "level.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

#ifdef _WIN32
#  include <direct.h>
#  define dge_mkdir(p) _mkdir(p)
#  define PATH_SEP "\\"
#else
#  include <unistd.h>
#  define dge_mkdir(p) mkdir((p), 0755)
#  define PATH_SEP "/"
#endif

/* ---------------------------------------------------------------------
   Helpers */

/* Lowercase, spaces->underscores, drop anything not [a-z0-9_]. Empty
   input (or input that's entirely dropped characters, e.g. "!!!")
   produces an empty output string — level_registry_add() checks for
   this and refuses to add a level with no usable slug rather than
   silently creating one with an empty filename. */
static void slugify(const char *name, char *out, size_t out_size) {
    size_t o = 0;
    for (size_t i = 0; name[i] != '\0' && o + 1 < out_size; i++) {
        char c = name[i];
        if (c == ' ') { out[o++] = '_'; continue; }
        c = (char)tolower((unsigned char)c);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')
            out[o++] = c;
    }
    out[o] = '\0';
}

static bool file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

/* ---------------------------------------------------------------------
   Lifecycle */

void level_registry_init(LevelRegistry *lr) {
    memset(lr, 0, sizeof(*lr));
    lr->active_index = -1;
}

int level_registry_add(LevelRegistry *lr, const char *name) {
    if (lr->count >= LEVEL_MAX) {
        LOG_WARN("level_registry_add: full (max %d levels)", LEVEL_MAX);
        return -1;
    }

    char slug[LEVEL_NAME_MAX];
    slugify(name, slug, sizeof(slug));
    if (slug[0] == '\0') {
        LOG_WARN("level_registry_add: '%s' has no usable characters after "
                 "slugging, refusing to add", name);
        return -1;
    }

    int idx = lr->count++;
    Level *lv = &lr->levels[idx];
    memset(lv, 0, sizeof(*lv));
    strncpy(lv->name, name, LEVEL_NAME_MAX - 1);
    snprintf(lv->world_path,  sizeof(lv->world_path),  "levels" PATH_SEP "%s_world.dge",    slug);
    snprintf(lv->entity_path, sizeof(lv->entity_path), "levels" PATH_SEP "%s_entities.dge", slug);
    lv->spawn_x = 0.0f;
    lv->spawn_y = 0.0f;
    lv->entry_marker[0] = '\0';

    if (lr->active_index < 0) lr->active_index = idx;

    LOG_INFO("Level added: '%s' -> %s", lv->name, lv->world_path);
    return idx;
}

const Level *level_registry_get(const LevelRegistry *lr, int index) {
    if (index < 0 || index >= lr->count) return NULL;
    return &lr->levels[index];
}

bool level_registry_set_active(LevelRegistry *lr, int index) {
    if (index < 0 || index >= lr->count) return false;
    lr->active_index = index;
    return true;
}

const Level *level_registry_active(const LevelRegistry *lr) {
    return level_registry_get(lr, lr->active_index);
}

void level_registry_bootstrap(LevelRegistry *lr) {
    if (lr->count > 0) return; /* already has at least one level */
    int idx = level_registry_add(lr, "Level 1");
    if (idx >= 0) lr->active_index = idx;
}

bool level_registry_migrate_legacy(const char *legacy_world_path,
                                    const char *legacy_entity_path,
                                    const Level *target) {
    if (!file_exists(legacy_world_path)) return false; /* nothing to migrate */

    dge_mkdir("levels");

    bool ok = true;
    if (rename(legacy_world_path, target->world_path) != 0) {
        LOG_ERROR("level_registry_migrate_legacy: could not move '%s' -> '%s'",
                  legacy_world_path, target->world_path);
        ok = false;
    }
    if (file_exists(legacy_entity_path)) {
        if (rename(legacy_entity_path, target->entity_path) != 0) {
            LOG_ERROR("level_registry_migrate_legacy: could not move '%s' -> '%s'",
                      legacy_entity_path, target->entity_path);
            ok = false;
        }
    }
    if (ok)
        LOG_INFO("Migrated pre-Level project files into '%s' (%s, %s)",
                 target->name, target->world_path, target->entity_path);
    return ok;
}

/* ---------------------------------------------------------------------
   Save / load — flat key=value manifest, same convention as
   project.dge/.theme files. */

bool level_registry_save(const LevelRegistry *lr, const char *manifest_path) {
    dge_mkdir("levels");

    FILE *f = fopen(manifest_path, "w");
    if (!f) {
        LOG_ERROR("level_registry_save: could not open '%s' for writing", manifest_path);
        return false;
    }

    fprintf(f, "count=%d\n", lr->count);
    fprintf(f, "active=%d\n", lr->active_index);
    for (int i = 0; i < lr->count; i++) {
        const Level *lv = &lr->levels[i];
        fprintf(f, "level%d_name=%s\n",     i, lv->name);
        fprintf(f, "level%d_world=%s\n",    i, lv->world_path);
        fprintf(f, "level%d_entities=%s\n", i, lv->entity_path);
        fprintf(f, "level%d_spawn_x=%.3f\n", i, lv->spawn_x);
        fprintf(f, "level%d_spawn_y=%.3f\n", i, lv->spawn_y);
        fprintf(f, "level%d_marker=%s\n",   i, lv->entry_marker);
    }

    fclose(f);
    LOG_INFO("level_registry_save: wrote '%s' (%d levels)", manifest_path, lr->count);
    return true;
}

bool level_registry_load(LevelRegistry *lr, const char *manifest_path) {
    FILE *f = fopen(manifest_path, "r");
    if (!f) {
        LOG_WARN("level_registry_load: '%s' not found", manifest_path);
        return false;
    }

    level_registry_init(lr);

    char line[512];
    int declared_count = 0;
    while (fgets(line, sizeof(line), f)) {
        char *nl = strchr(line, '\n'); if (nl) *nl = '\0';
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = line;
        const char *val = eq + 1;

        if (strcmp(key, "count") == 0) {
            declared_count = atoi(val);
            if (declared_count < 0) declared_count = 0;
            if (declared_count > LEVEL_MAX) declared_count = LEVEL_MAX;
            continue;
        }
        if (strcmp(key, "active") == 0) {
            lr->active_index = atoi(val);
            continue;
        }

        /* levelN_field — find N and field */
        if (strncmp(key, "level", 5) != 0) continue;
        int idx = atoi(key + 5);
        if (idx < 0 || idx >= LEVEL_MAX) continue;
        const char *underscore = strchr(key + 5, '_');
        if (!underscore) continue;
        const char *field = underscore + 1;

        if (idx >= lr->count) lr->count = idx + 1;
        Level *lv = &lr->levels[idx];

        if      (strcmp(field, "name")     == 0) strncpy(lv->name,        val, LEVEL_NAME_MAX - 1);
        else if (strcmp(field, "world")    == 0) strncpy(lv->world_path,  val, sizeof(lv->world_path) - 1);
        else if (strcmp(field, "entities") == 0) strncpy(lv->entity_path, val, sizeof(lv->entity_path) - 1);
        else if (strcmp(field, "spawn_x")  == 0) lv->spawn_x = (float)atof(val);
        else if (strcmp(field, "spawn_y")  == 0) lv->spawn_y = (float)atof(val);
        else if (strcmp(field, "marker")   == 0) strncpy(lv->entry_marker, val, LEVEL_NAME_MAX - 1);
    }
    fclose(f);

    /* Trust the parsed field-driven count over the declared "count="
       line if they disagree (e.g. a hand-edited manifest) -- but never
       exceed what was actually declared, so a truncated/corrupt file
       doesn't accidentally pick up trailing garbage as a phantom extra
       level. */
    if (declared_count > 0 && declared_count < lr->count)
        lr->count = declared_count;

    if (lr->active_index < 0 || lr->active_index >= lr->count)
        lr->active_index = lr->count > 0 ? 0 : -1;

    LOG_INFO("level_registry_load: '%s' (%d levels, active=%d)",
             manifest_path, lr->count, lr->active_index);
    return true;
}
