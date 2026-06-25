#include "object_def.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
#  include <direct.h>
#  include <windows.h>
#else
#  include <dirent.h>
#  include <sys/stat.h>
#endif

#include "log.h"

/* -----------------------------------------------------------------------
   Property helpers */

const char *prop_type_name(PropertyType t) {
    switch (t) {
        case PROP_INT:    return "int";
        case PROP_FLOAT:  return "float";
        case PROP_STRING: return "string";
        case PROP_BOOL:   return "bool";
        default:          return "?";
    }
}

void prop_value_to_str(char *buf, int bufsize,
                        PropertyType t, const PropertyValue *v) {
    switch (t) {
        case PROP_INT:    snprintf(buf, (size_t)bufsize, "%d",  v->as_int);    break;
        case PROP_FLOAT:  snprintf(buf, (size_t)bufsize, "%.4g",v->as_float);  break;
        case PROP_STRING: snprintf(buf, (size_t)bufsize, "%s",  v->as_string); break;
        case PROP_BOOL:   snprintf(buf, (size_t)bufsize, "%s",  v->as_bool ? "true" : "false"); break;
        default:          snprintf(buf, (size_t)bufsize, "?"); break;
    }
}

bool prop_value_from_str(PropertyType t, const char *str, PropertyValue *out) {
    switch (t) {
        case PROP_INT: {
            char *end;
            long v = strtol(str, &end, 10);
            if (end == str) return false;
            out->as_int = (int)v;
            return true;
        }
        case PROP_FLOAT: {
            char *end;
            float v = strtof(str, &end);
            if (end == str) return false;
            out->as_float = v;
            return true;
        }
        case PROP_STRING:
            strncpy(out->as_string, str, sizeof(out->as_string) - 1);
            out->as_string[sizeof(out->as_string) - 1] = '\0';
            return true;
        case PROP_BOOL:
            if (strcmp(str, "true") == 0 || strcmp(str, "1") == 0) {
                out->as_bool = true; return true;
            }
            if (strcmp(str, "false") == 0 || strcmp(str, "0") == 0) {
                out->as_bool = false; return true;
            }
            return false;
        default:
            return false;
    }
}

/* -----------------------------------------------------------------------
   File I/O */

bool objdef_load_file(ObjectDef *def, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return false;

    memset(def, 0, sizeof(*def));
    char line[512];

    while (fgets(line, sizeof line, f)) {
        /* Strip trailing newline */
        char *nl = strrchr(line, '\n');
        if (nl) *nl = '\0';
        char *cr = strrchr(line, '\r');
        if (cr) *cr = '\0';

        if (strncmp(line, "name=", 5) == 0) {
            strncpy(def->name, line + 5, OBJDEF_NAME_MAX - 1);

        } else if (strncmp(line, "sprite=", 7) == 0) {
            strncpy(def->sprite, line + 7, OBJDEF_NAME_MAX - 1);

        } else if (strncmp(line, "property ", 9) == 0) {
            if (def->prop_count >= OBJDEF_MAX_PROPS) continue;
            char pname[OBJDEF_NAME_MAX], ptype[16], pval[128];
            if (sscanf(line + 9, "%63s %15s %127[^\n]", pname, ptype, pval) < 2)
                continue;

            ObjectProperty *p = &def->props[def->prop_count];
            strncpy(p->name, pname, OBJDEF_NAME_MAX - 1);

            if      (strcmp(ptype, "int")    == 0) p->type = PROP_INT;
            else if (strcmp(ptype, "float")  == 0) p->type = PROP_FLOAT;
            else if (strcmp(ptype, "string") == 0) p->type = PROP_STRING;
            else if (strcmp(ptype, "bool")   == 0) p->type = PROP_BOOL;
            else continue; /* unknown type, skip */

            /* Default value may be absent — that's fine */
            if (pval[0])
                prop_value_from_str(p->type, pval, &p->value);

            def->prop_count++;

        } else if (strncmp(line, "behavior ", 9) == 0) {
            if (def->behavior_count >= OBJDEF_MAX_BEHAV) continue;
            char event[OBJDEF_NAME_MAX], script[OBJDEF_PATH_MAX];
            if (sscanf(line + 9, "%63s %255[^\n]", event, script) < 2) continue;

            ObjectBehavior *b = &def->behaviors[def->behavior_count++];
            strncpy(b->event,  event,  OBJDEF_NAME_MAX - 1);
            strncpy(b->script, script, OBJDEF_PATH_MAX - 1);
        }
        /* Lines starting with '#' or unknown keys are silently ignored */
    }

    fclose(f);
    return def->name[0] != '\0';  /* must have at least a name */
}

bool objdef_save(const ObjectDef *def) {
    if (!def->name[0]) return false;

    char path[OBJDEF_PATH_MAX];
    snprintf(path, sizeof path, "objects/%s.obj", def->name);

    FILE *f = fopen(path, "w");
    if (!f) {
        LOG_WARN("objdef_save: cannot write '%s': %s", path, strerror(errno));
        return false;
    }

    fprintf(f, "name=%s\n", def->name);
    if (def->sprite[0])
        fprintf(f, "sprite=%s\n", def->sprite);

    for (int i = 0; i < def->prop_count; i++) {
        const ObjectProperty *p = &def->props[i];
        char val[128];
        prop_value_to_str(val, sizeof val, p->type, &p->value);
        fprintf(f, "property %s %s %s\n", p->name, prop_type_name(p->type), val);
    }

    for (int i = 0; i < def->behavior_count; i++) {
        const ObjectBehavior *b = &def->behaviors[i];
        fprintf(f, "behavior %s %s\n", b->event, b->script);
    }

    fclose(f);
    LOG_INFO("ObjectDef saved: objects/%s.obj", def->name);
    return true;
}

bool objdef_delete(const char *name) {
    char path[OBJDEF_PATH_MAX + 16];
    snprintf(path, sizeof path, "objects/%s.obj", name);
    if (remove(path) != 0) {
        LOG_WARN("objdef_delete: cannot remove '%s': %s", path, strerror(errno));
        return false;
    }
    LOG_INFO("ObjectDef deleted: objects/%s.obj", name);
    return true;
}

/* -----------------------------------------------------------------------
   Registry */

void objdef_registry_init(ObjectDefRegistry *r) {
    memset(r, 0, sizeof(*r));
}

void objdef_registry_load_all(ObjectDefRegistry *r) {
    r->count = 0;

#ifdef _WIN32
    WIN32_FIND_DATA fd;
    HANDLE h = FindFirstFile("objects\\*.obj", &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (r->count >= OBJDEF_MAX_DEFS) break;
        char path[OBJDEF_PATH_MAX];
        snprintf(path, sizeof path, "objects\\%s", fd.cFileName);
        ObjectDef tmp;
        if (objdef_load_file(&tmp, path))
            r->defs[r->count++] = tmp;
    } while (FindNextFile(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir("objects");
    if (!d) return;  /* directory doesn't exist yet — fine */
    struct dirent *ent;
    while ((ent = readdir(d)) && r->count < OBJDEF_MAX_DEFS) {
        const char *n = ent->d_name;
        size_t len = strlen(n);
        if (len < 5 || strcmp(n + len - 4, ".obj") != 0) continue;
        char path[OBJDEF_PATH_MAX];
        snprintf(path, sizeof path, "objects/%s", n);
        ObjectDef tmp;
        if (objdef_load_file(&tmp, path))
            r->defs[r->count++] = tmp;
    }
    closedir(d);
#endif

    LOG_INFO("ObjectDef registry: %d definition(s) loaded.", r->count);
}

ObjectDef *objdef_find(ObjectDefRegistry *r, const char *name) {
    for (int i = 0; i < r->count; i++)
        if (strncmp(r->defs[i].name, name, OBJDEF_NAME_MAX) == 0)
            return &r->defs[i];
    return NULL;
}

/* Local property lookup -- prefabs.c has its own copy of this same
   shape for the same reason (different translation unit, ~10 lines,
   not worth a shared header just for this). See its comment for why
   "wrong type" is treated the same as "absent" rather than erroring. */
static const ObjectProperty *find_prop(const ObjectDef *def, const char *name, PropertyType type) {
    for (int i = 0; i < def->prop_count; i++) {
        if (def->props[i].type == type && strcmp(def->props[i].name, name) == 0)
            return &def->props[i];
    }
    return NULL;
}

bool objdef_is_buildable(const ObjectDef *def) {
    return find_prop(def, "build_time", PROP_FLOAT) != NULL;
}

void objdef_get_build_spec(const ObjectDef *def, ResourceKind *out_cost_kind,
                            int *out_cost_amount, float *out_build_time) {
    const ObjectProperty *bt = find_prop(def, "build_time", PROP_FLOAT);
    *out_build_time = bt ? bt->value.as_float : 0.0f;

    const ObjectProperty *kind_prop = find_prop(def, "build_cost_kind", PROP_STRING);
    *out_cost_kind = RESOURCE_WOOD; /* default, including when kind_prop names anything else */
    if (kind_prop && strcmp(kind_prop->value.as_string, "stone") == 0)
        *out_cost_kind = RESOURCE_STONE;

    const ObjectProperty *amount_prop = find_prop(def, "build_cost_amount", PROP_INT);
    *out_cost_amount = amount_prop ? amount_prop->value.as_int : 0;
}
