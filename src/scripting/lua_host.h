#ifndef DGE_LUA_HOST_H
#define DGE_LUA_HOST_H

/*  P2 — Lua scripting host (roadmap items 8, 9, 10).

    One shared lua_State for the whole engine session.  Scripts are
    loaded on first use and cached, so repeated calls to the same
    event (e.g. on_tick firing 60x/sec) never re-read the file.

    Engine API exposed to Lua (callable from any script):

        -- Entity property bag
        dge.get_property(entity_id, name)  -> value | nil
        dge.set_property(entity_id, name, value)

        -- Movement / task dispatch (mirrors editor M-command)
        dge.move_to(entity_id, x, y)

        -- World mutation
        dge.spawn(def_name, x, y)          -> entity_id | nil
        dge.destroy(entity_id)

        -- Resource store
        dge.get_resource(kind)             -> int   (kind: any resource name a
                                                       project uses -- Phase 2B
                                                       retired the old fixed
                                                       "wood"|"stone" ResourceKind
                                                       enum, see simulation.h)
        dge.add_resource(kind, amount)

        -- Genre — which fork of the engine this project is running
        -- (set at project creation, see core/project.h's GenreProfile)
        dge.get_genre()                    -> "sandbox_sim"|"tactics"|"freeform"

        -- Logging
        dge.log(message)

    Scripts receive their entity id as a global "SELF" before the
    event function is called.  The function name must match the event:

        -- scripts/on_click.lua
        function on_click()
            dge.log("clicked entity " .. SELF)
            local hp = dge.get_property(SELF, "health")
            if hp then dge.set_property(SELF, "health", hp - 10) end
        end

    Compile errors are logged and cached so we don't spam the console
    at 60 fps if a script has a syntax error.  Call lua_host_clear_cache()
    after the user edits a script to force a re-load. */

#include <stdbool.h>
#include "../ecs/registry.h"
#include "../core/object_def.h"
#include "../core/project.h"
#include "../simulation/simulation.h"

typedef struct LuaHost LuaHost;

/* Lifecycle ---------------------------------------------------------- */

/* Allocate and initialise the host.  Opens standard safe Lua libs
   (base, math, string, table) — io/os/package/debug are intentionally
   excluded: scripts shouldn't be able to open arbitrary files or exit
   the process.  Returns NULL on allocation failure (very unlikely). */
LuaHost *lua_host_create(void);

/* Shut down Lua and free the host.  Safe to call with NULL. */
void lua_host_destroy(LuaHost *h);

/* Inject the live world-state pointers so the engine-API functions
   can reach Registry, ObjectDefRegistry, and ResourceStore.  Must be
   called at least once before lua_host_call_behavior(), and again
   whenever any of these pointers changes (e.g. after a world reload). */
void lua_host_set_context(LuaHost *h,
                           Registry         *reg,
                           ObjectDefRegistry *obj_registry,
                           ResourceStore     *resources);

/* Tell the host which GenreProfile the active project uses, so
   dge.get_genre() can report it to scripts. Call once after loading
   project.dge (or whenever a different project is opened) — same
   "set it when it changes, read it many times" shape as
   lua_host_set_context() above. */
void lua_host_set_genre(LuaHost *h, GenreProfile genre);

/* Script cache ------------------------------------------------------- */

/* Force all cached scripts to be reloaded on next call.  Call this
   when the user edits a script file so stale byte-code is evicted. */
void lua_host_clear_cache(LuaHost *h);

/* Event dispatch ----------------------------------------------------- */

/* Look up the ObjectDef for entity `e`, find its behavior slot whose
   event name matches `event` (e.g. "on_click"), load + cache the Lua
   file, set SELF = e, and call the matching function.

   Returns true  if the function ran without a runtime error.
   Returns false if: entity has no def, def has no matching behavior,
                     the script file is missing, it failed to compile,
                     or the function threw a runtime error.
   In all error cases the reason is logged exactly once per unique
   script path + compile state (not once per call). */
bool lua_host_call_behavior(LuaHost *h, Entity e, const char *event);

/* Condition evaluation (Phase 5 — Win/Lose) ------------------------- */

/* Load and execute a standalone Lua script file (not entity-bound).
   The script must define a function named `check()` that returns true
   or false. Example:

       -- scripts/win.lua
       function check()
           return dge.get_resource("wood") >= 100
       end

   Returns true  if the script's check() returned a truthy value.
   Returns false if: the file is missing, it failed to compile,
                     check() is not defined, or check() returned
                     false/nil. Uses the same cache as entity behaviors. */
bool lua_host_eval_condition(LuaHost *h, const char *script_path);

#endif /* DGE_LUA_HOST_H */
