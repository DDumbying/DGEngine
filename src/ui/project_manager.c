#include "project_manager.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#include "text.h"
#include "textinput.h"
#include "../renderer/renderer.h"
#include "../platform/input.h"
#include "../core/log.h"

#ifdef _WIN32
#  include <direct.h>
#  define dge_mkdir(p)  _mkdir(p)
#  define PATH_SEP      "\\"
#  define getcwd        _getcwd
#else
#  include <unistd.h>
#  define dge_mkdir(p)  mkdir((p), 0755)
#  define PATH_SEP      "/"
#endif

/* -----------------------------------------------------------------------
   All layout is derived from vw/vh at draw-time so it responds correctly
   to any window size.  No hardcoded pixel positions outside this block.
   ----------------------------------------------------------------------- */

/* Scales — chosen so text is readable at 1280×720 and scales down
   gracefully at smaller windows. */
#define SCALE_TITLE  3.0f
#define SCALE_LABEL  1.8f
#define SCALE_BODY   1.8f
#define SCALE_SMALL  1.5f
#define SCALE_HINT   1.4f

/* Fixed sizes that don't depend on window dimensions. */
#define BTN_H        28.0f
#define FIELD_H      26.0f
#define LINE_GAP      8.0f
#define ITEM_GAP      4.0f

/* Derived layout — computed once per frame in layout_compute(). */
typedef struct {
    float pad;          /* outer margin                                  */
    float title_h;      /* height of the title row (text + gap below)    */
    float hint_h;       /* height reserved at the very bottom            */
    float div_x;        /* x position of the vertical divider            */
    float div_y;        /* y position of the horizontal divider          */
    bool  is_vertical;  /* true if panel stacked vertically              */
    float left_w;       /* usable width of the left (recent) panel       */
    float right_x;      /* x where the right panel content starts        */
    float right_w;      /* usable width of the right panel               */
    float content_top;  /* y below the title where both panels start     */
    float content_bot;  /* y above the hint strip where content ends     */
    float tab_w;        /* width of each tab button                      */
    float tab_h;        /* height of each tab button                     */
    float form_y;       /* y where the form fields start (below tabs)    */
    float recent_item_h;
} Layout;

static Layout layout_compute(int vw, int vh) {
    Layout L;
    L.pad   = (float)vw * 0.018f;
    if (L.pad < 12.0f) L.pad = 12.0f;

    L.title_h    = text_line_height(SCALE_TITLE) + L.pad;
    L.hint_h     = text_line_height(SCALE_HINT)  + L.pad * 2.0f;
    L.content_top = L.pad + L.title_h;
    L.content_bot = (float)vh - L.hint_h;

    L.is_vertical = (vw < 768);

    if (L.is_vertical) {
        L.div_x = 0;
        L.div_y = L.content_top + (L.content_bot - L.content_top) * 0.40f;
        L.left_w = (float)vw - L.pad * 2.0f;
        L.right_x = L.pad;
        L.right_w = (float)vw - L.pad * 2.0f;
        L.tab_w = (L.right_w - ITEM_GAP) * 0.5f;
        L.tab_h = BTN_H;
        L.form_y = L.div_y + L.pad + L.tab_h + LINE_GAP;
        L.recent_item_h = text_line_height(SCALE_BODY) * 2.2f;
    } else {
        L.div_x = (float)vw * 0.38f;
        if (L.div_x < 220.0f) L.div_x = 220.0f;
        L.div_y = 0;
        L.left_w  = L.div_x - L.pad * 2.0f;
        L.right_x = L.div_x + L.pad;
        L.right_w = (float)vw - L.right_x - L.pad;
        L.tab_w = (L.right_w - ITEM_GAP) * 0.5f;
        L.tab_h = BTN_H;
        L.form_y = L.content_top + L.tab_h + LINE_GAP;
        L.recent_item_h = text_line_height(SCALE_BODY) * 2.5f;
    }
    return L;
}

/* ---- Low-level draw helpers ---------------------------------------- */

static bool hittest(float x, float y, float w, float h, int mx, int my) {
    return mx >= (int)x && mx < (int)(x + w) &&
           my >= (int)y && my < (int)(y + h);
}

static void draw_box(float x, float y, float w, float h,
                     float fr, float fg, float fb,
                     float br, float bg, float bb) {
    renderer_draw_quad(x,     y,     w,    h,    fr, fg, fb, 1.0f);
    renderer_draw_quad(x,     y,     w,    1.0f, br, bg, bb, 1.0f);
    renderer_draw_quad(x,     y+h-1, w,    1.0f, br, bg, bb, 1.0f);
    renderer_draw_quad(x,     y,     1.0f, h,    br, bg, bb, 1.0f);
    renderer_draw_quad(x+w-1, y,     1.0f, h,    br, bg, bb, 1.0f);
}

static bool draw_button(float x, float y, float w, float h,
                         const char *label, bool primary,
                         int mx, int my) {
    bool hover = hittest(x, y, w, h, mx, my);
    bool click = hover && input_mouse_button_pressed(SDL_BUTTON_LEFT);
    float fr = primary ? (hover ? 0.22f : 0.17f) : (hover ? 0.20f : 0.13f);
    float fg = primary ? (hover ? 0.72f : 0.60f) : (hover ? 0.28f : 0.20f);
    float fb = primary ? (hover ? 0.42f : 0.32f) : (hover ? 0.20f : 0.13f);
    draw_box(x, y, w, h, fr, fg, fb,
             primary ? 0.35f : 0.30f,
             primary ? 0.85f : 0.38f,
             primary ? 0.50f : 0.30f);
    float lw = text_measure_width(label, SCALE_BODY);
    float lh = text_line_height(SCALE_BODY);
    text_draw(x + (w - lw) * 0.5f, y + (h - lh) * 0.5f, SCALE_BODY,
              primary ? 0.90f : 0.72f,
              primary ? 1.00f : 0.82f,
              primary ? 0.90f : 0.72f, 1.0f, label);
    return click;
}

/* Draw label + input box. Returns true if the box was clicked. */
static bool draw_field(float x, float y, float w,
                        const char *label, const TextInput *field,
                        bool focused, int mx, int my) {
    text_draw(x, y, SCALE_SMALL, 0.52f, 0.52f, 0.55f, 1.0f, label);
    float fy = y + text_line_height(SCALE_SMALL) + 2.0f;
    bool hov = hittest(x, fy, w, FIELD_H, mx, my);
    float fbg = focused ? 0.17f : (hov ? 0.13f : 0.09f);
    draw_box(x, fy, w, FIELD_H, fbg, fbg, fbg,
             focused ? 0.32f : 0.24f,
             focused ? 0.72f : 0.32f,
             focused ? 0.42f : 0.24f);
    float th = text_line_height(SCALE_BODY);
    textinput_render(field, x + 5.0f, fy + (FIELD_H - th) * 0.5f,
                     SCALE_BODY, 1.0f, 1.0f, 1.0f, 1.0f);
    return hov && input_mouse_button_pressed(SDL_BUTTON_LEFT);
}

/* Height a labeled field occupies vertically. */
static float field_block_h(void) {
    return text_line_height(SCALE_SMALL) + 2.0f + FIELD_H + LINE_GAP;
}

/* ---- Focus management --------------------------------------------- */

static void set_focus(ProjectManager *pm, PMFocus f) {
    if (pm->focus == f) return;
    switch (pm->focus) {
        case PM_FOCUS_NEW_NAME:  textinput_unfocus(&pm->new_name);  break;
        case PM_FOCUS_NEW_PATH:  textinput_unfocus(&pm->new_path);  break;
        case PM_FOCUS_NEW_W:     textinput_unfocus(&pm->new_w);     break;
        case PM_FOCUS_NEW_H:     textinput_unfocus(&pm->new_h);     break;
        case PM_FOCUS_OPEN_PATH: textinput_unfocus(&pm->open_path); break;
        default: break;
    }
    pm->focus = f;
    switch (f) {
        case PM_FOCUS_NEW_NAME:  textinput_focus(&pm->new_name);    break;
        case PM_FOCUS_NEW_PATH:  textinput_focus(&pm->new_path);    break;
        case PM_FOCUS_NEW_W:     textinput_focus(&pm->new_w);       break;
        case PM_FOCUS_NEW_H:     textinput_focus(&pm->new_h);       break;
        case PM_FOCUS_OPEN_PATH: textinput_focus(&pm->open_path);   break;
        default: break;
    }
}

/* ---- Path auto-fill ----------------------------------------------- */

static void sync_new_path(ProjectManager *pm) {
    char cwd[PROJECT_PATH_MAX];
    if (!getcwd(cwd, sizeof cwd)) cwd[0] = '\0';
    char full[PROJECT_PATH_MAX];
    const char *name = textinput_get(&pm->new_name);
    if (name[0])
        snprintf(full, sizeof full, "%s" PATH_SEP "%s", cwd, name);
    else
        snprintf(full, sizeof full, "%s", cwd);
    textinput_set(&pm->new_path, full);
}

/* ---- Actions ------------------------------------------------------ */

static bool do_create(ProjectManager *pm, Project *out) {
    const char *name = textinput_get(&pm->new_name);
    const char *path = textinput_get(&pm->new_path);
    const char *ws   = textinput_get(&pm->new_w);
    const char *hs   = textinput_get(&pm->new_h);
    if (!name[0]) { snprintf(pm->error_msg, sizeof pm->error_msg, "NAME CANNOT BE EMPTY"); return false; }
    if (!path[0]) { snprintf(pm->error_msg, sizeof pm->error_msg, "PATH CANNOT BE EMPTY"); return false; }
    int gw = ws[0] ? atoi(ws) : 64;
    int gh = hs[0] ? atoi(hs) : 64;
    if (gw < PROJECT_GRID_MIN || gw > PROJECT_GRID_MAX) {
        snprintf(pm->error_msg, sizeof pm->error_msg, "GRID W MUST BE %d-%d", PROJECT_GRID_MIN, PROJECT_GRID_MAX);
        return false;
    }
    if (gh < PROJECT_GRID_MIN || gh > PROJECT_GRID_MAX) {
        snprintf(pm->error_msg, sizeof pm->error_msg, "GRID H MUST BE %d-%d", PROJECT_GRID_MIN, PROJECT_GRID_MAX);
        return false;
    }
    if (dge_mkdir(path) != 0 && errno != EEXIST) {
        snprintf(pm->error_msg, sizeof pm->error_msg, "CANNOT CREATE FOLDER: %s", strerror(errno));
        return false;
    }
    project_defaults(out);
    strncpy(out->name, name, PROJECT_NAME_MAX - 1);
    strncpy(out->path, path, PROJECT_PATH_MAX - 1);
    out->grid_w = gw;  out->grid_h = gh;
    project_save(out);
    project_create_dirs(out);
    pm->error_msg[0] = '\0';
    return true;
}

static bool do_open(ProjectManager *pm, Project *out) {
    const char *path = textinput_get(&pm->open_path);
    if (!path[0])                  { snprintf(pm->error_msg, sizeof pm->error_msg, "PATH CANNOT BE EMPTY");          return false; }
    if (!project_folder_valid(path)){ snprintf(pm->error_msg, sizeof pm->error_msg, "NO PROJECT.DGE FOUND AT PATH"); return false; }
    if (!project_load(out, path))   { snprintf(pm->error_msg, sizeof pm->error_msg, "FAILED TO LOAD PROJECT");       return false; }
    pm->error_msg[0] = '\0';
    return true;
}

/* ---- Public API --------------------------------------------------- */

void project_manager_init(ProjectManager *pm) {
    memset(pm, 0, sizeof(*pm));
    pm->tab   = PM_TAB_NEW;
    pm->focus = PM_FOCUS_NONE;
    textinput_init(&pm->new_name,  48,  false);
    textinput_init(&pm->new_path,  PROJECT_PATH_MAX - 1, false);
    textinput_init(&pm->new_w,      3,  true);
    textinput_init(&pm->new_h,      3,  true);
    textinput_init(&pm->open_path, PROJECT_PATH_MAX - 1, false);
    textinput_set(&pm->new_w, "64");
    textinput_set(&pm->new_h, "64");
    char cwd[PROJECT_PATH_MAX];
    if (getcwd(cwd, sizeof cwd)) textinput_set(&pm->open_path, cwd);
    pm->recent_count = project_recent_load(pm->recent_paths);
    LOG_INFO("Project Manager: %d recent project(s).", pm->recent_count);
}

ProjectManagerResult project_manager_update(ProjectManager *pm,
                                             int vw, int vh,
                                             Project *out) {
    if (input_quit_requested()) return PM_RESULT_QUIT;

    Layout L = layout_compute(vw, vh);
    int mx, my;
    input_mouse_pos(&mx, &my);

    /* ---- Recent list ------------------------------------------ */
    float ry = L.content_top + text_line_height(SCALE_LABEL) + LINE_GAP;
    float limit_y = L.is_vertical ? L.div_y : L.content_bot;

    /* Handle [CLEAR ALL] button click */
    float clear_w = text_measure_width("CLEAR ALL", SCALE_SMALL) + 12.0f;
    float clear_x = L.pad + text_measure_width("RECENT PROJECTS", SCALE_LABEL) + 12.0f;
    if (clear_x + clear_w < L.left_w + L.pad) {
        if (hittest(clear_x, L.content_top, clear_w, BTN_H - 4.0f, mx, my) &&
            input_mouse_button_pressed(SDL_BUTTON_LEFT)) {
            project_recent_clear();
            pm->recent_count = project_recent_load(pm->recent_paths);
        }
    }

    for (int i = pm->recent_scroll;
         i < pm->recent_count && ry + L.recent_item_h < limit_y; i++) {
        if (pm->recent_paths[i][0] == '\0') continue;

        /* Buttons on card right side */
        float del_w = 40.0f;
        float del_x = L.pad + L.left_w - del_w - 6.0f;
        float del_y = ry + (L.recent_item_h - 20.0f) * 0.5f;
        float rem_w = 20.0f;
        float rem_x = del_x - rem_w - 4.0f;

        bool rem_clicked = hittest(rem_x, del_y, rem_w, 20.0f, mx, my) &&
                           input_mouse_button_pressed(SDL_BUTTON_LEFT);
        bool del_clicked = hittest(del_x, del_y, del_w, 20.0f, mx, my) &&
                           input_mouse_button_pressed(SDL_BUTTON_LEFT);

        if (rem_clicked) {
            project_recent_remove(pm->recent_paths[i]);
            pm->recent_count = project_recent_load(pm->recent_paths);
            break; /* restart loop */
        }
        if (del_clicked) {
            project_delete_from_disk(pm->recent_paths[i]);
            project_recent_remove(pm->recent_paths[i]);
            pm->recent_count = project_recent_load(pm->recent_paths);
            break; /* restart loop */
        }

        bool over_btn = hittest(rem_x, del_y, rem_w, 20.0f, mx, my) ||
                        hittest(del_x, del_y, del_w, 20.0f, mx, my);

        if (!over_btn && hittest(L.pad, ry, L.left_w, L.recent_item_h, mx, my)
            && input_mouse_button_pressed(SDL_BUTTON_LEFT)) {
            if (project_load(out, pm->recent_paths[i])) {
                pm->error_msg[0] = '\0';
                set_focus(pm, PM_FOCUS_NONE);
                return PM_RESULT_OPEN;
            }
            snprintf(pm->error_msg, sizeof pm->error_msg, "FAILED TO LOAD PROJECT");
        }
        ry += L.recent_item_h + ITEM_GAP;
    }

    /* ---- Tabs ------------------------------------------------- */
    float tab_new_x  = L.right_x;
    float tab_open_x = L.right_x + L.tab_w + ITEM_GAP;

    if (hittest(tab_new_x, L.content_top, L.tab_w, L.tab_h, mx, my)
        && input_mouse_button_pressed(SDL_BUTTON_LEFT)
        && pm->tab != PM_TAB_NEW) {
        pm->tab = PM_TAB_NEW;
        set_focus(pm, PM_FOCUS_NONE);
        pm->error_msg[0] = '\0';
    }
    if (hittest(tab_open_x, L.content_top, L.tab_w, L.tab_h, mx, my)
        && input_mouse_button_pressed(SDL_BUTTON_LEFT)
        && pm->tab != PM_TAB_OPEN) {
        pm->tab = PM_TAB_OPEN;
        set_focus(pm, PM_FOCUS_NONE);
        pm->error_msg[0] = '\0';
    }

    /* ---- Forms ------------------------------------------------ */
    float fw   = L.right_w;
    float fhalf = (fw - ITEM_GAP) * 0.5f;
    float fy   = L.form_y;

    if (pm->tab == PM_TAB_NEW) {
        bool prev_has_name = textinput_get(&pm->new_name)[0] != '\0';

        if (draw_field(L.right_x, fy, fw, "PROJECT NAME",
                       &pm->new_name, pm->focus == PM_FOCUS_NEW_NAME, mx, my))
            set_focus(pm, PM_FOCUS_NEW_NAME);
        fy += field_block_h();

        bool now_has_name = textinput_get(&pm->new_name)[0] != '\0';
        if (pm->focus == PM_FOCUS_NEW_NAME || now_has_name != prev_has_name)
            sync_new_path(pm);

        if (draw_field(L.right_x, fy, fw, "PROJECT FOLDER",
                       &pm->new_path, pm->focus == PM_FOCUS_NEW_PATH, mx, my))
            set_focus(pm, PM_FOCUS_NEW_PATH);
        fy += field_block_h();

        if (draw_field(L.right_x,               fy, fhalf, "GRID W",
                       &pm->new_w, pm->focus == PM_FOCUS_NEW_W, mx, my))
            set_focus(pm, PM_FOCUS_NEW_W);
        if (draw_field(L.right_x + fhalf + ITEM_GAP, fy, fhalf, "GRID H",
                       &pm->new_h, pm->focus == PM_FOCUS_NEW_H, mx, my))
            set_focus(pm, PM_FOCUS_NEW_H);
        fy += field_block_h() + LINE_GAP;

        /* Textinput update for focused field */
        float name_y  = L.form_y + text_line_height(SCALE_SMALL) + 2.0f;
        float path_y  = name_y  + FIELD_H + LINE_GAP + field_block_h() - FIELD_H - LINE_GAP
                        + text_line_height(SCALE_SMALL) + 2.0f;
        float grid_y  = path_y  + FIELD_H + LINE_GAP + text_line_height(SCALE_SMALL) + 2.0f;
        /* simpler: recompute inline */
        float f1y = L.form_y  + text_line_height(SCALE_SMALL) + 2.0f;
        float f2y = f1y + FIELD_H + LINE_GAP + text_line_height(SCALE_SMALL) + 2.0f;
        float f3y = f2y + FIELD_H + LINE_GAP + text_line_height(SCALE_SMALL) + 2.0f;
        (void)name_y; (void)path_y; (void)grid_y;

        switch (pm->focus) {
            case PM_FOCUS_NEW_NAME:
                if (textinput_update(&pm->new_name, L.right_x, f1y, fw, SCALE_BODY))
                    set_focus(pm, PM_FOCUS_NEW_PATH);
                break;
            case PM_FOCUS_NEW_PATH:
                textinput_update(&pm->new_path, L.right_x, f2y, fw, SCALE_BODY);
                break;
            case PM_FOCUS_NEW_W:
                textinput_update(&pm->new_w, L.right_x, f3y, fhalf, SCALE_BODY);
                break;
            case PM_FOCUS_NEW_H:
                textinput_update(&pm->new_h, L.right_x + fhalf + ITEM_GAP, f3y, fhalf, SCALE_BODY);
                break;
            default: break;
        }

        if (draw_button(L.right_x, fy, 130.0f, BTN_H, "CREATE", true, mx, my)) {
            set_focus(pm, PM_FOCUS_NONE);
            if (do_create(pm, out)) return PM_RESULT_OPEN;
        }

    } else {
        float f1y = L.form_y + text_line_height(SCALE_SMALL) + 2.0f;

        if (draw_field(L.right_x, fy, fw, "PROJECT FOLDER",
                       &pm->open_path, pm->focus == PM_FOCUS_OPEN_PATH, mx, my))
            set_focus(pm, PM_FOCUS_OPEN_PATH);
        fy += field_block_h() + LINE_GAP;

        if (pm->focus == PM_FOCUS_OPEN_PATH) {
            if (textinput_update(&pm->open_path, L.right_x, f1y, fw, SCALE_BODY)) {
                set_focus(pm, PM_FOCUS_NONE);
                if (do_open(pm, out)) return PM_RESULT_OPEN;
            }
        }

        if (draw_button(L.right_x, fy, 110.0f, BTN_H, "OPEN", true, mx, my)) {
            set_focus(pm, PM_FOCUS_NONE);
            if (do_open(pm, out)) return PM_RESULT_OPEN;
        }
    }

    /* ---- Tab cycling ------------------------------------------ */
    if (input_key_pressed(SDL_SCANCODE_TAB)) {
        if (pm->tab == PM_TAB_NEW) {
            switch (pm->focus) {
                case PM_FOCUS_NONE:
                case PM_FOCUS_NEW_H:    set_focus(pm, PM_FOCUS_NEW_NAME); break;
                case PM_FOCUS_NEW_NAME: set_focus(pm, PM_FOCUS_NEW_PATH); break;
                case PM_FOCUS_NEW_PATH: set_focus(pm, PM_FOCUS_NEW_W);    break;
                case PM_FOCUS_NEW_W:    set_focus(pm, PM_FOCUS_NEW_H);    break;
                default: break;
            }
        } else {
            set_focus(pm, PM_FOCUS_OPEN_PATH);
        }
    }
    if (input_key_pressed(SDL_SCANCODE_ESCAPE))
        set_focus(pm, PM_FOCUS_NONE);

    return PM_RESULT_NONE;
}

void project_manager_render(const ProjectManager *pm, int vw, int vh) {
    Layout L = layout_compute(vw, vh);
    int mx, my;
    input_mouse_pos(&mx, &my);

    /* Background */
    renderer_draw_quad(0, 0, (float)vw, (float)vh, 0.07f, 0.07f, 0.09f, 1.0f);

    {
        const char *title = "DGENGINE";
        float tw = text_measure_width(title, SCALE_TITLE);
        float th = text_line_height(SCALE_TITLE);
        float tx = ((float)vw - tw) * 0.5f;
        float ty = (L.content_top - th) * 0.5f;
        if (ty < L.pad) ty = L.pad;
        text_draw(tx, ty, SCALE_TITLE, 0.30f, 0.78f, 0.48f, 1.0f, title);
    }

    /* ---- Left panel ------------------------------------------- */
    {
        float ly = L.content_top;
        text_draw(L.pad, ly, SCALE_LABEL, 0.65f, 0.65f, 0.68f, 1.0f, "RECENT PROJECTS");

        /* Render [CLEAR ALL] button next to RECENT PROJECTS label */
        float clear_w = text_measure_width("CLEAR ALL", SCALE_SMALL) + 12.0f;
        float clear_x = L.pad + text_measure_width("RECENT PROJECTS", SCALE_LABEL) + 12.0f;
        if (clear_x + clear_w < L.left_w + L.pad) {
            bool hover = hittest(clear_x, ly - 2.0f, clear_w, BTN_H - 4.0f, mx, my);
            float bg = hover ? 0.22f : 0.14f;
            draw_box(clear_x, ly - 2.0f, clear_w, BTN_H - 4.0f, bg, bg, bg, 0.30f, 0.30f, 0.30f);
            float tw = text_measure_width("CLEAR ALL", SCALE_SMALL);
            float th = text_line_height(SCALE_SMALL);
            text_draw(clear_x + (clear_w - tw) * 0.5f, ly - 2.0f + (BTN_H - 4.0f - th) * 0.5f,
                      SCALE_SMALL, 0.8f, 0.4f, 0.4f, 1.0f, "CLEAR ALL");
        }

        ly += text_line_height(SCALE_LABEL) + LINE_GAP;

        float limit_y = L.is_vertical ? L.div_y : L.content_bot;

        if (pm->recent_count == 0) {
            text_draw(L.pad, ly, SCALE_BODY, 0.35f, 0.35f, 0.38f, 1.0f, "NO RECENT PROJECTS");
        } else {
            for (int i = pm->recent_scroll;
                 i < pm->recent_count && ly + L.recent_item_h < limit_y; i++) {
                if (pm->recent_paths[i][0] == '\0') continue;

                /* Buttons on card right side */
                float del_w = 40.0f;
                float del_x = L.pad + L.left_w - del_w - 6.0f;
                float del_y = ly + (L.recent_item_h - 20.0f) * 0.5f;
                float rem_w = 20.0f;
                float rem_x = del_x - rem_w - 4.0f;

                bool hover = hittest(L.pad, ly, L.left_w, L.recent_item_h, mx, my) &&
                             !hittest(rem_x, del_y, rem_w, 20.0f, mx, my) &&
                             !hittest(del_x, del_y, del_w, 20.0f, mx, my);

                float ibg = hover ? 0.14f : 0.10f;
                renderer_draw_quad(L.pad, ly, L.left_w, L.recent_item_h,
                                   ibg, ibg, ibg + 0.03f, 1.0f);
                renderer_draw_quad(L.pad, ly, L.left_w, 1.0f,
                                   0.22f, 0.22f, 0.28f, 1.0f);

                /* Draw DEL button */
                bool del_hover = hittest(del_x, del_y, del_w, 20.0f, mx, my);
                float dbg = del_hover ? 0.25f : 0.16f;
                draw_box(del_x, del_y, del_w, 20.0f, dbg, dbg * 0.5f, dbg * 0.5f,
                         0.40f, 0.20f, 0.20f);
                float dtw = text_measure_width("DEL", SCALE_SMALL);
                float dth = text_line_height(SCALE_SMALL);
                text_draw(del_x + (del_w - dtw) * 0.5f, del_y + (20.0f - dth) * 0.5f,
                          SCALE_SMALL, 0.90f, 0.50f, 0.50f, 1.0f, "DEL");

                /* Draw X (remove) button */
                bool rem_hover = hittest(rem_x, del_y, rem_w, 20.0f, mx, my);
                float rbg = rem_hover ? 0.22f : 0.14f;
                draw_box(rem_x, del_y, rem_w, 20.0f, rbg, rbg, rbg,
                         0.30f, 0.30f, 0.30f);
                float rtw = text_measure_width("X", SCALE_SMALL);
                text_draw(rem_x + (rem_w - rtw) * 0.5f, del_y + (20.0f - dth) * 0.5f,
                          SCALE_SMALL, 0.70f, 0.70f, 0.70f, 1.0f, "X");

                /* Folder name (last path component) large, full path small below */
                const char *name_start = pm->recent_paths[i];
                const char *slash = strrchr(pm->recent_paths[i], '/');
#ifdef _WIN32
                const char *bs = strrchr(pm->recent_paths[i], '\\');
                if (!slash || (bs && bs > slash)) slash = bs;
#endif
                if (slash && slash[1]) name_start = slash + 1;

                float nr = hover ? 1.0f : 0.88f;
                float ng = hover ? 1.0f : 0.88f;
                float nb = hover ? 1.0f : 0.88f;
                float item_text_y = ly + (L.recent_item_h - text_line_height(SCALE_BODY)
                                           - text_line_height(SCALE_SMALL) - 2.0f) * 0.5f;
                text_draw(L.pad + 8.0f, item_text_y,
                          SCALE_BODY, nr, ng, nb, 1.0f, name_start);
                
                /* Truncate path display to left panel width */
                char trunc_path[128];
                snprintf(trunc_path, sizeof trunc_path, "%s", pm->recent_paths[i]);
                float path_max_w = L.left_w - del_w - rem_w - 24.0f;
                while (strlen(trunc_path) > 3 && text_measure_width(trunc_path, SCALE_SMALL) > path_max_w) {
                    size_t len = strlen(trunc_path);
                    trunc_path[len-4] = '.';
                    trunc_path[len-3] = '.';
                    trunc_path[len-2] = '.';
                    trunc_path[len-1] = '\0';
                }

                text_draw(L.pad + 8.0f, item_text_y + text_line_height(SCALE_BODY) + 2.0f,
                          SCALE_SMALL, 0.42f, 0.42f, 0.48f, 1.0f,
                          trunc_path);

                ly += L.recent_item_h + ITEM_GAP;
            }
        }
    }

    /* ---- Divider ---------------------------------------------- */
    if (L.is_vertical) {
        renderer_draw_quad(L.pad, L.div_y, (float)vw - L.pad * 2.0f, 1.0f,
                           0.22f, 0.22f, 0.28f, 1.0f);
    } else {
        renderer_draw_quad(L.div_x, L.pad, 1.0f, (float)vh - L.pad * 2.0f,
                           0.22f, 0.22f, 0.28f, 1.0f);
    }

    /* ---- Right panel ------------------------------------------ */
    float tab_new_x  = L.right_x;
    float tab_open_x = L.right_x + L.tab_w + ITEM_GAP;
    bool  new_active  = (pm->tab == PM_TAB_NEW);
    bool  open_active = (pm->tab == PM_TAB_OPEN);

    /* NEW tab */
    {
        bool hov = hittest(tab_new_x, L.content_top, L.tab_w, L.tab_h, mx, my);
        float fr = new_active ? 0.16f : (hov ? 0.14f : 0.09f);
        float fg = new_active ? 0.52f : (hov ? 0.28f : 0.14f);
        float fb = new_active ? 0.28f : (hov ? 0.16f : 0.09f);
        draw_box(tab_new_x, L.content_top, L.tab_w, L.tab_h, fr, fg, fb,
                 new_active ? 0.32f : 0.22f,
                 new_active ? 0.78f : 0.28f,
                 new_active ? 0.42f : 0.22f);
        const char *lbl = "NEW PROJECT";
        float lw = text_measure_width(lbl, SCALE_SMALL);
        float lh = text_line_height(SCALE_SMALL);
        text_draw(tab_new_x + (L.tab_w - lw) * 0.5f,
                  L.content_top + (L.tab_h - lh) * 0.5f,
                  SCALE_SMALL,
                  new_active ? 1.0f : 0.60f,
                  new_active ? 1.0f : 0.60f,
                  new_active ? 1.0f : 0.60f, 1.0f, lbl);
    }

    /* OPEN tab */
    {
        bool hov = hittest(tab_open_x, L.content_top, L.tab_w, L.tab_h, mx, my);
        float fr = open_active ? 0.16f : (hov ? 0.14f : 0.09f);
        float fg = open_active ? 0.52f : (hov ? 0.28f : 0.14f);
        float fb = open_active ? 0.28f : (hov ? 0.16f : 0.09f);
        draw_box(tab_open_x, L.content_top, L.tab_w, L.tab_h, fr, fg, fb,
                 open_active ? 0.32f : 0.22f,
                 open_active ? 0.78f : 0.28f,
                 open_active ? 0.42f : 0.22f);
        const char *lbl = "OPEN PROJECT";
        float lw = text_measure_width(lbl, SCALE_SMALL);
        float lh = text_line_height(SCALE_SMALL);
        text_draw(tab_open_x + (L.tab_w - lw) * 0.5f,
                  L.content_top + (L.tab_h - lh) * 0.5f,
                  SCALE_SMALL,
                  open_active ? 1.0f : 0.60f,
                  open_active ? 1.0f : 0.60f,
                  open_active ? 1.0f : 0.60f, 1.0f, lbl);
    }

    /* ---- Form fields ------------------------------------------ */
    float fw    = L.right_w;
    float fhalf = (fw - ITEM_GAP) * 0.5f;
    float fy    = L.form_y;

    if (pm->tab == PM_TAB_NEW) {
        draw_field(L.right_x, fy, fw, "PROJECT NAME",
                   &pm->new_name, pm->focus == PM_FOCUS_NEW_NAME, mx, my);
        fy += field_block_h();

        draw_field(L.right_x, fy, fw, "PROJECT FOLDER",
                   &pm->new_path, pm->focus == PM_FOCUS_NEW_PATH, mx, my);
        fy += field_block_h();

        draw_field(L.right_x,               fy, fhalf, "GRID W",
                   &pm->new_w, pm->focus == PM_FOCUS_NEW_W, mx, my);
        draw_field(L.right_x + fhalf + ITEM_GAP, fy, fhalf, "GRID H",
                   &pm->new_h, pm->focus == PM_FOCUS_NEW_H, mx, my);
        fy += field_block_h() + LINE_GAP;

        draw_button(L.right_x, fy, 130.0f, BTN_H, "CREATE", true, mx, my);

    } else {
        draw_field(L.right_x, fy, fw, "PROJECT FOLDER",
                   &pm->open_path, pm->focus == PM_FOCUS_OPEN_PATH, mx, my);
        fy += field_block_h() + LINE_GAP;

        draw_button(L.right_x, fy, 110.0f, BTN_H, "OPEN", true, mx, my);
    }

    /* ---- Bottom strip: hint + error — stacked, always visible --- */
    {
        float hint_y = (float)vh - L.pad - text_line_height(SCALE_HINT);
        const char *hint = "TAB NEXT FIELD   ENTER CONFIRM   ESC CANCEL";
        float hw = text_measure_width(hint, SCALE_HINT);
        text_draw(((float)vw - hw) * 0.5f, hint_y,
                  SCALE_HINT, 0.32f, 0.32f, 0.36f, 1.0f, hint);

        if (pm->error_msg[0]) {
            float err_y = hint_y - text_line_height(SCALE_BODY) - 4.0f;
            float ew = text_measure_width(pm->error_msg, SCALE_BODY);
            text_draw(((float)vw - ew) * 0.5f, err_y,
                      SCALE_BODY, 1.0f, 0.32f, 0.28f, 1.0f, pm->error_msg);
        }
    }
}
