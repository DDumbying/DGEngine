#ifndef DGE_PROJECT_H
#define DGE_PROJECT_H

/*  A DGEngine project is a folder on disk containing:
      project.dge      — metadata (this struct, plain-text key=value)
      world.dge        — tilemap
      entities.dge     — placed entity instances
      objects/         — user-defined object definitions  (Phase L)
      scripts/         — Lua behavior files              (Phase N)
      assets/
        sprites.png    — sprite sheet (user-provided)
        sprites.meta   — sprite cell names               (Phase K)

    project.dge is plain text so it's human-readable and git-friendly:
      name=my_game
      grid_w=64
      grid_h=64
      tile_w=64
      tile_h=32

    Everything the engine needs at startup lives here. Paths to world.dge
    etc. are always relative to the project folder — the engine cd's into
    the project folder before opening any save files, so existing
    world_save/world_load paths ("world.dge") work unchanged.           */

#include <stdbool.h>

#define PROJECT_NAME_MAX  64
#define PROJECT_PATH_MAX 256

/*  Hard limits for grid dimensions — large enough to be useful,
    small enough that a blank world_create() never OOMs on any
    reasonable machine (256×256 tiles × sizeof(Tile) ≈ 512 KB).     */
#define PROJECT_GRID_MIN   4
#define PROJECT_GRID_MAX 256

#define PROJECT_RECENT_MAX  8   /* most-recently-used list length */

/*  World topology — chosen once at project creation time, applied by
    world_topology_generate() (world/world_generator.h) the first time
    a brand-new project is opened (i.e. world.dge doesn't exist yet).
    Lives here rather than in ui/project_manager.h because it's part
    of the persisted project metadata, not just New-Project-form UI
    state -- world/world_generator.c (a non-UI module) needs it too. */
typedef enum {
    WORLD_TOPO_RECT     = 0,  /* Filled rectangle (classic, default)    */
    WORLD_TOPO_FREEFORM = 1,  /* Any shape — tiles individually enabled */
    WORLD_TOPO_ISLAND   = 2,  /* Auto-generated island (water border)   */
    WORLD_TOPO_ROOMS    = 3,  /* Dungeon room grid (rooms + corridors)  */
    WORLD_TOPO_COUNT    = 4,
} WorldTopology;

/*  GenreProfile — chosen once at project creation, same moment and same
    spot as WorldTopology above. This is the actual fork point that makes
    DGEngine build more than one kind of isometric game: everything that
    used to be unconditional (resource HUD, weather ticking, harvest/build
    AI tasks, the Weather/Resources panel sections) now checks this first.

    Adding a profile here is the ENGINE-level extension point. Adding a
    *project* that wants different terrain/resources/buildings than the
    ones shipped with GENRE_SANDBOX_SIM does NOT touch this enum — that's
    what terrain.def / resources.def (data files, not C) are for. This
    enum only decides which *systems* run, not what they're populated
    with. A tactics game and a sandbox-sim game can both want resources
    in principle — but TACTICS turning ResourceStore/weather off by
    default is the right starting assumption for the common case, and a
    project can still hand-wire its own loop entirely through Lua even
    inside GENRE_FREEFORM where the engine assumes nothing at all. */
typedef enum {
    GENRE_SANDBOX_SIM = 0, /* today's behavior: resources, weather, harvest/build AI.
                               This stays the default so every existing project keeps
                               working unchanged after this field is added. */
    GENRE_TACTICS     = 1, /* turn-based, no resources/weather ticking, no harvest/build
                               task vocabulary — agent.c falls back to script-only tasks */
    GENRE_FREEFORM    = 2, /* blank slate: simulation loop stays off entirely, panel
                               shows only PAINT/PLACE/SELECT/SHAPE, everything else is
                               built by the project's own Lua scripts */
    GENRE_COUNT       = 3,
} GenreProfile;

const char *genre_profile_name(GenreProfile g);
const char *genre_profile_desc(GenreProfile g);

typedef struct {
    char name[PROJECT_NAME_MAX];   /* display name, also folder name    */
    char path[PROJECT_PATH_MAX];   /* absolute path to the project folder */
    int  grid_w, grid_h;           /* world dimensions in tiles         */
    int  tile_w, tile_h;           /* isometric tile pixel size         */
    WorldTopology topology;        /* chosen at creation, see above     */
    GenreProfile  genre;           /* chosen at creation, see above     */
} Project;

/* ---- project.dge I/O ---------------------------------------------- */

/*  Write project metadata to <project.path>/project.dge.
    Returns false on I/O error (logs the reason).                       */
bool project_save(const Project *p);

/*  Read project metadata from <folder>/project.dge into *p.
    p->path is set to folder regardless of success so the caller always
    knows where the load was attempted.
    Returns false if the file doesn't exist or can't be parsed.         */
bool project_load(Project *p, const char *folder);

/* ---- Recent project list ------------------------------------------ */

/*  Reads ~/.dgengine (one absolute project path per line, newest first).
    paths[i] is set to "" for any slot past the end of the file.
    Returns the number of valid paths found (0 if file missing).        */
int  project_recent_load(char paths[PROJECT_RECENT_MAX][PROJECT_PATH_MAX]);

/*  Prepends folder to the recent list (deduplicating if already present)
    and writes the updated list back to ~/.dgengine.                    */
void project_recent_add(const char *folder);

/*  Removes folder from the recent list. */
void project_recent_remove(const char *folder);

/*  Clears the entire recent list. */
void project_recent_clear(void);

/*  Deletes the project folder and all its contents from disk. */
bool project_delete_from_disk(const char *folder);

/* ---- Helpers ----------------------------------------------------------*/

/*  Create the standard sub-folders (objects/, scripts/, assets/) inside
    an already-existing project folder. Safe to call on an existing
    project (folders that already exist are silently skipped).          */
void project_create_dirs(const Project *p);

/*  Returns true if <folder>/project.dge exists — used by the project
    manager to distinguish a valid project folder from an arbitrary path. */
bool project_folder_valid(const char *folder);

/*  Set *p to safe defaults (64×64 grid, 64×32 tiles, empty name/path). */
void project_defaults(Project *p);

#endif /* DGE_PROJECT_H */
