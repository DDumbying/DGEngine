#include "ui.h"

#include <stdio.h>
#include "text.h"
#include "../renderer/renderer.h"
#include "../simulation/construction.h"
#include "../ui/tabbar.h"
#include "../world/tileset.h"

/*  Status strip — its own full-width row directly below the tab bar
    (y = TABBAR_H .. TABBAR_H+STATUS_BAR_H, see ui.h), not sharing the
    tab bar's own row. It used to be drawn right-aligned inside row
    0..TABBAR_H, which on most window widths landed it on top of the
    SPRITES/SCRIPTS/SETTINGS tab buttons — those buttons each take
    vw/TAB_COUNT of the *entire* width (see tabbar.c), so there is no
    free space anywhere in that row for anything else to share. Giving
    the status strip its own row is the only fix that doesn't depend on
    window width.

    STATUS_BAR_H (defined in ui.h) matches the gap panel.c already
    reserves before its own first button (panel.c's content starts at
    y=34+28=62) — that 28 was already set aside for exactly this, it
    just wasn't being used here yet. */

void ui_render(const ResourceStore *resources,
               const SimClock *clock,
               const WeatherSystem *weather,
               const Editor *editor,
               const Registry *registry,
               const World *world,
               GenreProfile genre,
               int viewport_w, int viewport_h,
               int world_w, int world_h,
               int hud_x_origin) {
    (void)hud_x_origin;
    (void)viewport_h;
    (void)world_w; (void)world_h;
    (void)registry;

    /* Build a compact single-line status string */
    char buf[256];
    float sc = 1.5f;

    /* Mode + brush/prefab — always shown, every genre has an editor mode */
    /* OBJDEF_NAME_MAX (64) + editor mode label + separator — sized so
       even the longest possible place_def_name can't be truncated,
       silencing (correctly) what -Wformat-truncation flagged: the old
       48-byte buffer couldn't provably fit a full-length ObjectDef
       name here. */
    char mode[96];
    if (editor->mode == EDITOR_MODE_PAINT) {
        /* Brush name now comes from the project's own Tileset, not a
           fixed terrain_name() the engine used to ship — see
           world/tileset.h. -1/undefined shows as "(none)" rather than
           crashing into an out-of-range slot lookup. */
        const char *brush_name = (editor->brush >= 0)
            ? tileset_name_for(&world->tileset, editor->brush)
            : "(none)";
        snprintf(mode, sizeof mode, "%s:%s",
                 editor_mode_name(editor->mode), brush_name);
    }
    else if (editor->mode == EDITOR_MODE_PLACE) {
        /* PLACE mode's stamp name now comes straight from place_def_name
           (the selected ObjectDef) — see editor.h's doc comment on why
           PrefabKind/BuildingKind retired. Empty means nothing picked
           yet, same "(none)" treatment as an unselected PAINT brush. */
        const char *stamp_name = editor->place_def_name[0]
            ? editor->place_def_name : "(none)";
        snprintf(mode, sizeof mode, "%s:%s",
                 editor_mode_name(editor->mode), stamp_name);
    }
    else
        snprintf(mode, sizeof mode, "%s", editor_mode_name(editor->mode));

    /* Tile coord — always shown */
    char tile[24];
    if (editor->hover_valid)
        snprintf(tile, sizeof tile, "T:%d,%d", editor->hover_gx, editor->hover_gy);
    else
        snprintf(tile, sizeof tile, "T:--");

    if (genre == GENRE_SANDBOX_SIM) {
        /* Resources */
        char res[64];
        snprintf(res, sizeof res, "W:%d  S:%d", resources->wood, resources->stone);

        /* Time */
        char tim[32];
        int ms = (int)clock->elapsed;
        if (simclock_is_paused(clock))
            snprintf(tim, sizeof tim, "%02d:%02d PAUSED", (ms/60)%60, ms%60);
        else
            snprintf(tim, sizeof tim, "%02d:%02d", (ms/60)%60, ms%60);

        /* Weather */
        char wx[24];
        snprintf(wx, sizeof wx, "%s", weather_name(weather->type));

        snprintf(buf, sizeof buf, "%s  |  %s  |  %s  |  %s  |  %s",
                 res, tim, wx, mode, tile);
    } else {
        /* TACTICS / FREEFORM: no resources, no clock, no weather
           ticking (see main.c's genre-gated simulation tick) — a
           status line with those would just show permanently-zero
           or frozen values, which reads as broken rather than as
           "this genre doesn't use that". Show the genre name itself
           instead, so it's still obvious at a glance which fork of
           the engine this project is running. */
        snprintf(buf, sizeof buf, "%s  |  %s  |  %s",
                 genre_profile_name(genre), mode, tile);
    }

    float tw = text_measure_width(buf, sc);
    float th = text_line_height(sc);
    float tx = (float)viewport_w - tw - 8.0f;
    float ty = (float)TABBAR_H + ((float)STATUS_BAR_H - th) * 0.5f;

    /* Full-width backing strip for the whole status row, not just a
       pill behind the text — this is the row's only occupant, so it
       reads as a deliberate second bar rather than a floating label. */
    renderer_draw_quad(0.0f, (float)TABBAR_H, (float)viewport_w, (float)STATUS_BAR_H,
                       0.08f, 0.08f, 0.10f, 0.85f);
    renderer_draw_quad(0.0f, (float)(TABBAR_H + STATUS_BAR_H) - 1.0f, (float)viewport_w, 1.0f,
                       0.22f, 0.22f, 0.26f, 1.0f);

    float tr = 0.75f, tg = 0.75f, tb = 0.78f;
    if (genre == GENRE_SANDBOX_SIM && simclock_is_paused(clock)) { tr=1.0f; tg=0.85f; tb=0.3f; }
    text_draw(tx, ty, sc, tr, tg, tb, 1.0f, buf);
}
