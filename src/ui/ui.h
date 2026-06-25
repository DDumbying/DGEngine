#ifndef DGE_UI_H
#define DGE_UI_H

#include "../simulation/simulation.h"
#include "../simulation/weather.h"
#include "../editor/editor.h"
#include "../ecs/registry.h"

/* Height of ui_render()'s own status row, directly below the tab bar
   (see ui.c) — exposed so main.c can fold it into the same top-margin
   it already passes to editor_update() for the tab bar itself, instead
   of the two constants drifting apart in two files. */
#define STATUS_BAR_H 28

/*  Composes the actual HUD: resource counts, sim clock/pause state,
    weather, editor mode, and a control-hint line. This is the one
    place that decides *what* the HUD shows; text.c only knows how to
    draw characters, it has no idea what a ResourceStore is.

    hud_x_origin is where the HUD starts horizontally — pass
    panel_effective_width() (see ui/panel.h) plus a small margin so the
    HUD slides over to fill the gap when the sidebar is hidden, instead
    of leaving a dead strip of screen.

    Call between renderer_begin_ui() and renderer_end(), i.e. after all
    world-space drawing for the frame is done. */
void ui_render(const ResourceStore *resources,
               const SimClock *clock,
               const WeatherSystem *weather,
               const Editor *editor,
               const Registry *registry,
               int viewport_w, int viewport_h,
               int world_w, int world_h,
               int hud_x_origin);

#endif /* DGE_UI_H */
