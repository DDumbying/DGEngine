#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

#ifdef _WIN32
#  include <direct.h>
#  define chdir _chdir
#else
#  include <unistd.h>
#endif

#include "core/log.h"
#include "core/time.h"
#include "core/project.h"
#include "core/object_def.h"
#include "core/playmode.h"
#include "platform/window.h"
#include "platform/input.h"
#include "renderer/renderer.h"
#include "renderer/camera.h"
#include "renderer/atlas.h"
#include "renderer/asset_library.h"
#include "world/world.h"
#include "world/world_generator.h"
#include "world/spatial_grid.h"
#include "ecs/registry.h"
#include "ecs/systems.h"
#include "game/prefabs.h"
#include "editor/editor.h"
#include "simulation/simulation.h"
#include "simulation/harvest.h"
#include "simulation/weather.h"
#include "simulation/construction.h"
#include "ai/agent.h"
#include "ai/pathfinder.h"
#include "ui/ui.h"
#include "ui/text.h"
#include "ui/font_atlas.h"
#include "ui/panel.h"
#include "ui/shape_pane.h"
#include "ui/minimap.h"
#include "ui/project_manager.h"
#include "ui/tabbar.h"
#include "ui/sprites_tab.h"
#include "ui/objects_tab.h"
#include "ui/settings_tab.h"
#include "ui/theme.h"
#include "scripting/lua_host.h"

#define WINDOW_W 1280
#define WINDOW_H  720

#define WORLD_SAVE_PATH   "world.dge"
#define ENTITY_SAVE_PATH  "entities.dge"
#define SIM_SAVE_PATH     "sim.dge"
#define WEATHER_SAVE_PATH "weather.dge"

typedef enum {
    SCREEN_PROJECT_MANAGER,
    SCREEN_EDITOR,
} Screen;

/* Shim: bridges AgentEventCB(entity, event, userdata) signature to
   lua_host_call_behavior(host, entity, event).  File-scope so it's
   valid C11 (nested functions are a GCC extension, not standard). */
static void lua_event_cb(Entity e, const char *event, void *ud) {
    lua_host_call_behavior((LuaHost *)ud, e, event);
}

int main(void) {
    Window window;
    if (!window_create(&window, "DGEngine", WINDOW_W, WINDOW_H))
        return 1;

    if (!renderer_init()) {
        window_destroy(&window);
        return 1;
    }
    renderer_set_tile_size(64.0f, 32.0f);

    /* Phase 3 — texture font atlas (graceful fallback to bitmap if absent) */
    if (!font_atlas_load("assets/font.png"))
        LOG_WARN("Font atlas not found — falling back to legacy bitmap font");

    /* Theme — single global palette every UI file reads through
       theme_current(). Tries themes/default.theme relative to the
       binary's launch cwd (not the project folder — themes are an
       engine-level preference, not per-project, so this load happens
       once here rather than inside ENTER_EDITOR's per-project chdir). */
    theme_reset_default();
    theme_load("themes/default.theme");

    dge_time_tick();

    /* ---- Project Manager ---- */
    Screen screen = SCREEN_PROJECT_MANAGER;
    Project project;
    project_defaults(&project);

    ProjectManager pm;
    project_manager_init(&pm);

    /* ---- Editor state ---- */
    SpriteAtlas        atlas;   memset(&atlas,   0, sizeof atlas);
    AssetLibrary       assets;  asset_library_init(&assets);
    World              world;   memset(&world,   0, sizeof world);
    Registry           registry;
    SpatialGrid        sgrid;   memset(&sgrid,   0, sizeof sgrid);
    Editor             editor;
    Panel              panel;
    ShapePane          shape_pane; shape_pane_init(&shape_pane);
    EditorMode         prev_editor_mode = EDITOR_MODE_PAINT;
    SimClock           sim_clock;
    ResourceStore      resources;
    WeatherSystem      weather;
    Camera             camera;
    ObjectDefRegistry  obj_registry;
    objdef_registry_init(&obj_registry);
    LuaHost *lua = lua_host_create();

    /* Phase J — tab workspace */
    TabBar      tabbar;
    tabbar_init(&tabbar);

    /* Phase K — sprites tab */
    SpritesTab  sprites_tab;
    memset(&sprites_tab, 0, sizeof sprites_tab);

    /* Phase L — objects tab */
    ObjectsTab  objects_tab;
    memset(&objects_tab, 0, sizeof objects_tab);

    /* Phase O — settings tab + editor-wide settings */
    EditorSettings editor_settings;
    editor_settings_init(&editor_settings);
    if (editor_settings_load(&editor_settings)) {
        LOG_INFO("Editor settings loaded from disk");
        /* Re-apply whichever theme file was active last session — the
           startup theme_load("themes/default.theme") above ran before
           we knew this, so this can override it with the saved choice. */
        if (editor_settings.theme_path[0])
            theme_load(editor_settings.theme_path);
    }
    SettingsTab settings_tab;
    memset(&settings_tab, 0, sizeof settings_tab);

    bool editor_ready = false;

    /* ---- Edit/Play split ---- */
    typedef enum { MODE_EDIT = 0, MODE_PLAY } GameMode;
    GameMode mode = MODE_EDIT;
    PlaySnapshot play_snap;
    memset(&play_snap, 0, sizeof play_snap);

    #define ENTER_EDITOR() do {                                                \
        if (chdir(project.path) != 0)                                         \
            LOG_WARN("chdir('%s') failed", project.path);                      \
        if (mode == MODE_PLAY) playmode_snapshot_free(&play_snap);            \
        mode = MODE_EDIT;                                                      \
        if (editor_ready) atlas_destroy(&atlas);                               \
        atlas_load(&atlas, "assets/sprites.png", 32, 32, 4);                  \
        asset_library_destroy(&assets);                                       \
        asset_library_init(&assets);                                          \
        asset_library_load_meta(&assets);                                     \
        if (editor_ready) world_destroy(&world);                               \
        if (editor_ready) sgrid_destroy(&sgrid);                               \
        if (!world_create(&world, project.grid_w, project.grid_h)) {          \
            LOG_ERROR("world_create failed."); break;                          \
        }                                                                      \
        world_clear(&world);                                                   \
        if (!world_load(&world, WORLD_SAVE_PATH)) {                           \
            world_topology_generate(&world, project.topology,                 \
                                     (unsigned int)SDL_GetTicks());           \
        }                                                                      \
        registry_init(&registry);                                              \
        registry_load(&registry, ENTITY_SAVE_PATH);                           \
        sgrid_create(&sgrid, world.width, world.height);                       \
        { /* rebuild sgrid from registry */                                    \
          for (int _e = 0; _e < MAX_ENTITIES; _e++) {                         \
            if (!registry.alive[_e] || !registry.has_transform[_e]) continue; \
            int _gx = (int)(registry.transform[_e].x + 0.5f);                 \
            int _gy = (int)(registry.transform[_e].y + 0.5f);                 \
            sgrid_insert(&sgrid, (Entity)_e, _gx, _gy);                       \
          }                                                                    \
        }                           \
        editor_init(&editor);                                                  \
        shape_pane_init(&shape_pane);                                          \
        panel_init(&panel, project.grid_w, project.grid_h);                   \
        simclock_init(&sim_clock);                                             \
        resource_store_init(&resources);                                       \
        simulation_load(&sim_clock, &resources, SIM_SAVE_PATH);               \
        weather_init(&weather);                                                \
        weather_load(&weather, WEATHER_SAVE_PATH);                             \
        camera_init(&camera, WINDOW_W, WINDOW_H);                             \
        { float tw, th; renderer_get_tile_size(&tw, &th);                     \
          camera_center_on_world(&camera, world.width, world.height, tw, th);}\
        /* Phase K/L/O init */                                                 \
        sprites_tab_init(&sprites_tab, &atlas, &assets);                       \
        objects_tab_init(&objects_tab, &obj_registry, &sprites_tab);          \
        objdef_registry_load_all(&obj_registry);                               \
        lua_host_set_context(lua, &registry, &obj_registry, &resources);      \
        lua_host_set_genre(lua, project.genre);                               \
        settings_tab_init(&settings_tab, &project, &editor_settings);           \
        tabbar_init(&tabbar);                                                  \
        editor_ready = true;                                                   \
        LOG_INFO("Editor ready: '%s' (%dx%d)", project.name,                  \
                 project.grid_w, project.grid_h);                             \
    } while(0)

    #define SGRID_REBUILD() do {                                               \
        sgrid_destroy(&sgrid);                                                 \
        sgrid_create(&sgrid, world.width, world.height);                       \
        for (int _e = 0; _e < MAX_ENTITIES; _e++) {                           \
            if (!registry.alive[_e] || !registry.has_transform[_e]) continue; \
            int _gx = (int)(registry.transform[_e].x + 0.5f);                 \
            int _gy = (int)(registry.transform[_e].y + 0.5f);                 \
            sgrid_insert(&sgrid, (Entity)_e, _gx, _gy);                       \
        }                                                                      \
    } while(0)

    while (!input_quit_requested()) {
        input_begin_frame();
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
            input_process_event(&ev);
        dge_time_tick();

        int vw, vh;
        window_get_size(&window, &vw, &vh);

        /* ================================================================
           PROJECT MANAGER SCREEN
           ================================================================ */
        if (screen == SCREEN_PROJECT_MANAGER) {
            ProjectManagerResult pmr =
                project_manager_update(&pm, vw, vh, &project);
            if (pmr == PM_RESULT_QUIT) break;
            if (pmr == PM_RESULT_OPEN) {
                project_recent_add(project.path);
                ENTER_EDITOR();
                screen = SCREEN_EDITOR;
            }
            renderer_clear(0.08f, 0.08f, 0.10f);
            renderer_begin_ui(vw, vh);
            project_manager_render(&pm, vw, vh);
            renderer_end();
            window_swap(&window);
            continue;
        }

        /* ================================================================
           EDITOR SCREEN
           ================================================================ */
        if (input_key_pressed(SDL_SCANCODE_ESCAPE)) break;

        /* Tab selection */
        bool toggle_play = false;
        ActiveTab cur_tab = tabbar_update(&tabbar, vw, &toggle_play);

        if (toggle_play) {
            if (mode == MODE_EDIT) {
                if (playmode_snapshot(&play_snap, &world, &registry, &resources,
                                      &sim_clock, &weather)) {
                    mode = MODE_PLAY;
                    editor.selected = ENTITY_HANDLE_NULL;
                    LOG_INFO("-- PLAY --");
                } else {
                    LOG_ERROR("Could not enter Play mode (snapshot failed) — staying in Edit");
                }
            } else {
                playmode_restore(&play_snap, &world, &registry, &resources, &sim_clock, &weather);
                playmode_snapshot_free(&play_snap);
                mode = MODE_EDIT;
                LOG_INFO("-- STOP --");
            }
        }

        /* ---- Content area is below the tab bar ---- */
        int content_vh = vh - TABBAR_H;  /* logical height for sub-systems */
        (void)content_vh;

        /* ---- Save / Load (Edit mode only) ----
           Gated on mode, not just cur_tab: F5 while playing would write
           the live (Play-mutated) World/Registry over the authored
           save file -- exactly the leak playmode_snapshot/restore
           exists to prevent. F9 similarly shouldn't load over whatever
           Play is running; Stop already restores the pre-Play state
           on its own. */
        if (mode == MODE_EDIT && !input_keyboard_consumed()) {
            if (input_key_pressed(SDL_SCANCODE_F5)) {
                world_save(&world, WORLD_SAVE_PATH);
                registry_save(&registry, ENTITY_SAVE_PATH);
                simulation_save(&sim_clock, &resources, SIM_SAVE_PATH);
                weather_save(&weather, WEATHER_SAVE_PATH);
                editor_settings_save(&editor_settings);
                LOG_INFO("Project saved.");
            }
            if (input_key_pressed(SDL_SCANCODE_F9)) {
                world_load(&world, WORLD_SAVE_PATH);
                registry_load(&registry, ENTITY_SAVE_PATH);
                simulation_load(&sim_clock, &resources, SIM_SAVE_PATH);
                weather_load(&weather, WEATHER_SAVE_PATH);
                editor.selected = ENTITY_HANDLE_NULL;
                SGRID_REBUILD();
                LOG_INFO("Project loaded.");
            }
            if (input_key_pressed(SDL_SCANCODE_R) && cur_tab == TAB_WORLD) {
                world_clear(&world);
                registry_init(&registry);
                editor.selected = ENTITY_HANDLE_NULL;
                SGRID_REBUILD();
                LOG_INFO("Canvas cleared.");
            }
        }

        /* ---- Simulation tick ----
           Only GENRE_SANDBOX_SIM runs SimClock/weather/agent-AI on its
           own — that's the genre that has resources, weather and the
           HARVEST/BUILD task vocabulary to drive in the first place.

           TACTICS and FREEFORM skip this block entirely rather than
           ticking a clock and weather system nothing in those genres
           reads. A tactics game advances on turn-end events instead of
           continuous gdt, and a freeform project's own Lua scripts are
           expected to drive whatever timing model they want, including
           calling simclock_tick() themselves if they happen to want
           one — the engine just isn't assuming it on their behalf. */
        float dt  = dge_time_delta();
        float gdt = 0.0f;
        if (project.genre == GENRE_SANDBOX_SIM) {
            gdt = simclock_tick(&sim_clock, dt);
            weather_update(&weather, gdt, dt, &camera);
            float spd = weather_speed_multiplier(weather.type);
            system_update_agents(&registry, &world, &sgrid, &resources, gdt, spd,
                                  lua ? lua_event_cb : NULL, lua);
        }

        /* Fire on_tick for every alive entity that has an ObjectDef
           with that behavior. For GENRE_SANDBOX_SIM this only fires
           when the sim clock actually advanced (gdt > 0) — ticking
           scripts on a frozen clock would be wasted work in a genre
           that already has its own pacing. For TACTICS/FREEFORM there
           is no sim clock driving anything, so on_tick fires every
           real frame instead — it's the only per-frame hook those
           genres get, and scripts there are expected to do their own
           "did meaningful time pass" gating if they need it (e.g. a
           tactics game's scripts checking "is it this unit's turn"
           rather than relying on a clock the engine isn't running). */
        bool should_tick_scripts = (project.genre == GENRE_SANDBOX_SIM)
                                    ? (gdt > 0.0f) : true;
        if (should_tick_scripts && lua) {
            for (int _e = 0; _e < MAX_ENTITIES; _e++) {
                if (!registry.alive[_e] || !registry.has_definition[_e]) continue;
                lua_host_call_behavior(lua, (Entity)_e, "on_tick");
            }
        }

        /* Fire on_click in Play mode: LMB on a tile that holds an
           entity triggers that entity's on_click behavior.  In Edit
           mode clicking is for SELECT, not script dispatch. */
        if (mode == MODE_PLAY && cur_tab == TAB_WORLD && lua) {
            if (input_mouse_button_pressed(SDL_BUTTON_LEFT)) {
                int _mx, _my; input_mouse_pos(&_mx, &_my);
                /* Only fire if the click is in the world viewport (not
                   over the tab bar at the top). */
                if (_my > TABBAR_H) {
                    Vec2 _w = camera_screen_to_world(&camera, (float)_mx, (float)_my);
                    float _fgx, _fgy;
                    renderer_world_to_grid(_w.x, _w.y, &_fgx, &_fgy);
                    int _gx = (int)(_fgx + 0.5f), _gy = (int)(_fgy + 0.5f);
                    if (_gx >= 0 && _gy >= 0 && _gx < world.width && _gy < world.height) {
                        Entity _hit = sgrid_at(&sgrid, _gx, _gy);
                        if (_hit != ENTITY_NULL)
                            lua_host_call_behavior(lua, _hit, "on_click");
                    }
                }
            }
        }

        /* Pause toggle -- valid in both modes; pausing a running
           playtest is a reasonable thing to want, not just an Edit
           sandbox control. */
        if (!input_keyboard_consumed()) {
            if (input_key_pressed(SDL_SCANCODE_P) && cur_tab == TAB_WORLD) {
                if (simclock_is_paused(&sim_clock)) simclock_resume(&sim_clock);
                else simclock_pause(&sim_clock);
            }
            /* Ctrl+R — reload all Lua scripts from disk.  Useful when
               iterating on a script without restarting the engine. */
            if (lua && input_key_pressed(SDL_SCANCODE_R)
                && (input_key_down(SDL_SCANCODE_LCTRL) || input_key_down(SDL_SCANCODE_RCTRL))) {
                lua_host_clear_cache(lua);
                LOG_INFO("Lua script cache cleared (Ctrl+R)");
            }
            if (input_key_pressed(SDL_SCANCODE_H) && cur_tab == TAB_WORLD && mode == MODE_EDIT) {
                if (entity_handle_valid(&registry, editor.selected)) {
                    if (system_harvest_entity(&registry, editor.selected, &resources))
                        editor.selected = ENTITY_HANDLE_NULL;
                }
            }
        }

        /* ---- Camera (World tab) ---- */
        if (cur_tab == TAB_WORLD) {
            float pan = 200.0f * dt;
            if (!input_keyboard_consumed()) {
                if (input_key_down(SDL_SCANCODE_W)||input_key_down(SDL_SCANCODE_UP))
                    camera_pan(&camera,  0.0f, -pan);
                if (input_key_down(SDL_SCANCODE_S)||input_key_down(SDL_SCANCODE_DOWN))
                    camera_pan(&camera,  0.0f,  pan);
                if (input_key_down(SDL_SCANCODE_A)||input_key_down(SDL_SCANCODE_LEFT))
                    camera_pan(&camera, -pan,   0.0f);
                if (input_key_down(SDL_SCANCODE_D)||input_key_down(SDL_SCANCODE_RIGHT))
                    camera_pan(&camera,  pan,   0.0f);
            }

            if (input_mouse_button_down(SDL_BUTTON_MIDDLE)) {
                int mdx, mdy; input_mouse_delta(&mdx, &mdy);
                if (mdx || mdy) camera_pan(&camera, -(float)mdx, -(float)mdy);
            }
            int sx, sy; input_mouse_scroll(&sx, &sy);
            if (sy) {
                int mx, my; input_mouse_pos(&mx, &my);
                camera_zoom_at(&camera, sy>0?1.1f:0.9f, (float)mx, (float)my);
            }
        }

        camera.viewport_w = vw;
        camera.viewport_h = vh;

        /* ---- Panel (World tab, Edit mode only) ----
           In Play there's nothing to paint/place/select -- the whole
           point of Play is "watch/test the game as it actually runs",
           not "keep editing it". */
        if (cur_tab == TAB_WORLD && mode == MODE_EDIT) {
            PanelAction pa;
            panel_update(&panel, &editor, &resources, &weather,
                        &obj_registry, &sprites_tab, &atlas, &world, project.genre, vw, vh, &pa);
            switch (pa.type) {
                case PANEL_ACTION_NEW:
                    world_clear(&world); registry_init(&registry);
                    editor.selected = ENTITY_HANDLE_NULL;
                    SGRID_REBUILD(); break;
                case PANEL_ACTION_REGENERATE:
                    world_generate(&world, (unsigned int)SDL_GetTicks());
                    registry_init(&registry);
                    editor.selected = ENTITY_HANDLE_NULL;
                    SGRID_REBUILD(); break;
                case PANEL_ACTION_SAVE:
                    world_save(&world, WORLD_SAVE_PATH);
                    registry_save(&registry, ENTITY_SAVE_PATH);
                    simulation_save(&sim_clock, &resources, SIM_SAVE_PATH);
                    weather_save(&weather, WEATHER_SAVE_PATH); break;
                case PANEL_ACTION_LOAD:
                    world_load(&world, WORLD_SAVE_PATH);
                    registry_load(&registry, ENTITY_SAVE_PATH);
                    simulation_load(&sim_clock, &resources, SIM_SAVE_PATH);
                    weather_load(&weather, WEATHER_SAVE_PATH);
                    editor.selected = ENTITY_HANDLE_NULL;
                    SGRID_REBUILD(); break;
                case PANEL_ACTION_RESIZE:
                    if (world_resize(&world, panel.pending_w, panel.pending_h)) {
                        float tw, th; renderer_get_tile_size(&tw, &th);
                        camera_center_on_world(&camera, world.width, world.height, tw, th);
                        SGRID_REBUILD();
                    } break;
                case PANEL_ACTION_WEATHER_TOGGLE:
                    weather_set_enabled(&weather, !weather.enabled); break;
                case PANEL_ACTION_WEATHER_SET:
                    weather_set_type(&weather, (WeatherType)pa.weather_type); break;
                default: break;
            }

            /* ---- Shape Pane (flat-grid mask painter, docked right) ----
               Live-linked to world.shape: shape_pane_update() writes
               directly into the same struct world_render() reads, so
               there's no apply step -- painting here shows up in the
               isometric view the very next frame. Only active while in
               EDITOR_MODE_SHAPE; shape_pane_fit_to_world() re-frames the
               flat grid once on entry so it doesn't open scrolled to
               wherever a previous session left it relative to a since-
               resized world. */
            if (editor.mode == EDITOR_MODE_SHAPE) {
                if (prev_editor_mode != EDITOR_MODE_SHAPE)
                    shape_pane_fit_to_world(&shape_pane, &world, vh);
                shape_pane_update(&shape_pane, &world, vw, vh);
            }
            prev_editor_mode = editor.mode;

            /* editor_update() always runs (TAB mode-switching and other
               non-mouse handling must work regardless of where the
               cursor is) but is told to exclude the Shape Pane's screen
               region from tile hit-testing while it's open -- see
               editor.h's ui_right_margin doc comment. Without this, a
               click inside the flat pane could ALSO land on whatever
               isometric tile happens to render underneath that same
               screen region (the iso camera can pan/zoom such that
               world tiles visually extend under a right-docked panel),
               double-painting one click in two different grid positions.
               Outside SHAPE mode the pane isn't shown, so 0 disables
               the exclusion entirely -- same convention ui_panel_width
               already uses for "no panel here". */
            int shape_pane_right_margin =
                (editor.mode == EDITOR_MODE_SHAPE) ? (vw - SHAPE_PANE_W) : 0;
            editor_update(&editor, &registry, &world, &camera, &resources,
                          &sgrid, panel_effective_width(&panel), TABBAR_H + STATUS_BAR_H,
                          shape_pane_right_margin);
        }

        /* ---- Tab-specific updates (non-World) ---- */
        if (cur_tab == TAB_SPRITES)
            sprites_tab_update(&sprites_tab, vw, vh);
        if (cur_tab == TAB_OBJECTS)
            objects_tab_update(&objects_tab, vw, vh);
        if (cur_tab == TAB_SETTINGS) {
            /* Mirror the live World's shape state into the checkbox
               before drawing/handling it, so toggling SHAPE mode from
               the World panel (editor.c) and toggling it here always
               agree -- whichever one last changed it wins, and the
               checkbox never lies about what's actually active. */
            editor_settings.world_shape_active = world.shape.active;
            bool shape_was_active = world.shape.active;

            settings_tab_update(&settings_tab, &project, vw, vh);

            if (editor_settings.world_shape_active && !shape_was_active) {
                world_shape_activate(&world.shape, world.width, world.height);
                LOG_INFO("World shape activated (settings) -- all tiles enabled, cut holes from SHAPE mode");
            } else if (!editor_settings.world_shape_active && shape_was_active) {
                world_shape_destroy(&world.shape);
                LOG_INFO("World shape deactivated -- map is a full rectangle again");
            }
            /* Handle resize requests from settings tab */
            if (settings_tab.wants_resize) {
                if (world_resize(&world, settings_tab.pending_grid_w,
                                          settings_tab.pending_grid_h)) {
                    float tw, th; renderer_get_tile_size(&tw, &th);
                    camera_center_on_world(&camera, world.width, world.height, tw, th);
                    panel_init(&panel, settings_tab.pending_grid_w, settings_tab.pending_grid_h);
                    SGRID_REBUILD();
                }
                settings_tab.wants_resize = false;
            }
            if (settings_tab.wants_tile_resize) {
                renderer_set_tile_size((float)settings_tab.pending_tile_w,
                                       (float)settings_tab.pending_tile_h);
                settings_tab.wants_tile_resize = false;
            }
            if (settings_tab.wants_close_project) {
                settings_tab.wants_close_project = false;
                if (mode == MODE_PLAY) {
                    playmode_restore(&play_snap, &world, &registry, &resources, &sim_clock, &weather);
                    playmode_snapshot_free(&play_snap);
                    mode = MODE_EDIT;
                }
                /* Same persistence F5 does, so nothing placed/painted
                   this session is lost just because the project is
                   closing rather than the whole app quitting. */
                world_save(&world, WORLD_SAVE_PATH);
                registry_save(&registry, ENTITY_SAVE_PATH);
                simulation_save(&sim_clock, &resources, SIM_SAVE_PATH);
                weather_save(&weather, WEATHER_SAVE_PATH);
                editor_settings_save(&editor_settings);
                LOG_INFO("Project '%s' closed -> back to Project Manager", project.name);
                project_manager_init(&pm);
                screen = SCREEN_PROJECT_MANAGER;
            }
        }

        /* ================================================================
           RENDER
           ================================================================ */
        renderer_clear(0.08f, 0.08f, 0.10f);

        if (cur_tab == TAB_WORLD) {
            renderer_begin(&camera);
            world_render(&world, &atlas);
            if (mode == MODE_EDIT) editor_render(&editor, &registry);
            system_animate_entities(&registry, dge_time_delta());
            system_render_entities(&registry, &atlas, &assets);
            weather_render(&weather, &camera);
            construction_render(&registry, &camera);
        }

        renderer_begin_ui(vw, vh);

        /* Tab bar always on top */
        tabbar_render(&tabbar, vw, mode == MODE_PLAY);

        if (cur_tab == TAB_WORLD && mode == MODE_EDIT) {
            ui_render(&resources, &sim_clock, &weather, &editor, &registry, &world,
                      project.genre, vw, vh, world.width, world.height,
                      panel_effective_width(&panel) + 10);
            panel_render(&panel, &editor, &resources, &weather, &obj_registry,
                         &atlas, &sprites_tab, &world, project.genre, vw, vh);
            minimap_render(&world, &registry, &camera, vw, vh);
            if (editor.mode == EDITOR_MODE_SHAPE)
                shape_pane_render(&shape_pane, &world, vw, vh);
        } else if (cur_tab == TAB_WORLD && mode == MODE_PLAY) {
            /* No sidebar to offset against -- the status row gets the
               full width back, same reasoning as panel_effective_width()
               returning 0 when the sidebar is hidden in Edit mode. */
            ui_render(&resources, &sim_clock, &weather, &editor, &registry, &world,
                      project.genre, vw, vh, world.width, world.height, 10);
            minimap_render(&world, &registry, &camera, vw, vh);

            const char *hint = "PLAYING -- click STOP (top right) to return to editing";
            text_draw(12.0f, (float)TABBAR_H + 8.0f, 1.3f, 0.6f, 0.85f, 0.65f, 1.0f, hint);
        } else if (cur_tab == TAB_SPRITES) {
            sprites_tab_render(&sprites_tab, vw, vh);
        } else if (cur_tab == TAB_OBJECTS) {
            objects_tab_render(&objects_tab, vw, vh);
        } else if (cur_tab == TAB_SETTINGS) {
            settings_tab_render(&settings_tab, &project, vw, vh);
        }

        renderer_end();
        window_swap(&window);
    }

    /* Shutdown */
    editor_settings_save(&editor_settings);
    lua_host_destroy(lua);
    if (mode == MODE_PLAY) playmode_snapshot_free(&play_snap);
    if (editor_ready) {
        world_destroy(&world);
        sgrid_destroy(&sgrid);
        atlas_destroy(&atlas);
        asset_library_destroy(&assets);
    }
    renderer_shutdown();
    window_destroy(&window);
    LOG_INFO("DGEngine shut down cleanly.");
    return 0;
}
