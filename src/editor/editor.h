#ifndef DGE_EDITOR_H
#define DGE_EDITOR_H

#include <stdbool.h>
#include "../ecs/registry.h"
#include "../world/world.h"
#include "../renderer/camera.h"
#include "../game/prefabs.h"

/*  No on-screen UI yet (no text renderer exists — see renderer/texture.h's
    note that textures land later too). Mode/brush/prefab switch via
    keyboard and announce themselves with LOG_INFO, the same pattern
    main.c already uses for "World generated", "World saved", etc. A
    visual HUD (mode indicator, brush palette) is the natural upgrade
    once there's a text/UI system — tracked, not blocking this phase. */
typedef enum {
    EDITOR_MODE_PAINT = 0,  /* left-click+drag paints terrain          */
    EDITOR_MODE_PLACE,      /* left-click places a prefab, right deletes */
    EDITOR_MODE_SELECT,     /* left-click selects, prints entity info  */
    EDITOR_MODE_COUNT
} EditorMode;

typedef struct {
    EditorMode   mode;
    TerrainType  brush;     /* active terrain type in PAINT mode */
    PrefabKind   prefab;    /* active prefab in PLACE mode */

    EntityHandle selected;  /* ENTITY_HANDLE_NULL when nothing selected */

    /* Hovered tile this frame, kept around so editor_render() can draw
       the same highlight editor_update() used to decide what to act on. */
    bool hover_valid;
    int  hover_gx, hover_gy;
} Editor;

void editor_init(Editor *ed);

/* Reads keyboard/mouse state and mutates world/registry accordingly.
   Call once per frame, after input_begin_frame()/event polling and
   after the camera's viewport size is current for this frame. */
void editor_update(Editor *ed, Registry *reg, World *world, const Camera *cam);

/* Draws ground-level highlights only (hover + selection markers).
   Call between world_render() and system_render_entities() — that
   ordering matters: highlights are flat tiles, so anything drawn after
   them (entities) renders on top, the way it should for a marker
   *under* an object rather than an overlay on top of it. Takes the
   registry (read-only) so the selection highlight is resolved through
   entity_handle_valid() each frame instead of trusting a cached
   position that could go stale once something other than the editor
   is able to destroy entities (Simulation/AI, later phases). */
void editor_render(const Editor *ed, Registry *reg);

#endif /* DGE_EDITOR_H */
