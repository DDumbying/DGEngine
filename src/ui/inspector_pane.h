#ifndef DGE_INSPECTOR_PANE_H
#define DGE_INSPECTOR_PANE_H

#include <stdbool.h>
#include "../ecs/registry.h"
#include "../editor/editor.h"

#define INSPECTOR_PANE_WIDTH 300

#include "textinput.h"

typedef struct {
    bool is_collapsed;
    int scroll_y;
    int editing_field;       /* 0 = none, 1 = Transform X, 2 = Transform Y */
    Entity editing_entity;   /* which entity was selected when editing started */
    TextInput input_field;
} InspectorPane;

void inspector_pane_init(InspectorPane *ip);

/* Returns true if the pointer is currently over the inspector pane */
bool inspector_pane_update(InspectorPane *ip, Editor *ed, Registry *reg, Entity selected_entity, int viewport_w, int viewport_h);

void inspector_pane_render(const InspectorPane *ip, const Editor *ed, const Registry *reg, Entity selected_entity, int viewport_w, int viewport_h);

#endif /* DGE_INSPECTOR_PANE_H */
