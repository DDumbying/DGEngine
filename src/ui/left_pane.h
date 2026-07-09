#ifndef DGE_LEFT_PANE_H
#define DGE_LEFT_PANE_H

#include <stdbool.h>
#include "../ecs/registry.h"
#include "../editor/editor.h"
#include "../world/world.h"
#include "../core/object_def.h"
#include "../renderer/atlas.h"
#include "textinput.h"

#define LEFT_PANE_WIDTH 250

typedef enum {
    LEFT_TAB_SCENE = 0,
    LEFT_TAB_FILES,
    LEFT_TAB_PALETTE,
    LEFT_TAB_COUNT
} LeftTab;

typedef struct {
    bool is_collapsed;
    LeftTab active_tab;

    /* Scene Tab */
    int scene_scroll_y;
    Entity selected_entity;

    /* Files Tab */
    int files_scroll_y;

    /* Palette Tab */
    int palette_scroll_y;
    int renaming_slot;
    TextInput rename_field;
    int assigning_sprite_slot;
    int assign_sprite_scroll;
} LeftPane;

void left_pane_init(LeftPane *lp);

/* Returns true if the pointer is currently over the left pane */
bool left_pane_update(LeftPane *lp, Editor *ed, Registry *reg, World *world,
                      const SpriteAtlas *atlas, const ObjectDefRegistry *obj_registry,
                      int viewport_h);

void left_pane_render(const LeftPane *lp, const Editor *ed, const Registry *reg,
                      const World *world, const SpriteAtlas *atlas,
                      const ObjectDefRegistry *obj_registry, int viewport_h);

#endif /* DGE_LEFT_PANE_H */
