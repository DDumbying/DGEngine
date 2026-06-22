#include <stdbool.h>
#include <stdlib.h>
#include <SDL2/SDL.h>

#include "core/log.h"
#include "core/time.h"
#include "platform/window.h"
#include "platform/input.h"
#include "renderer/renderer.h"
#include "renderer/camera.h"
#include "world/world.h"
#include "ecs/registry.h"
#include "ecs/systems.h"
#include "game/prefabs.h"
#include "editor/editor.h"
#include "simulation/simulation.h"
#include "simulation/harvest.h"
#include "simulation/weather.h"
#include "ai/agent.h"
#include "ai/pathfinder.h"

#define WINDOW_W 1280
#define WINDOW_H  720
#define WORLD_W    32
#define WORLD_H    32
#define WORLD_SAVE_PATH   "world.dge"
#define ENTITY_SAVE_PATH  "entities.dge"

/*  Phase 3 proof-of-life: scatter trees and rocks across walkable land
    so there's something to look at besides terrain. Component values
    now live in game/prefabs.c, shared with the editor's PLACE mode —
    this spawner just decides *where*, prefab_spawn() decides *what*. */
static void spawn_demo_entities(Registry *reg, World *world) {
    int spawned = 0;
    for (int y = 0; y < world->height && spawned < 60; y++) {
        for (int x = 0; x < world->width && spawned < 60; x++) {
            const Tile *t = world_get_tile(world, x, y);
            if (!t || !tile_is_walkable(t->type) || t->type == TERRAIN_SAND)
                continue;
            if (t->type != TERRAIN_GRASS && t->type != TERRAIN_STONE)
                continue;

            int roll = rand() % 100;
            bool place_tree = (t->type == TERRAIN_GRASS && roll < 8);
            bool place_rock  = (t->type == TERRAIN_STONE && roll < 35);
            if (!place_tree && !place_rock) continue;

            PrefabKind kind = place_tree ? PREFAB_TREE : PREFAB_ROCK;
            if (prefab_spawn(reg, kind, (float)x, (float)y) == ENTITY_NULL)
                break;
            spawned++;
        }
    }
    LOG_INFO("Spawned %d demo resource entities", spawned);

    /* Spawn 3 workers at valid locations */
    int workers_spawned = 0;
    for (int y = 0; y < world->height && workers_spawned < 3; y++) {
        for (int x = 0; x < world->width && workers_spawned < 3; x++) {
            Tile *t = world_get_tile(world, x, y);
            if (t && tile_is_walkable(t->type)) {
                /* Make sure there isn't already an entity at this tile */
                bool occupied = false;
                for (Entity e = 0; e < (Entity)MAX_ENTITIES; e++) {
                    if (reg->alive[e]) {
                        TransformComponent *tc = entity_get_transform(reg, e);
                        if (tc && (int)tc->x == x && (int)tc->y == y) {
                            occupied = true;
                            break;
                        }
                    }
                }
                if (!occupied) {
                    prefab_spawn(reg, PREFAB_WORKER, (float)x, (float)y);
                    workers_spawned++;
                }
            }
        }
    }
    LOG_INFO("Spawned %d initial workers", workers_spawned);
}

int main(void) {
    Window window;
    if (!window_create(&window, "DGEngine — Phase 7 (Advanced)", WINDOW_W, WINDOW_H))
        return 1;

    if (!renderer_init()) {
        window_destroy(&window);
        return 1;
    }

    renderer_set_tile_size(64.0f, 32.0f);

    World world;
    if (!world_create(&world, WORLD_W, WORLD_H)) {
        renderer_shutdown();
        window_destroy(&window);
        return 1;
    }
    world_generate(&world, 1337u);

    Registry registry;
    registry_init(&registry);
    srand(1337u);
    spawn_demo_entities(&registry, &world);

    Editor editor;
    editor_init(&editor);

    SimClock     sim_clock;
    ResourceStore resources;
    simclock_init(&sim_clock);
    resource_store_init(&resources);

    WeatherSystem weather;
    weather_init(&weather);

    Camera camera;
    camera_init(&camera, WINDOW_W, WINDOW_H);
    /* Centre the grid on screen */
    camera.position.x = 0.0f;
    camera.position.y = (float)(WORLD_H) * 16.0f; /* half-tile offset */

    /* Initial timing tick so delta isn't garbage on frame 1. */
    dge_time_tick();

    LOG_INFO("DGEngine Phase 6 running.");
    LOG_INFO("  WASD to pan, scroll to zoom, TAB cycles editor mode.");
    LOG_INFO("  F5 save, F9 load, R regenerate, ESC quit.");
    LOG_INFO("  SELECT mode: H to harvest selected entity, M to command worker, P to pause/resume sim.");

    while (!input_quit_requested()) {
        /* ---- Input ---- */
        input_begin_frame();
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
            input_process_event(&ev);

        if (input_key_pressed(SDL_SCANCODE_ESCAPE))
            break;

        if (input_key_pressed(SDL_SCANCODE_F5)) {
            world_save(&world, WORLD_SAVE_PATH);
            registry_save(&registry, ENTITY_SAVE_PATH);
        }

        if (input_key_pressed(SDL_SCANCODE_F9)) {
            world_load(&world, WORLD_SAVE_PATH);
            registry_load(&registry, ENTITY_SAVE_PATH);
            editor.selected = ENTITY_HANDLE_NULL; /* old handle can't survive a full reload */
        }

        if (input_key_pressed(SDL_SCANCODE_R)) {
            world_generate(&world, (unsigned int)SDL_GetTicks());
            registry_init(&registry);
            spawn_demo_entities(&registry, &world);
            editor.selected = ENTITY_HANDLE_NULL;
        }

        /* ---- Time ---- */
        dge_time_tick();
        float dt = dge_time_delta();

        /* ---- Simulation clock ---- */
        float gdt = simclock_tick(&sim_clock, dt);

        /* ---- Weather update ---- */
        weather_update(&weather, gdt, dt, &camera);

        /* ---- Agent system update ---- */
        float speed_multiplier = weather_speed_multiplier(weather.type);
        system_update_agents(&registry, &world, &resources, gdt, speed_multiplier);

        /* Log elapsed game-time every 10 real seconds (rough, not exact)
           so you can see the clock running without it spamming the log. */
        static float log_timer = 0.0f;
        log_timer += dt;
        if (log_timer >= 10.0f) {
            LOG_INFO("Sim time: %.1f s (speed %.1fx)",
                     sim_clock.elapsed, (double)sim_clock.speed);
            log_timer = 0.0f;
        }

        /* P — pause / resume simulation (rendering keeps running). */
        if (input_key_pressed(SDL_SCANCODE_P)) {
            if (simclock_is_paused(&sim_clock)) {
                simclock_resume(&sim_clock);
                LOG_INFO("Simulation resumed.");
            } else {
                simclock_pause(&sim_clock);
                LOG_INFO("Simulation paused.");
            }
        }

        /* H — harvest selected entity (only makes sense in SELECT mode). */
        if (input_key_pressed(SDL_SCANCODE_H)) {
            if (entity_handle_valid(&registry, editor.selected)) {
                bool destroyed = system_harvest_entity(&registry,
                                                       editor.selected,
                                                       &resources);
                if (destroyed)
                    editor.selected = ENTITY_HANDLE_NULL;
            } else {
                LOG_WARN("H pressed but no valid entity selected "
                         "(use SELECT mode and click an entity first).");
            }
        }

        /* ---- Camera pan (WASD / arrow keys) ---- */
        float pan_speed = 200.0f * dt;
        if (input_key_down(SDL_SCANCODE_W) || input_key_down(SDL_SCANCODE_UP))
            camera_pan(&camera,  0.0f, -pan_speed);
        if (input_key_down(SDL_SCANCODE_S) || input_key_down(SDL_SCANCODE_DOWN))
            camera_pan(&camera,  0.0f,  pan_speed);
        if (input_key_down(SDL_SCANCODE_A) || input_key_down(SDL_SCANCODE_LEFT))
            camera_pan(&camera, -pan_speed, 0.0f);
        if (input_key_down(SDL_SCANCODE_D) || input_key_down(SDL_SCANCODE_RIGHT))
            camera_pan(&camera,  pan_speed, 0.0f);

        /* ---- Camera zoom (scroll wheel) ---- */
        int sx, sy;
        input_mouse_scroll(&sx, &sy);
        if (sy != 0) {
            int mx, my;
            input_mouse_pos(&mx, &my);
            camera_zoom_at(&camera, sy > 0 ? 1.1f : 0.9f,
                           (float)mx, (float)my);
        }

        /* ---- Update viewport size (handles window resize) ---- */
        int vw, vh;
        window_get_size(&window, &vw, &vh);
        camera.viewport_w = vw;
        camera.viewport_h = vh;

        /* ---- Editor ----
           After camera/viewport are current for this frame, so mouse
           picking uses the right transform; before rendering, since it
           can mutate the world/registry the frame is about to draw. */
        editor_update(&editor, &registry, &world, &camera);

        /* ---- Render ---- */
        renderer_clear(0.08f, 0.08f, 0.10f);
        renderer_begin(&camera);
        world_render(&world);
        editor_render(&editor, &registry);   /* ground highlights, under entities */
        system_render_entities(&registry);
        weather_render(&weather, &camera);
        renderer_end();

        window_swap(&window);
    }

    world_destroy(&world);
    renderer_shutdown();
    window_destroy(&window);
    LOG_INFO("DGEngine shut down cleanly.");
    return 0;
}
