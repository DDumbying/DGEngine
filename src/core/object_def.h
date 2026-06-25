#ifndef DGE_OBJECT_DEF_H
#define DGE_OBJECT_DEF_H

/*  User-defined object types.

    An ObjectDef is the *template* for a category of entity — the
    data-driven replacement for hardcoded C enums like PREFAB_TREE.
    Each definition lives in objects/<name>.obj inside the project folder.

    File format (plain text, human-editable):

        name=tree
        sprite=tree_trunk
        property health int 100
        property speed float 2.5
        property drops string wood
        behavior on_click scripts/tree_click.lua
        behavior on_tick  scripts/tree_tick.lua

    Properties are a flat bag of typed name/value pairs — up to
    OBJDEF_MAX_PROPS per object.  Behavior slots map named events
    ("on_click", "on_tick", "on_build") to Lua script paths.

    The ObjectDefRegistry holds all loaded definitions and provides
    find-by-name lookup.  It is owned by main.c (EditorState) and
    passed wherever needed.                                              */

#include <stdbool.h>

#define OBJDEF_NAME_MAX    64
#define OBJDEF_MAX_PROPS   32
#define OBJDEF_MAX_BEHAV   8
#define OBJDEF_MAX_DEFS   128
#define OBJDEF_PATH_MAX   256

/* ---- Property -------------------------------------------------------- */

typedef enum {
    PROP_INT    = 0,
    PROP_FLOAT  = 1,
    PROP_STRING = 2,
    PROP_BOOL   = 3,
} PropertyType;

typedef union {
    int   as_int;
    float as_float;
    char  as_string[64];
    bool  as_bool;
} PropertyValue;

typedef struct {
    char          name[OBJDEF_NAME_MAX];
    PropertyType  type;
    PropertyValue value;
} ObjectProperty;

/* ---- Behavior slot --------------------------------------------------- */

typedef struct {
    char event[OBJDEF_NAME_MAX];   /* "on_click", "on_tick", "on_build" */
    char script[OBJDEF_PATH_MAX];  /* path to .lua file, relative to cwd */
} ObjectBehavior;

/* ---- Object Definition ----------------------------------------------- */

typedef struct {
    char           name[OBJDEF_NAME_MAX];   /* unique id, used as filename base */
    char           sprite[OBJDEF_NAME_MAX]; /* sprite name from sprites.meta     */

    ObjectProperty props[OBJDEF_MAX_PROPS];
    int            prop_count;

    ObjectBehavior behaviors[OBJDEF_MAX_BEHAV];
    int            behavior_count;
} ObjectDef;

/* ---- Registry -------------------------------------------------------- */

typedef struct {
    ObjectDef defs[OBJDEF_MAX_DEFS];
    int       count;
} ObjectDefRegistry;

/* Initialise an empty registry. */
void objdef_registry_init(ObjectDefRegistry *r);

/*  Load all *.obj files from the objects/ sub-directory (relative to
    cwd = project folder).  Safe to call if objects/ doesn't exist yet
    (silently succeeds with count=0).  Clears any previously loaded defs. */
void objdef_registry_load_all(ObjectDefRegistry *r);

/*  Find a definition by name.  Returns NULL if not found. */
ObjectDef *objdef_find(ObjectDefRegistry *r, const char *name);

/*  Save a single definition to objects/<def->name>.obj.
    Returns false on I/O error. */
bool objdef_save(const ObjectDef *def);

/*  Delete objects/<name>.obj from disk.
    Returns false if the file didn't exist or could not be removed. */
bool objdef_delete(const char *name);

/*  Parse a single .obj file into *def.
    Returns false on format error. */
bool objdef_load_file(ObjectDef *def, const char *path);

/* ---- Property helpers ------------------------------------------------ */

/*  Returns the string representation of a PropertyType (for UI display). */
const char *prop_type_name(PropertyType t);

/*  Stringify a property value into buf (for inspector display). */
void prop_value_to_str(char *buf, int bufsize,
                        PropertyType t, const PropertyValue *v);

/*  Parse a string into a PropertyValue for the given type.
    Returns false if the string is not valid for that type. */
bool prop_value_from_str(PropertyType t, const char *str, PropertyValue *out);

#endif /* DGE_OBJECT_DEF_H */
