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
#include "core/level.h"
#include "core/object_def.h"
#include "core/playmode.h"
#include "core/rules.h"
#include "editor/play_mode.h"
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
#include "ui/menubar.h"
#include "ui/text.h"
#include "ui/font_atlas.h"
#include "ui/panel.h"
#include "ui/shape_pane.h"
#include "ui/minimap.h"
#include "ui/project_manager.h"

#include "ui/sprites_tab.h"
#include "ui/objects_tab.h"
#include "ui/settings_tab.h"
#include "ui/theme.h"
#include "ui/left_pane.h"
#include "ui/inspector_pane.h"
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
       
    char engine_root[512];
    if (getcwd(engine_root, sizeof(engine_root))) {
        theme_set_engine_root(engine_root);
    } else {
        engine_root[0] = '.';
        engine_root[1] = '\0';
    }
    
    theme_reset_default();
    char def_theme[1024];
    snprintf(def_theme, sizeof(def_theme), "%s/themes/default.theme", engine_root);
    theme_load(def_theme);

    dge_time_tick();

    /* ---- Project Manager ---- */
    Screen screen = SCREEN_PROJECT_MANAGER;
    Project project;
    project_defaults(&project);

    ProjectManager pm;
    project_manager_init(&pm);

    /* ---- Editor state ---- */
    static SpriteAtlas        atlas;
    static AssetLibrary       assets;  asset_library_init(&assets);
    static World              world;
    static Registry           registry;
    static LevelRegistry      levels; level_registry_init(&levels);
    /* Resolved from levels' active Level each time it changes (see
       REFRESH_LEVEL_PATHS() below) -- every call site that used to
       read the fixed WORLD_SAVE_PATH/ENTITY_SAVE_PATH constants now
       reads these instead. sim.dge/weather.dge stay project-wide
       fixed paths (SIM_SAVE_PATH/WEATHER_SAVE_PATH), unaffected by
       which Level is active -- see core/level.h's own doc comment on
       why SimClock/ResourceStore/WeatherSystem are project-wide, not
       per-level. */
    char cur_world_path[300]  = WORLD_SAVE_PATH;
    char cur_entity_path[300] = ENTITY_SAVE_PATH;
    static SpatialGrid        sgrid;
    static Editor             editor;
    static LeftPane           left_pane; left_pane_init(&left_pane);
    static InspectorPane      inspector_pane; inspector_pane_init(&inspector_pane);
    EditorMode         prev_editor_mode = EDITOR_MODE_PAINT;
    static SimClock           sim_clock;
    static ResourceStore      resources;
    static WeatherSystem      weather;
    static Camera             camera;
    static ObjectDefRegistry  obj_registry;
    objdef_registry_init(&obj_registry);
    LuaHost *lua = lua_host_create();

    /* Phase J — tab workspace */
    MenuBar     menubar;
    menubar_init(&menubar);

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
    PlayMode play_mode;
    play_mode_init(&play_mode);
    GameRules rules;
    rules_clear(&rules);
    PlaySnapshot play_snap;
    memset(&play_snap, 0, sizeof play_snap);

    #define REFRESH_LEVEL_PATHS() do {                                        \
        const Level *_lv = level_registry_active(&levels);                    \
        if (_lv) {                                                            \
            snprintf(cur_world_path,  sizeof(cur_world_path),  "%s", _lv->world_path);  \
            snprintf(cur_entity_path, sizeof(cur_entity_path), "%s", _lv->entity_path); \
        } else {                                                              \
            /* Should not normally happen -- level_registry_bootstrap() is    \
               always called before this in ENTER_EDITOR -- but fall back to  \
               the pre-Level flat filenames rather than an empty path if it   \
               somehow does (e.g. LEVEL_MAX==0, which never happens today,    \
               but this keeps the fallback honest instead of silently         \
               writing to ""). */                                             \
            snprintf(cur_world_path,  sizeof(cur_world_path),  "%s", WORLD_SAVE_PATH);  \
            snprintf(cur_entity_path, sizeof(cur_entity_path), "%s", ENTITY_SAVE_PATH); \
        }                                                                      \
    } while(0)

    /* Loads whatever cur_world_path/cur_entity_path currently point at
       (see REFRESH_LEVEL_PATHS() above) into the live World/Registry/
       SpatialGrid, re-fitting the panel's resize fields and the camera
       to the newly active Level's own dimensions. Used both by
       ENTER_EDITOR (first entry into a project) and by the
       PANEL_ACTION_LEVEL_PREV/NEXT/ADD handlers (switching to a
       different Level within an already-open project) — extracted
       once so both paths can't quietly drift apart on what "loading a
       level" actually means.

       On a world_create() failure (allocation failure — in practice
       this never happens; every other OOM path in this codebase treats
       it the same way) this only aborts the REST OF THIS MACRO's own
       work via its inner break, not whatever ENTER_EDITOR still has
       left to do afterward (editor_init, sprites_tab_init, etc.) --
       unlike the original inline code this replaced, which aborted
       all of ENTER_EDITOR on the same failure. Accepted as a minor,
       documented behavior change rather than threading a success/
       failure flag through a macro purely to preserve exact behavior
       for a failure mode this codebase doesn't otherwise engineer
       around. */                                                            \
    #define LOAD_ACTIVE_LEVEL() do {                                          \
        if (editor_ready) world_destroy(&world);                              \
        if (editor_ready) sgrid_destroy(&sgrid);                              \
        if (!world_create(&world, project.grid_w, project.grid_h)) {         \
            LOG_ERROR("world_create failed."); break;                         \
        }                                                                     \
        world_clear(&world);                                                  \
        if (!world_load(&world, cur_world_path)) {                           \
            world_topology_generate(&world, project.topology,                \
                                     (unsigned int)SDL_GetTicks());          \
        }                                                                     \
        registry_init(&registry);                                             \
        registry_load(&registry, cur_entity_path);                           \
        sgrid_create(&sgrid, world.width, world.height);                      \
        { /* rebuild sgrid from registry */                                   \
          for (int _e = 0; _e < MAX_ENTITIES; _e++) {                        \
            if (!registry.alive[_e] || !registry.has_transform[_e]) continue;\
            int _gx = (int)(registry.transform[_e].x + 0.5f);                \
            int _gy = (int)(registry.transform[_e].y + 0.5f);                \
            sgrid_insert(&sgrid, (Entity)_e, _gx, _gy);                      \
          }                                                                   \
        }                                                                     \
        { float _tw, _th; renderer_get_tile_size(&_tw, &_th);                \
          camera_center_on_world(&camera, world.width, world.height, _tw, _th); } \
    } while(0)

    #define ENTER_EDITOR() do {                                                \
        if (chdir(project.path) != 0)                                         \
            LOG_WARN("chdir('%s') failed", project.path);                      \
        /* Phase 3, Part A: Level/Scene system (see core/level.h,              \
           ENGINE_DESIGN.md §6). A project that already has a                  \
           levels/manifest.def just loads it. A project that predates          \
           Levels (or is genuinely brand new) gets bootstrapped with a         \
           single default "Level 1" -- and if this project has old-style      \
           flat world.dge/entities.dge files sitting in its root (a           \
           pre-Phase-3 save), those get migrated into Level 1's own paths     \
           losslessly rather than silently ignored/orphaned. Either way,      \
           the manifest is written back so this bootstrap/migration only      \
           ever runs once per project. */                                     \
        if (!level_registry_load(&levels, "levels/manifest.def")) {           \
            level_registry_bootstrap(&levels);                                \
            level_registry_migrate_legacy(WORLD_SAVE_PATH, ENTITY_SAVE_PATH,   \
                                          level_registry_active(&levels));     \
            level_registry_save(&levels, "levels/manifest.def");              \
        } else {                                                              \
            level_registry_bootstrap(&levels); /* defensive no-op if count>0 */\
        }                                                                      \
        REFRESH_LEVEL_PATHS();                                                \
        if (play_mode_is_playing(&play_mode)) playmode_snapshot_free(&play_snap);            \
        play_mode_stop(&play_mode);                                                      \
        if (editor_ready) atlas_destroy(&atlas);                               \
        atlas_load(&atlas, "assets/sprites.png", 32, 32, 4);                  \
        asset_library_destroy(&assets);                                       \
        asset_library_init(&assets);                                          \
        asset_library_load_meta(&assets);                                     \
        LOAD_ACTIVE_LEVEL();                                                   \
        editor_init(&editor);                                                  \
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
        menubar_init(&menubar);                                                  \
        rules_load(&rules);                                                      \
        editor_ready = true;                                                   \
        LOG_INFO("Editor ready: '%s' (%dx%d) -- level '%s'", project.name,     \
                 project.grid_w, project.grid_h,                              \
                 level_registry_active(&levels) ? level_registry_active(&levels)->name : "?"); \
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

    /* Phase 4 (Sidebar cleanup): the single owner for "a resize just
       happened, now make every piece of state that cares agree with
       the new dimensions." Before this existed, PANEL_ACTION_RESIZE
       (the sidebar's quick +/- buttons) and settings_tab.wants_resize
       (Settings' precise text-field entry) each called world_resize()
       directly and then did their OWN, DIFFERENT partial cleanup —
       Settings' path refreshed the panel's pending fields via
       panel_init() but neither path ever refreshed the OTHER UI's
       pending fields, and — the real bug — neither ever updated
       project.grid_w/h at all. That field would silently go stale the
       moment you resized through either UI, and stay wrong forever
       (reloading doesn't fix it either: world_load() only touches the
       World struct, never writes back into Project). It's not just
       cosmetic — PANEL_ACTION_LEVEL_ADD (Phase 3) sizes a brand new
       Level from project.grid_w/h, so a stale value there meant new
       levels could silently be created at the WRONG size after any
       resize.

       Both call sites now route through this one macro instead of
       duplicating (and inevitably diverging on) the cleanup — "one
       owner" in the sense the roadmap meant: not fewer UI entry
       points, but exactly one place that defines what "resized"
       actually means for the rest of the program's state. */
    #define COMPLETE_WORLD_RESIZE(new_w, new_h) do {                          \
        if (world_resize(&world, (new_w), (new_h))) {                        \
            project.grid_w = world.width;                                    \
            project.grid_h = world.height;                                   \
            /* Only the dimension fields, NOT a full panel_init() --         \
               that would also force p->visible=true and reset scroll/       \
               rename/sprite-assignment state, which has nothing to do       \
               with a resize and would be a real regression (e.g. hiding     \
               the sidebar, then resizing via Settings, would force it       \
               back open). */                                                \
            settings_tab.pending_grid_w = world.width;                       \
            settings_tab.pending_grid_h = world.height;                      \
            float _tw, _th; renderer_get_tile_size(&_tw, &_th);              \
            camera_center_on_world(&camera, world.width, world.height, _tw, _th); \
            SGRID_REBUILD();                                                  \
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
        if (input_key_pressed(SDL_SCANCODE_ESCAPE)) {
            menubar.active_dropdown = DROPDOWN_NONE;
            editor.mode = EDITOR_MODE_SELECT;
        }

        /* Tab selection */
        bool toggle_play = false;
        MenuAction menu_action = MENU_ACTION_NONE;
        ActiveTab cur_tab = menubar_update(&menubar, vw, vh, &toggle_play, &menu_action);
        
        if (menu_action == MENU_ACTION_QUIT) break;
        else if (menu_action == MENU_ACTION_SAVE) {
            world_save(&world, cur_world_path);
            registry_save(&registry, cur_entity_path);
            simulation_save(&sim_clock, &resources, SIM_SAVE_PATH);
            weather_save(&weather, WEATHER_SAVE_PATH);
            level_registry_save(&levels, "levels/manifest.def");
            editor_settings_save(&editor_settings);
            LOG_INFO("Project saved via Menu.");
        }
        else if (menu_action == MENU_ACTION_LOAD) {
            world_load(&world, cur_world_path);
            registry_load(&registry, cur_entity_path);
            simulation_load(&sim_clock, &resources, SIM_SAVE_PATH);
            weather_load(&weather, WEATHER_SAVE_PATH);
            editor.selected = ENTITY_HANDLE_NULL;
            SGRID_REBUILD();
            LOG_INFO("Project loaded via Menu.");
        }
        else if (menu_action == MENU_ACTION_LEVEL_NEXT || menu_action == MENU_ACTION_LEVEL_PREV) {
            world_save(&world, cur_world_path);
            registry_save(&registry, cur_entity_path);
            int n = levels.count;
            if (n > 1) {
                int next_idx = (menu_action == MENU_ACTION_LEVEL_NEXT)
                                ? (levels.active_index + 1) % n
                                : (levels.active_index - 1 + n) % n;
                level_registry_set_active(&levels, next_idx);
                REFRESH_LEVEL_PATHS();
                LOAD_ACTIVE_LEVEL();
                level_registry_save(&levels, "levels/manifest.def");
                editor.selected = ENTITY_HANDLE_NULL;
                LOG_INFO("Switched to level '%s' (%d/%d)", level_registry_active(&levels)->name, next_idx + 1, n);
            }
        }
        else if (menu_action == MENU_ACTION_LEVEL_ADD) {
            world_save(&world, cur_world_path);
            registry_save(&registry, cur_entity_path);
            char name[LEVEL_NAME_MAX];
            snprintf(name, sizeof(name), "Level %d", levels.count + 1);
            int new_idx = level_registry_add(&levels, name);
            if (new_idx >= 0) {
                level_registry_set_active(&levels, new_idx);
                REFRESH_LEVEL_PATHS();
                LOAD_ACTIVE_LEVEL();
                level_registry_save(&levels, "levels/manifest.def");
                editor.selected = ENTITY_HANDLE_NULL;
                LOG_INFO("Added and switched to level '%s' (%d/%d)", name, new_idx + 1, levels.count);
            }
        }

        if (toggle_play) {
            if (play_mode_is_editing(&play_mode)) {
                if (playmode_snapshot(&play_snap, &world, &registry, &resources,
                                      &sim_clock, &weather)) {
                    play_mode_enter_play(&play_mode);
                    editor.selected = ENTITY_HANDLE_NULL;
                    LOG_INFO("-- PLAY --");
                } else {
                    LOG_ERROR("Could not enter Play mode (snapshot failed) — staying in Edit");
                }
            } else if (play_mode_is_playing(&play_mode) || play_mode_ended(&play_mode)) {
                playmode_restore(&play_snap, &world, &registry, &resources, &sim_clock, &weather);
                playmode_snapshot_free(&play_snap);
                play_mode_stop(&play_mode);
                lua_host_clear_cache(lua);
                LOG_INFO("-- STOP --");
            }
        }
        
        play_mode_update(&play_mode, dge_time_delta());

        /* ---- Content area is below the tab bar ---- */
        int content_vh = vh - TOP_BAR_H;  /* logical height for sub-systems */
        (void)content_vh;

        /* ---- Save / Load (Edit mode only) ----
           Gated on mode, not just cur_tab: F5 while playing would write
           the live (Play-mutated) World/Registry over the authored
           save file -- exactly the leak playmode_snapshot/restore
           exists to prevent. F9 similarly shouldn't load over whatever
           Play is running; Stop already restores the pre-Play state
           on its own. */
        if (play_mode_is_editing(&play_mode) && !input_keyboard_consumed()) {
            if (input_key_pressed(SDL_SCANCODE_F5)) {
                world_save(&world, cur_world_path);
                registry_save(&registry, cur_entity_path);
                simulation_save(&sim_clock, &resources, SIM_SAVE_PATH);
                weather_save(&weather, WEATHER_SAVE_PATH);
                level_registry_save(&levels, "levels/manifest.def");
                editor_settings_save(&editor_settings);
                LOG_INFO("Project saved.");
            }
            if (input_key_pressed(SDL_SCANCODE_F9)) {
                world_load(&world, cur_world_path);
                registry_load(&registry, cur_entity_path);
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

        /* ---- Phase 5: Win/Lose condition evaluation ----
           Only fires during active Play (not paused, not already ended).
           Win script is checked first — if it returns true, the game
           is won regardless of the lose script. If win doesn't fire
           but lose does, the game is lost. A project with no rules.def
           or empty script paths simply never triggers either. */
        if (play_mode.state == PLAY_MODE_PLAY && lua && rules.loaded) {
            if (rules.win_script[0] &&
                lua_host_eval_condition(lua, rules.win_script)) {
                play_mode.state = PLAY_MODE_WON;
                snprintf(play_mode.end_message, sizeof play_mode.end_message,
                         "%s", rules.win_message[0] ? rules.win_message : "You Win!");
                LOG_INFO("GAME WON: %s", play_mode.end_message);
            } else if (rules.lose_script[0] &&
                       lua_host_eval_condition(lua, rules.lose_script)) {
                play_mode.state = PLAY_MODE_LOST;
                snprintf(play_mode.end_message, sizeof play_mode.end_message,
                         "%s", rules.lose_message[0] ? rules.lose_message : "Game Over");
                LOG_INFO("GAME LOST: %s", play_mode.end_message);
            }
        }

        /* Fire on_click in Play mode: LMB on a tile that holds an
           entity triggers that entity's on_click behavior.  In Edit
           mode clicking is for SELECT, not script dispatch. */
        if (play_mode_is_playing(&play_mode) && cur_tab == TAB_WORLD && lua) {
            if (input_mouse_button_pressed(SDL_BUTTON_LEFT)) {
                int _mx, _my; input_mouse_pos(&_mx, &_my);
                /* Only fire if the click is in the world viewport (not
                   over the tab bar at the top). */
                if (_my > TOP_BAR_H) {
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
            if (input_key_pressed(SDL_SCANCODE_H) && cur_tab == TAB_WORLD && play_mode_is_editing(&play_mode)) {
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
        if (cur_tab == TAB_WORLD && play_mode_is_editing(&play_mode)) {
            left_pane_update(&left_pane, &editor, &registry, vh);
            inspector_pane_update(&inspector_pane, &editor, &registry,
                                  left_pane.selected_entity, vw, vh);

            int current_left_w = left_pane.is_collapsed ? 24 : LEFT_PANE_WIDTH;
            int current_right_w = inspector_pane.is_collapsed ? 24 : INSPECTOR_PANE_WIDTH;
            editor_update(&editor, &registry, &world, &camera, &resources,
                          &sgrid, current_left_w, TOP_BAR_H,
                          vw - current_right_w);
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
                COMPLETE_WORLD_RESIZE(settings_tab.pending_grid_w, settings_tab.pending_grid_h);
                settings_tab.wants_resize = false;
            }
            if (settings_tab.wants_tile_resize) {
                renderer_set_tile_size((float)settings_tab.pending_tile_w,
                                       (float)settings_tab.pending_tile_h);
                settings_tab.wants_tile_resize = false;
            }
            if (settings_tab.wants_close_project) {
                settings_tab.wants_close_project = false;
                if (play_mode_is_playing(&play_mode)) {
                    playmode_restore(&play_snap, &world, &registry, &resources, &sim_clock, &weather);
                    playmode_snapshot_free(&play_snap);
                    play_mode_stop(&play_mode);
                }
                /* Same persistence F5 does, so nothing placed/painted
                   this session is lost just because the project is
                   closing rather than the whole app quitting. */
                world_save(&world, cur_world_path);
                registry_save(&registry, cur_entity_path);
                simulation_save(&sim_clock, &resources, SIM_SAVE_PATH);
                weather_save(&weather, WEATHER_SAVE_PATH);
                level_registry_save(&levels, "levels/manifest.def");
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
            if (play_mode_is_editing(&play_mode)) editor_render(&editor, &registry);
            system_animate_entities(&registry, dge_time_delta());
            system_render_entities(&registry, &atlas, &assets);
            weather_render(&weather, &camera);
            construction_render(&registry, &camera);
        }

        renderer_begin_ui(vw, vh);

        /* Menu bar always on top */
        menubar_render(&menubar, vw, play_mode_is_playing(&play_mode) || play_mode_ended(&play_mode));
        if (play_mode.overlay_timer > 0.0f) {
            menubar_render_play_overlay(play_mode.overlay_timer, play_mode.state == PLAY_MODE_PAUSED, vw, vh);
        }

        if (cur_tab == TAB_WORLD && play_mode_is_editing(&play_mode)) {
            /* Draw original panel stuff (like weather toggle) for now temporarily */
            /* panel_render(&panel, &editor, &resources, &weather, &obj_registry,
                         &atlas, &sprites_tab, &world, project.genre, &levels, vw, vh); */
            
            if (editor_settings.show_minimap)
                minimap_render(&world, &registry, &camera, vw, vh);
            
            /* Render New Editor Panes */
            left_pane_render(&left_pane, &editor, &registry, vh);
            inspector_pane_render(&inspector_pane, &editor, &registry, left_pane.selected_entity, vw, vh);
            
        } else if (cur_tab == TAB_WORLD && play_mode_is_playing(&play_mode)) {
            if (editor_settings.show_minimap)
                minimap_render(&world, &registry, &camera, vw, vh);

            const char *hint = "PLAYING -- click STOP (top right) to return to editing";
            text_draw(12.0f, (float)TOP_BAR_H + 8.0f, 1.3f, 0.6f, 0.85f, 0.65f, 1.0f, hint);
        } else if (cur_tab == TAB_SPRITES) {
            sprites_tab_render(&sprites_tab, vw, vh);
        } else if (cur_tab == TAB_OBJECTS) {
            objects_tab_render(&objects_tab, vw, vh);
        } else if (cur_tab == TAB_SETTINGS) {
            settings_tab_render(&settings_tab, &project, vw, vh);
        }

        /* Phase 5: Win/Lose end-state overlay */
        if (play_mode_ended(&play_mode)) {
            /* Full-screen dim */
            renderer_draw_quad(0.0f, 0.0f, (float)vw, (float)vh,
                               0.0f, 0.0f, 0.0f, 0.65f);

            float scale_big   = 4.0f;
            float scale_small = 1.8f;
            const Theme *th_ = theme_current();

            /* Title: "YOU WIN!" or "GAME OVER" */
            const char *title = (play_mode.state == PLAY_MODE_WON)
                                ? "YOU WIN!" : "GAME OVER";
            float title_w = text_measure_width(title, scale_big);
            float title_h = text_line_height(scale_big);
            float title_x = ((float)vw - title_w) * 0.5f;
            float title_y = (float)vh * 0.35f;

            /* Backing box */
            float box_pad = 30.0f;
            float msg_h = text_line_height(scale_small);
            float box_h = title_h + msg_h + box_pad * 3.0f + 40.0f;
            float box_w = (title_w > 400.0f ? title_w : 400.0f) + box_pad * 2.0f;
            float box_x = ((float)vw - box_w) * 0.5f;
            float box_y = title_y - box_pad;
            renderer_draw_quad(box_x, box_y, box_w, box_h,
                               0.06f, 0.06f, 0.08f, 0.92f);
            /* Border */
            float br, bg, bb;
            if (play_mode.state == PLAY_MODE_WON) {
                br = th_->accent_r; bg = th_->accent_g; bb = th_->accent_b;
            } else {
                br = th_->error_r; bg = th_->error_g; bb = th_->error_b;
            }
            renderer_draw_quad(box_x, box_y, box_w, 3.0f, br, bg, bb, 1.0f);
            renderer_draw_quad(box_x, box_y + box_h - 3.0f, box_w, 3.0f, br, bg, bb, 1.0f);

            /* Title text */
            text_draw(title_x, title_y, scale_big, br, bg, bb, 1.0f, title);

            /* Custom message */
            if (play_mode.end_message[0]) {
                float msg_w = text_measure_width(play_mode.end_message, scale_small);
                text_draw(((float)vw - msg_w) * 0.5f,
                          title_y + title_h + box_pad,
                          scale_small, 0.8f, 0.8f, 0.82f, 1.0f,
                          play_mode.end_message);
            }

            /* Hint */
            const char *hint = "Click STOP to return to editing";
            float hint_w = text_measure_width(hint, 1.3f);
            text_draw(((float)vw - hint_w) * 0.5f,
                      box_y + box_h - 30.0f,
                      1.3f, 0.5f, 0.5f, 0.55f, 1.0f, hint);
        }

        /* Dropdowns must be rendered last so they appear on top of everything */
        menubar_render_dropdowns(&menubar, vw, vh);

        renderer_end();
        window_swap(&window);
    }

    /* Shutdown */
    editor_settings_save(&editor_settings);
    lua_host_destroy(lua);
    if (play_mode_is_playing(&play_mode) || play_mode_ended(&play_mode)) playmode_snapshot_free(&play_snap);
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
