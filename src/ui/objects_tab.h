#ifndef DGE_OBJECTS_TAB_H
#define DGE_OBJECTS_TAB_H

/*  Object Editor tab.

    Three-column layout (within the content area below TABBAR_H):
      LEFT   — scrollable list of defined objects + [+ New] button
      CENTER — preview (sprite name shown at scale; full sprite picker popup)
      RIGHT  — Inspector: name, sprite field, properties, behaviors, Save/Delete

    This tab writes/reads through ObjectDefRegistry and disk (.obj files).
    The SpritesTab is referenced for sprite name lookup. */

#include <stdbool.h>
#include "../core/object_def.h"
#include "sprites_tab.h"
#include "textinput.h"

/* Maximum properties / behaviors the inspector can edit simultaneously */
#define OBJECTS_TAB_EDIT_PROPS  OBJDEF_MAX_PROPS
#define OBJECTS_TAB_EDIT_BEHAV  OBJDEF_MAX_BEHAV

typedef enum {
    OBJ_FOCUS_NONE = 0,
    OBJ_FOCUS_NAME,
    OBJ_FOCUS_SPRITE,
    OBJ_FOCUS_PROP_NAME,   /* property name field — index in focus_idx */
    OBJ_FOCUS_PROP_VALUE,  /* property value field */
    OBJ_FOCUS_BEHAV_EVENT, /* behavior event name */
    OBJ_FOCUS_BEHAV_SCRIPT,/* behavior script path */
} ObjFocus;

typedef struct {
    ObjectDefRegistry *registry;
    SpritesTab        *sprites;

    /* Selected definition in the list (-1 = none) */
    int selected_idx;
    int list_scroll;       /* top item index currently visible */

    /* Working copy being edited (only valid when selected_idx >= 0) */
    ObjectDef editing;

    /* Text fields for the editing session */
    TextInput fi_name;     /* object name */
    TextInput fi_sprite;   /* sprite name */

    /* Per-property name/value fields (parallel to editing.props[]) */
    TextInput fi_prop_name [OBJDEF_MAX_PROPS];
    TextInput fi_prop_value[OBJDEF_MAX_PROPS];

    /* Per-behavior event/script fields */
    TextInput fi_behav_event [OBJDEF_MAX_BEHAV];
    TextInput fi_behav_script[OBJDEF_MAX_BEHAV];

    /* Focus management */
    ObjFocus  focus;
    int       focus_idx;   /* which prop/behav row has focus */

    /* One-line status message */
    char status[128];

    /* New-object name scratch field (used in "+ New" flow) */
    TextInput fi_new_name;
    bool      new_mode;    /* true while the "new object" name field is shown */
} ObjectsTab;

void objects_tab_init(ObjectsTab *ot,
                       ObjectDefRegistry *registry,
                       SpritesTab *sprites);

/*  Per-frame update / render.  vw/vh = full viewport. */
void objects_tab_update(ObjectsTab *ot, int vw, int vh);
void objects_tab_render(const ObjectsTab *ot, int vw, int vh);

#endif /* DGE_OBJECTS_TAB_H */
