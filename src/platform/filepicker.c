#include "filepicker.h"

#if !defined(_WIN32)
/* -std=c11 alone hides popen/pclose (they're POSIX, not C11) --
   needs to come before the first system header include, which is
   why it's here rather than in filepicker.h. */
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)

#include <windows.h>
#include <commdlg.h>

bool filepicker_open_image(char *out_path, int out_path_size) {
    wchar_t wpath[1024] = L"";

    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize  = sizeof ofn;
    ofn.lpstrFilter  = L"Images (*.png;*.jpg;*.jpeg;*.bmp)\0*.png;*.jpg;*.jpeg;*.bmp\0All files\0*.*\0";
    ofn.lpstrFile    = wpath;
    ofn.nMaxFile     = (DWORD)(sizeof(wpath) / sizeof(wpath[0]));
    ofn.Flags        = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrTitle   = L"Choose a sprite image";

    if (!GetOpenFileNameW(&ofn)) return false;

    int n = WideCharToMultiByte(CP_UTF8, 0, wpath, -1, out_path, out_path_size, NULL, NULL);
    return n > 0;
}

#else /* POSIX: Linux (zenity/kdialog) and macOS (osascript) */

#include <stdlib.h>

/* Runs `cmd`, reads the first line of stdout into out_path (trimming
   the trailing newline). Returns false on popen failure, a non-zero
   exit status, or empty output (cancel looks the same as "tool not
   installed and the shell printed nothing" from here, which is fine
   -- both mean "nothing to import", and the text field stays as the
   fallback either way). */
static bool run_picker_command(const char *cmd, char *out_path, int out_path_size) {
    FILE *p = popen(cmd, "r");
    if (!p) return false;

    bool got_line = (fgets(out_path, out_path_size, p) != NULL);
    int status = pclose(p);

    if (!got_line || status != 0) return false;

    size_t n = strlen(out_path);
    while (n > 0 && (out_path[n-1] == '\n' || out_path[n-1] == '\r')) out_path[--n] = '\0';

    return n > 0;
}

/* command -v exits 0 iff the named tool is on PATH -- used so we don't
   show a confusing empty result when a tool genuinely isn't installed
   rather than the user just cancelling its dialog. */
static bool tool_available(const char *name) {
    char cmd[64];
    snprintf(cmd, sizeof cmd, "command -v %s >/dev/null 2>&1", name);
    return system(cmd) == 0;
}

bool filepicker_open_image(char *out_path, int out_path_size) {
#if defined(__APPLE__)
    const char *cmd =
        "osascript -e 'POSIX path of (choose file with prompt "
        "\"Choose a sprite image\" of type {\"png\",\"jpg\",\"jpeg\",\"bmp\"})' 2>/dev/null";
    return run_picker_command(cmd, out_path, out_path_size);
#else
    if (tool_available("zenity")) {
        const char *cmd =
            "zenity --file-selection --title='Choose a sprite image' "
            "--file-filter='Images | *.png *.jpg *.jpeg *.bmp' 2>/dev/null";
        if (run_picker_command(cmd, out_path, out_path_size)) return true;
        return false; /* zenity ran but user cancelled -- don't fall through to kdialog */
    }
    if (tool_available("kdialog")) {
        const char *cmd =
            "kdialog --getopenfilename . 'Images (*.png *.jpg *.jpeg *.bmp)' 2>/dev/null";
        return run_picker_command(cmd, out_path, out_path_size);
    }
    return false; /* no supported dialog tool installed */
#endif
}

#endif
