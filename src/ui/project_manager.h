#ifndef DGE_PROJECT_MANAGER_H
#define DGE_PROJECT_MANAGER_H

/*  The launcher screen — the first thing the user sees when DGEngine
    starts. Two panels side by side:

    LEFT — Recent Projects
      A scrollable list of recently opened project folders.
      Click one → loads it immediately.
      Each entry shows the project name (large) and its path (small).

    RIGHT — New Project / Open Project
      NEW PROJECT form:
        Name field   (TextInput, max 48 chars)
        Path field   (TextInput, max 255 chars, pre-filled with cwd/name)
        Grid W/H     (TextInput, numeric, 4–256)
        [Create]  button

      OPEN PROJECT form:
        Path field   (TextInput, max 255 chars)
        [Open]    button

    Result is communicated through ProjectManagerResult so main.c stays
    the owner of the Project and the editor state.                      */

#include <stdbool.h>
#include "../core/project.h"
#include "../ui/textinput.h"

typedef enum {
    PM_RESULT_NONE = 0,   /* still on the launcher screen              */
    PM_RESULT_OPEN,       /* user picked/created a project → *project  */
    PM_RESULT_QUIT,       /* user closed the window from this screen   */
} ProjectManagerResult;

/* Which right-panel tab is visible. */
typedef enum {
    PM_TAB_NEW  = 0,
    PM_TAB_OPEN = 1,
} PMTab;

/* Which text field currently has focus (only one at a time). */
typedef enum {
    PM_FOCUS_NONE = 0,
    PM_FOCUS_NEW_NAME,
    PM_FOCUS_NEW_PATH,
    PM_FOCUS_NEW_W,
    PM_FOCUS_NEW_H,
    PM_FOCUS_OPEN_PATH,
} PMFocus;

typedef struct {
    /* Right-panel tab */
    PMTab   tab;
    PMFocus focus;

    /* NEW PROJECT form fields */
    TextInput new_name;
    TextInput new_path;
    TextInput new_w;
    TextInput new_h;

    /* OPEN PROJECT form field */
    TextInput open_path;

    /* Error message to display below the active form (empty = none). */
    char error_msg[128];

    /* Recent projects list (loaded once at init). */
    char recent_paths[PROJECT_RECENT_MAX][PROJECT_PATH_MAX];
    int  recent_count;

    /* Scroll offset for the recent list (in items). */
    int  recent_scroll;
} ProjectManager;

void project_manager_init(ProjectManager *pm);

/*  Call every frame while on the launcher screen.
    vw/vh are the current viewport dimensions.
    If a project was chosen or created, *out is populated and the
    function returns PM_RESULT_OPEN. The caller must then call
    project_recent_add(out->path) and enter the editor.
    Returns PM_RESULT_QUIT if the window was closed.                    */
ProjectManagerResult project_manager_update(ProjectManager *pm,
                                             int vw, int vh,
                                             Project *out);

/*  Draw the launcher. Call between renderer_begin_ui / renderer_end.   */
void project_manager_render(const ProjectManager *pm, int vw, int vh);

#endif /* DGE_PROJECT_MANAGER_H */
