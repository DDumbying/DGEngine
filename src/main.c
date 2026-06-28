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
#include "ui/minimap.h"
#include "ui/project_manager.h"
#include "ui/tabbar.h"
#include "ui/sprites_tab.h"
#include "ui/objects_tab.h"
#include "ui/settings_tab.h"

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
    SimClock           sim_clock;
    ResourceStore      resources;
    WeatherSystem      weather;
    Camera             camera;
    ObjectDefRegistry  obj_registry;
    objdef_registry_init(&obj_registry);

    /* Phase J — tab workspace */
    TabBar      tabbar;
    tabbar_init(&tabbar);

    /* Phase K — sprites tab */
    SpritesTab  sprites_tab;
    memset(&sprites_tab, 0, sizeof sprites_tab);

    /* Phase L — objects tab */
    ObjectsTab  objects_tab;
    memset(&objects_tab, 0, sizeof objects_tab);

    /* Phase O — settings tab */
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
        world_load(&world, WORLD_SAVE_PATH);                                   \
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
        settings_tab_init(&settings_tab, &project);                            \
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

        /* ---- Simulation tick (always, regardless of tab or mode --
           a playtest needs the same simulation running as Edit's
           sandbox preview does) ---- */
        float dt  = dge_time_delta();
        float gdt = simclock_tick(&sim_clock, dt);
        weather_update(&weather, gdt, dt, &camera);
        float spd = weather_speed_multiplier(weather.type);
        system_update_agents(&registry, &world, &sgrid, &resources, gdt, spd);

        /* Pause toggle -- valid in both modes; pausing a running
           playtest is a reasonable thing to want, not just an Edit
           sandbox control. */
        if (!input_keyboard_consumed()) {
            if (input_key_pressed(SDL_SCANCODE_P) && cur_tab == TAB_WORLD) {
                if (simclock_is_paused(&sim_clock)) simclock_resume(&sim_clock);
                else simclock_pause(&sim_clock);
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
                        &obj_registry, &sprites_tab, vw, vh, &pa);
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
            editor_update(&editor, &registry, &world, &camera, &resources,
                          &sgrid, panel_effective_width(&panel), TABBAR_H + STATUS_BAR_H);
        }

        /* ---- Tab-specific updates (non-World) ---- */
        if (cur_tab == TAB_SPRITES)
            sprites_tab_update(&sprites_tab, vw, vh);
        if (cur_tab == TAB_OBJECTS)
            objects_tab_update(&objects_tab, vw, vh);
        if (cur_tab == TAB_SETTINGS) {
            settings_tab_update(&settings_tab, &project, vw, vh);
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
            world_render(&world);
            if (mode == MODE_EDIT) editor_render(&editor, &registry);
            system_render_entities(&registry, &atlas, &assets);
            weather_render(&weather, &camera);
            construction_render(&registry, &camera);
        }

        renderer_begin_ui(vw, vh);

        /* Tab bar always on top */
        tabbar_render(&tabbar, vw, mode == MODE_PLAY);

        if (cur_tab == TAB_WORLD && mode == MODE_EDIT) {
            ui_render(&resources, &sim_clock, &weather, &editor, &registry,
                      vw, vh, world.width, world.height,
                      panel_effective_width(&panel) + 10);
            panel_render(&panel, &editor, &resources, &weather, &obj_registry, vw, vh);
            minimap_render(&world, &registry, &camera, vw, vh);
        } else if (cur_tab == TAB_WORLD && mode == MODE_PLAY) {
            /* No sidebar to offset against -- the status row gets the
               full width back, same reasoning as panel_effective_width()
               returning 0 when the sidebar is hidden in Edit mode. */
            ui_render(&resources, &sim_clock, &weather, &editor, &registry,
                      vw, vh, world.width, world.height, 10);
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
