#include "project.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

/* ---- Portability shims -------------------------------------------- */

#ifdef _WIN32
#  include <direct.h>
#  define dge_mkdir(p) _mkdir(p)
#  define PATH_SEP "\\"
#else
#  include <unistd.h>
#  define dge_mkdir(p) mkdir((p), 0755)
#  define PATH_SEP "/"
#endif

/* ---- Helpers ------------------------------------------------------ */

void project_defaults(Project *p) {
    memset(p, 0, sizeof(*p));
    strncpy(p->name, "untitled", PROJECT_NAME_MAX - 1);
    p->grid_w = 64;
    p->grid_h = 64;
    p->tile_w = 64;
    p->tile_h = 32;
}

bool project_folder_valid(const char *folder) {
    char path[PROJECT_PATH_MAX + 16];
    snprintf(path, sizeof path, "%s" PATH_SEP "project.dge", folder);
    FILE *f = fopen(path, "r");
    if (!f) return false;
    fclose(f);
    return true;
}

/* ---- project.dge save/load ---------------------------------------- */

bool project_save(const Project *p) {
    char path[PROJECT_PATH_MAX + 16];
    snprintf(path, sizeof path, "%s" PATH_SEP "project.dge", p->path);

    FILE *f = fopen(path, "w");
    if (!f) {
        LOG_ERROR("project_save: cannot open '%s': %s", path, strerror(errno));
        return false;
    }
    fprintf(f, "name=%s\n",   p->name);
    fprintf(f, "grid_w=%d\n", p->grid_w);
    fprintf(f, "grid_h=%d\n", p->grid_h);
    fprintf(f, "tile_w=%d\n", p->tile_w);
    fprintf(f, "tile_h=%d\n", p->tile_h);
    fclose(f);
    LOG_INFO("project_save: wrote '%s'", path);
    return true;
}

bool project_load(Project *p, const char *folder) {
    /* Always store the folder so the caller knows where we tried. */
    strncpy(p->path, folder, PROJECT_PATH_MAX - 1);
    p->path[PROJECT_PATH_MAX - 1] = '\0';

    char path[PROJECT_PATH_MAX + 16];
    snprintf(path, sizeof path, "%s" PATH_SEP "project.dge", folder);

    FILE *f = fopen(path, "r");
    if (!f) {
        LOG_WARN("project_load: '%s' not found", path);
        return false;
    }

    /* Safe defaults before parsing — a partial file still produces a
       usable project rather than garbage. */
    project_defaults(p);
    strncpy(p->path, folder, PROJECT_PATH_MAX - 1);

    char line[128];
    while (fgets(line, sizeof line, f)) {
        /* Strip trailing newline/CR. */
        size_t n = strlen(line);
        while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r'))
            line[--n] = '\0';

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = line;
        const char *val = eq + 1;

        if (strcmp(key, "name")   == 0) strncpy(p->name, val, PROJECT_NAME_MAX - 1);
        else if (strcmp(key, "grid_w") == 0) p->grid_w = atoi(val);
        else if (strcmp(key, "grid_h") == 0) p->grid_h = atoi(val);
        else if (strcmp(key, "tile_w") == 0) p->tile_w = atoi(val);
        else if (strcmp(key, "tile_h") == 0) p->tile_h = atoi(val);
    }
    fclose(f);

    /* Clamp grid dimensions to sane range so a hand-edited project.dge
       with nonsense values doesn't crash world_create(). */
    if (p->grid_w < PROJECT_GRID_MIN) p->grid_w = PROJECT_GRID_MIN;
    if (p->grid_h < PROJECT_GRID_MIN) p->grid_h = PROJECT_GRID_MIN;
    if (p->grid_w > PROJECT_GRID_MAX) p->grid_w = PROJECT_GRID_MAX;
    if (p->grid_h > PROJECT_GRID_MAX) p->grid_h = PROJECT_GRID_MAX;
    if (p->tile_w < 8)  p->tile_w = 8;
    if (p->tile_h < 4)  p->tile_h = 4;

    LOG_INFO("project_load: '%s' (grid %dx%d)", p->name, p->grid_w, p->grid_h);
    return true;
}

/* ---- Recent list -------------------------------------------------- */

static const char *recent_path(void) {
    /* ~/.dgengine — SDL_GetPrefPath is not available here (no SDL
       dependency in core/), so we use HOME directly. Falls back to
       the current directory on platforms without HOME. */
    static char buf[PROJECT_PATH_MAX];
    if (buf[0]) return buf;  /* computed once */

    const char *home = getenv("HOME");
#ifdef _WIN32
    if (!home) home = getenv("USERPROFILE");
#endif
    if (home)
        snprintf(buf, sizeof buf, "%s" PATH_SEP ".dgengine", home);
    else
        snprintf(buf, sizeof buf, ".dgengine");
    return buf;
}

int project_recent_load(char paths[PROJECT_RECENT_MAX][PROJECT_PATH_MAX]) {
    for (int i = 0; i < PROJECT_RECENT_MAX; i++)
        paths[i][0] = '\0';

    FILE *f = fopen(recent_path(), "r");
    if (!f) return 0;

    int count = 0;
    char line[PROJECT_PATH_MAX];
    while (count < PROJECT_RECENT_MAX && fgets(line, sizeof line, f)) {
        size_t n = strlen(line);
        while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r'))
            line[--n] = '\0';
        if (n == 0) continue;
        snprintf(paths[count], PROJECT_PATH_MAX, "%s", line);
        count++;
    }
    fclose(f);
    return count;
}

void project_recent_add(const char *folder) {
    char paths[PROJECT_RECENT_MAX][PROJECT_PATH_MAX];
    int  count = project_recent_load(paths);

    /* Deduplicate: remove existing entry for this folder. */
    int out = 0;
    for (int i = 0; i < count; i++) {
        if (strcmp(paths[i], folder) != 0)
            memcpy(paths[out++], paths[i], PROJECT_PATH_MAX);
    }

    /* Prepend. */
    int new_count = (out < PROJECT_RECENT_MAX - 1) ? out : PROJECT_RECENT_MAX - 1;
    /* Shift existing entries down to make room at index 0. */
    memmove(paths[1], paths[0], (size_t)new_count * PROJECT_PATH_MAX);
    strncpy(paths[0], folder, PROJECT_PATH_MAX - 1);
    new_count++;

    FILE *f = fopen(recent_path(), "w");
    if (!f) {
        LOG_WARN("project_recent_add: cannot write '%s'", recent_path());
        return;
    }
    for (int i = 0; i < new_count; i++)
        fprintf(f, "%s\n", paths[i]);
    fclose(f);
}

void project_recent_remove(const char *folder) {
    char paths[PROJECT_RECENT_MAX][PROJECT_PATH_MAX];
    int count = project_recent_load(paths);
    int out = 0;
    char new_paths[PROJECT_RECENT_MAX][PROJECT_PATH_MAX];
    for (int i = 0; i < count; i++) {
        if (strcmp(paths[i], folder) != 0) {
            strncpy(new_paths[out++], paths[i], PROJECT_PATH_MAX - 1);
            new_paths[out - 1][PROJECT_PATH_MAX - 1] = '\0';
        }
    }
    FILE *f = fopen(recent_path(), "w");
    if (!f) return;
    for (int i = 0; i < out; i++) {
        fprintf(f, "%s\n", new_paths[i]);
    }
    fclose(f);
}

void project_recent_clear(void) {
    FILE *f = fopen(recent_path(), "w");
    if (f) fclose(f);
}

bool project_delete_from_disk(const char *folder) {
    if (!folder || !folder[0]) return false;
#ifdef _WIN32
    // On Windows, system RM is shell-dependent, but we can call rmdir /s /q
    char cmd[512];
    snprintf(cmd, sizeof cmd, "rmdir /s /q \"%s\"", folder);
    return system(cmd) == 0;
#else
    char cmd[512];
    snprintf(cmd, sizeof cmd, "rm -rf \"%s\"", folder);
    return system(cmd) == 0;
#endif
}

/* ---- Directory creation ------------------------------------------- */

void project_create_dirs(const Project *p) {
    char sub[PROJECT_PATH_MAX + 16];
    const char *subs[] = { "objects", "scripts", "assets", NULL };
    for (int i = 0; subs[i]; i++) {
        snprintf(sub, sizeof sub, "%s" PATH_SEP "%s", p->path, subs[i]);
        if (dge_mkdir(sub) != 0 && errno != EEXIST)
            LOG_WARN("project_create_dirs: mkdir '%s': %s", sub, strerror(errno));
    }
}
