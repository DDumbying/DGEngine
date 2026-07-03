#ifndef DGE_SHAPE_PANE_H
#define DGE_SHAPE_PANE_H

/*  Shape Pane — a flat, plain-grid (non-isometric) view for painting the
    world's playable-area mask, docked to the right edge of the world view
    while EDITOR_MODE_SHAPE is active.

    WHY THIS EXISTS (the problem it replaces):
    Before this, the only way to paint world->shape was to click directly
    on the live isometric diamond tiles — same camera, same pan/zoom, same
    projection math as normal terrain painting. Diamond hit-testing is
    fine for "where do I put this tree" but is a genuinely worse way to
    *think* about a shape: you're reasoning about a 2D boolean mask, and
    a 60° rotated rhombus grid fights that reasoning instead of helping it.

    WHY IT'S LIVE-LINKED, NOT MODAL:
    world_shape_set()/world_shape_fill_rect() (see world/world_shape.h)
    are pure data operations — they have no idea whether an isometric
    diamond or a flat square triggered the call. world_render() already
    reads world->shape every frame to decide which tiles to draw. That
    means there is no second buffer to maintain and no "Apply" step to
    get right: painting in this flat pane writes directly into the same
    WorldShape struct the iso view already renders from, so the change
    is visible in the iso world the very next frame, the same way two
    mirrors pointed at the same object both update instantly — there's
    only one object (one WorldShape), just two ways of looking at it.

    LAYOUT:
    Docked to the right edge of the world viewport, fixed pixel width
    (SHAPE_PANE_W). Cells are simple squares (renderer_draw_quad), one
    cell per world tile, with pan (drag) and zoom (scroll) independent
    of the main isometric camera — so you can zoom into the flat grid to
    place individual holes precisely without that affecting how the iso
    view is currently framed, and vice versa.

    INPUT OWNERSHIP:
    Only live while ed->mode == EDITOR_MODE_SHAPE. shape_pane_update()
    must run BEFORE editor_update() in main.c's per-frame order, and its
    return value tells the caller whether the click landed inside the
    pane — if so, the caller must skip editor_update()'s own SHAPE
    handling for that frame so a click can't paint both the flat pane's
    cell AND the iso tile underneath it at the same screen position.   */

#include <stdbool.h>
#include "../world/world.h"

#define SHAPE_PANE_W 320   /* fixed width, right-docked */

typedef struct {
    /* Flat-grid camera — independent of the main isometric Camera */
    float pan_x, pan_y;     /* world-tile offset of the top-left visible cell (fractional, for smooth pan) */
    float cell_px;          /* current on-screen size of one cell, in pixels (zoom) */

    /* Drag-to-pan state */
    bool  panning;
    int   pan_start_mx, pan_start_my;
    float pan_start_x, pan_start_y;

    /* Shift+drag rect-fill state (mirrors Editor's shape_dragging, but
       this pane has its own input surface so it needs its own copy) */
    bool  rect_dragging;
    int   rect_x0, rect_y0;

    /* Last hovered cell this frame, for the render pass's highlight —
       same "compute once in update, reuse in render" pattern editor.c
       already uses for hover_valid/hover_gx/hover_gy. */
    bool  hover_valid;
    int   hover_gx, hover_gy;
} ShapePane;

void shape_pane_init(ShapePane *sp);

/*  Resets pan/zoom to frame the whole world grid — call when entering
    EDITOR_MODE_SHAPE so the pane doesn't open scrolled to wherever a
    previous session left it relative to a since-resized world. */
void shape_pane_fit_to_world(ShapePane *sp, const World *world, int viewport_h);

/*  Returns true if the mouse is currently over the pane's rect — callers
    use this to skip the isometric editor's own SHAPE-mode handling for
    the frame, preventing a double-paint at two different grid positions
    from one click. Pass the same World the iso view renders so painting
    here writes into the exact struct world_render() reads from.

    viewport_w/h are the full window dimensions; the pane docks itself
    to the right edge using SHAPE_PANE_W, same convention as panel.h's
    PANEL_WIDTH docking to the left edge. */
bool shape_pane_update(ShapePane *sp, World *world, int viewport_w, int viewport_h);

/*  Draws the flat grid, the enabled/disabled cell coloring, the hover
    highlight, and an in-progress rect-fill preview if dragging. Also
    draws the pane's background/border so it reads as a distinct panel
    docked against the world view, not a transparent overlay.         */
void shape_pane_render(const ShapePane *sp, const World *world, int viewport_w, int viewport_h);

#endif /* DGE_SHAPE_PANE_H */
