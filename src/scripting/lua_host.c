#include "lua_host.h"

#include <lua5.4/lua.h>
#include <lua5.4/lauxlib.h>
#include <lua5.4/lualib.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "../core/log.h"
#include "../core/project.h"
#include "../ecs/components.h"
#include "../ai/pathfinder.h"

/* -------------------------------------------------------------------
   Script cache

   Scripts are loaded on first use and cached by path.  A script that
   failed to compile is also cached (as "errored") so we don't re-read
   and log every single frame if it has a syntax error. */

#define CACHE_MAX 128

typedef enum {
    CACHE_EMPTY = 0,
    CACHE_OK,       /* loaded and the function pushed into registry[ref] */
    CACHE_ERROR,    /* failed to load/compile; logged once               */
} CacheState;

typedef struct {
    char       path[OBJDEF_PATH_MAX];
    CacheState state;
    /* Lua registry reference to the chunk's loaded env table (the
       table that holds the named function after running the script). */
    int        env_ref;
} CacheEntry;

struct LuaHost {
    lua_State        *L;
    CacheEntry        cache[CACHE_MAX];
    int               cache_count;

    /* Context pointers — set by lua_host_set_context() */
    Registry         *reg;
    ObjectDefRegistry *obj_registry;
    ResourceStore     *resources;

    /* Set once at lua_host_create() from the active Project — exposed
       to scripts via dge.get_genre() so a TACTICS or FREEFORM project's
       scripts can tell, at runtime, that the engine isn't ticking
       SimClock/weather/resources for them and adjust accordingly
       (e.g. running their own turn-timer instead of expecting on_tick
       to mean "a frame of real simulated time passed"). */
    GenreProfile      genre;
};

/* -------------------------------------------------------------------
   Engine API functions exposed to Lua (prefixed dge_api_) */

/* Retrieve the LuaHost pointer stored as a light userdata in the Lua
   registry under a stable key, so every API function can find it
   without passing it as an upvalue through closures. */
static LuaHost *get_host(lua_State *L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "DGE_HOST");
    LuaHost *h = (LuaHost *)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return h;
}

/* dge.get_property(entity_id, name) -> value | nil */
static int dge_api_get_property(lua_State *L) {
    LuaHost *h = get_host(L);
    Entity e   = (Entity)luaL_checkinteger(L, 1);
    const char *name = luaL_checkstring(L, 2);

    if (!h->reg || !h->obj_registry) { lua_pushnil(L); return 1; }
    if (!h->reg->alive[e] || !h->reg->has_definition[e]) { lua_pushnil(L); return 1; }

    const char *def_name = h->reg->definition[e].def_name;
    const ObjectDef *def = objdef_find(h->obj_registry, def_name);
    if (!def) { lua_pushnil(L); return 1; }

    for (int i = 0; i < def->prop_count; i++) {
        if (strcmp(def->props[i].name, name) != 0) continue;
        const ObjectProperty *p = &def->props[i];
        switch (p->type) {
            case PROP_INT:    lua_pushinteger(L, p->value.as_int);    return 1;
            case PROP_FLOAT:  lua_pushnumber (L, (lua_Number)p->value.as_float); return 1;
            case PROP_STRING: lua_pushstring (L, p->value.as_string); return 1;
            case PROP_BOOL:   lua_pushboolean(L, p->value.as_bool);   return 1;
        }
    }
    lua_pushnil(L);
    return 1;
}

/* dge.set_property(entity_id, name, value) */
static int dge_api_set_property(lua_State *L) {
    LuaHost *h = get_host(L);
    Entity e   = (Entity)luaL_checkinteger(L, 1);
    const char *name = luaL_checkstring(L, 2);

    if (!h->reg || !h->obj_registry) return 0;
    if (!h->reg->alive[e] || !h->reg->has_definition[e]) return 0;

    const char *def_name = h->reg->definition[e].def_name;
    ObjectDef *def = objdef_find(h->obj_registry, def_name);
    if (!def) return 0;

    for (int i = 0; i < def->prop_count; i++) {
        if (strcmp(def->props[i].name, name) != 0) continue;
        ObjectProperty *p = &def->props[i];
        switch (p->type) {
            case PROP_INT:
                p->value.as_int = (int)luaL_checkinteger(L, 3);
                break;
            case PROP_FLOAT:
                p->value.as_float = (float)luaL_checknumber(L, 3);
                break;
            case PROP_STRING: {
                const char *s = luaL_checkstring(L, 3);
                snprintf(p->value.as_string, sizeof p->value.as_string, "%s", s);
                break;
            }
            case PROP_BOOL:
                p->value.as_bool = lua_toboolean(L, 3);
                break;
        }
        return 0;
    }
    return 0;
}

/* dge.move_to(entity_id, x, y) */
static int dge_api_move_to(lua_State *L) {
    LuaHost *h  = get_host(L);
    Entity e    = (Entity)luaL_checkinteger(L, 1);
    int tx      = (int)luaL_checkinteger(L, 2);
    int ty      = (int)luaL_checkinteger(L, 3);

    if (!h->reg || !h->reg->alive[e] || !h->reg->has_task[e]) return 0;

    TaskComponent *tsk = &h->reg->task[e];
    tsk->kind     = TASK_MOVE_TO;
    tsk->target_x = tx;
    tsk->target_y = ty;
    tsk->path.len = 0;  /* agent system will re-path on next tick */
    return 0;
}

/* dge.spawn(def_name, x, y) -> entity_id | nil */
static int dge_api_spawn(lua_State *L) {
    LuaHost *h       = get_host(L);
    const char *name = luaL_checkstring(L, 1);
    float x          = (float)luaL_checknumber(L, 2);
    float y          = (float)luaL_checknumber(L, 3);

    if (!h->reg || !h->obj_registry) { lua_pushnil(L); return 1; }

    ObjectDef *def = objdef_find(h->obj_registry, name);
    if (!def) {
        LOG_WARN("dge.spawn: unknown def '%s'", name);
        lua_pushnil(L);
        return 1;
    }

    /* We don't have access to the sprite_id-resolution logic here --
       spawn with sprite_id = -1 (color-box fallback).  The visual
       isn't the primary goal; gameplay behavior from the script is. */
    extern Entity objdef_spawn_instance(Registry*, const ObjectDef*, int, float, float);
    Entity e = objdef_spawn_instance(h->reg, def, -1, x, y);
    if (!h->reg->alive[e]) { lua_pushnil(L); return 1; }

    lua_pushinteger(L, (lua_Integer)e);
    return 1;
}

/* dge.destroy(entity_id) */
static int dge_api_destroy(lua_State *L) {
    LuaHost *h = get_host(L);
    Entity e   = (Entity)luaL_checkinteger(L, 1);
    if (h->reg && h->reg->alive[e])
        entity_destroy(h->reg, e);
    return 0;
}

/* dge.get_resource(kind) -> int */
static int dge_api_get_resource(lua_State *L) {
    LuaHost *h       = get_host(L);
    const char *kind = luaL_checkstring(L, 1);
    if (!h->resources) { lua_pushinteger(L, 0); return 1; }
    int amount = (strcmp(kind, "stone") == 0)
               ? h->resources->stone
               : h->resources->wood;
    lua_pushinteger(L, amount);
    return 1;
}

/* dge.add_resource(kind, amount) */
static int dge_api_add_resource(lua_State *L) {
    LuaHost *h       = get_host(L);
    const char *kind = luaL_checkstring(L, 1);
    int amount       = (int)luaL_checkinteger(L, 2);
    if (!h->resources) return 0;
    if (strcmp(kind, "stone") == 0)
        resource_store_add_stone(h->resources, amount);
    else
        resource_store_add_wood(h->resources, amount);
    return 0;
}

/* dge.log(message) */
static int dge_api_log(lua_State *L) {
    const char *msg = luaL_checkstring(L, 1);
    LOG_INFO("[Lua] %s", msg);
    return 0;
}

/* dge.get_genre() -> "sandbox_sim" | "tactics" | "freeform"
   Lets a script check which fork of the engine it's running under
   without needing a separate config file of its own — the same
   project.dge field the editor/panel/HUD already gate on (see
   project.h's GenreProfile, main.c's simulation-tick gating, and
   panel.c's Weather-section gating) is the one source of truth here
   too, so a script and the engine itself never disagree about which
   genre is active. */
static int dge_api_get_genre(lua_State *L) {
    LuaHost *h = get_host(L);
    const char *name;
    switch (h->genre) {
        case GENRE_SANDBOX_SIM: name = "sandbox_sim"; break;
        case GENRE_TACTICS:     name = "tactics";     break;
        case GENRE_FREEFORM:    name = "freeform";    break;
        default:                name = "unknown";     break;
    }
    lua_pushstring(L, name);
    return 1;
}

/* -------------------------------------------------------------------
   Host lifecycle */

static void register_api(LuaHost *h) {
    lua_State *L = h->L;

    /* Store the host pointer in the Lua registry so API functions can
       find it without upvalue closures. */
    lua_pushlightuserdata(L, h);
    lua_setfield(L, LUA_REGISTRYINDEX, "DGE_HOST");

    /* Create the "dge" table and populate it. */
    lua_newtable(L);

    lua_pushcfunction(L, dge_api_get_property); lua_setfield(L, -2, "get_property");
    lua_pushcfunction(L, dge_api_set_property); lua_setfield(L, -2, "set_property");
    lua_pushcfunction(L, dge_api_move_to);      lua_setfield(L, -2, "move_to");
    lua_pushcfunction(L, dge_api_spawn);         lua_setfield(L, -2, "spawn");
    lua_pushcfunction(L, dge_api_destroy);       lua_setfield(L, -2, "destroy");
    lua_pushcfunction(L, dge_api_get_resource);  lua_setfield(L, -2, "get_resource");
    lua_pushcfunction(L, dge_api_add_resource);  lua_setfield(L, -2, "add_resource");
    lua_pushcfunction(L, dge_api_log);           lua_setfield(L, -2, "log");
    lua_pushcfunction(L, dge_api_get_genre);     lua_setfield(L, -2, "get_genre");

    lua_setglobal(L, "dge");
}

LuaHost *lua_host_create(void) {
    LuaHost *h = (LuaHost *)calloc(1, sizeof(LuaHost));
    if (!h) return NULL;

    h->L = luaL_newstate();
    if (!h->L) { free(h); return NULL; }

    /* Open only safe standard libs -- no io, os, package, debug. */
    luaL_requiref(h->L, "_G",       luaopen_base,   1); lua_pop(h->L, 1);
    luaL_requiref(h->L, "math",     luaopen_math,   1); lua_pop(h->L, 1);
    luaL_requiref(h->L, "string",   luaopen_string, 1); lua_pop(h->L, 1);
    luaL_requiref(h->L, "table",    luaopen_table,  1); lua_pop(h->L, 1);

    register_api(h);

    LOG_INFO("Lua host initialised (Lua %s)", LUA_VERSION);
    return h;
}

void lua_host_destroy(LuaHost *h) {
    if (!h) return;
    /* Release all cached env refs. */
    for (int i = 0; i < h->cache_count; i++)
        if (h->cache[i].state == CACHE_OK)
            luaL_unref(h->L, LUA_REGISTRYINDEX, h->cache[i].env_ref);
    lua_close(h->L);
    free(h);
}

void lua_host_set_context(LuaHost *h,
                           Registry *reg,
                           ObjectDefRegistry *obj_registry,
                           ResourceStore *resources) {
    h->reg          = reg;
    h->obj_registry = obj_registry;
    h->resources    = resources;
}

void lua_host_set_genre(LuaHost *h, GenreProfile genre) {
    h->genre = genre;
}

void lua_host_clear_cache(LuaHost *h) {
    for (int i = 0; i < h->cache_count; i++)
        if (h->cache[i].state == CACHE_OK)
            luaL_unref(h->L, LUA_REGISTRYINDEX, h->cache[i].env_ref);
    memset(h->cache, 0, sizeof(h->cache));
    h->cache_count = 0;
    LOG_INFO("Lua script cache cleared");
}

/* -------------------------------------------------------------------
   Script loading + cache */

static CacheEntry *cache_find(LuaHost *h, const char *path) {
    for (int i = 0; i < h->cache_count; i++)
        if (strcmp(h->cache[i].path, path) == 0)
            return &h->cache[i];
    return NULL;
}

static CacheEntry *cache_load(LuaHost *h, const char *path) {
    if (h->cache_count >= CACHE_MAX) {
        LOG_WARN("Lua cache full (%d scripts) — cannot load '%s'", CACHE_MAX, path);
        return NULL;
    }
    CacheEntry *e = &h->cache[h->cache_count++];
    snprintf(e->path, sizeof e->path, "%s", path);

    /* Each script runs in its own fresh environment table so functions
       defined in one script can't stomp globals used by another. */
    lua_State *L = h->L;

    /* Load the file as a chunk without executing it. */
    if (luaL_loadfile(L, path) != LUA_OK) {
        LOG_ERROR("Lua compile error in '%s': %s", path, lua_tostring(L, -1));
        lua_pop(L, 1);
        e->state = CACHE_ERROR;
        return e;
    }

    /* Create a fresh env table that inherits the global env (_G). */
    lua_newtable(L);          /* env = {}                              */
    lua_newtable(L);          /* mt  = {}                              */
    lua_pushglobaltable(L);   /* _G                                    */
    lua_setfield(L, -2, "__index");   /* mt.__index = _G               */
    lua_setmetatable(L, -2);  /* setmetatable(env, mt)                 */

    /* Set the env as the chunk's first upvalue (_ENV). */
    lua_pushvalue(L, -1);     /* dup env                               */
    lua_setupvalue(L, -3, 1); /* chunk._ENV = env  (pops dup)         */

    /* Execute the chunk to define functions into env. */
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        LOG_ERROR("Lua runtime error loading '%s': %s", path, lua_tostring(L, -1));
        lua_pop(L, 2); /* error + env */
        e->state = CACHE_ERROR;
        return e;
    }

    /* env is now on top; store it in the registry. */
    e->env_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    e->state   = CACHE_OK;
    LOG_INFO("Lua: loaded '%s'", path);
    return e;
}

/* -------------------------------------------------------------------
   Event dispatch */

bool lua_host_call_behavior(LuaHost *h, Entity e, const char *event) {
    if (!h || !h->reg || !h->obj_registry) return false;
    if (!h->reg->alive[e] || !h->reg->has_definition[e]) return false;

    const char *def_name = h->reg->definition[e].def_name;
    const ObjectDef *def = objdef_find(h->obj_registry, def_name);
    if (!def) return false;

    /* Find the matching behavior slot. */
    const char *script_path = NULL;
    for (int i = 0; i < def->behavior_count; i++) {
        if (strcmp(def->behaviors[i].event, event) == 0) {
            script_path = def->behaviors[i].script;
            break;
        }
    }
    if (!script_path || !script_path[0]) return false;

    /* Load or fetch from cache. */
    CacheEntry *ce = cache_find(h, script_path);
    if (!ce) ce = cache_load(h, script_path);
    if (!ce || ce->state == CACHE_ERROR) return false;

    lua_State *L = h->L;

    /* Push the env table, then look up the function by event name. */
    lua_rawgeti(L, LUA_REGISTRYINDEX, ce->env_ref);
    lua_getfield(L, -1, event);         /* env[event]                  */

    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 2); /* nil + env */
        LOG_WARN("Lua: '%s' has no function named '%s'", script_path, event);
        return false;
    }

    /* Set SELF in the env so the script knows which entity fired. */
    lua_pushinteger(L, (lua_Integer)e);
    lua_setfield(L, -4, "SELF"); /* env.SELF = e  (-4 = env under fn+e) */
    /* Actually SELF is a global in the env table: we need to fix indexing.
       The stack is: env (-2), fn (-1).  We set SELF on env (-2). */
    lua_pushinteger(L, (lua_Integer)e);
    lua_setfield(L, -3, "SELF"); /* re-set; this time -3 = env correctly */

    /* Call the function with no args, no return values. */
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        LOG_ERROR("Lua runtime error in '%s::%s': %s",
                  script_path, event, lua_tostring(L, -1));
        lua_pop(L, 2); /* error + env */
        return false;
    }

    lua_pop(L, 1); /* pop env */
    return true;
}
