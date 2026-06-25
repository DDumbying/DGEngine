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

typedef struct {
    char name[PROJECT_NAME_MAX];   /* display name, also folder name    */
    char path[PROJECT_PATH_MAX];   /* absolute path to the project folder */
    int  grid_w, grid_h;           /* world dimensions in tiles         */
    int  tile_w, tile_h;           /* isometric tile pixel size         */
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
