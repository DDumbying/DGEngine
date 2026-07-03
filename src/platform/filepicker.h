#ifndef DGE_FILEPICKER_H
#define DGE_FILEPICKER_H

#include <stdbool.h>

/*  Roadmap P5#17 — native file dialogs instead of a bare text field.

    filepicker_open_image() blocks the calling thread until the user
    picks a file or cancels (or the platform tool isn't available),
    which is fine here: it's only ever called from a single button
    click in sprites_tab.c, and a native file dialog is itself modal
    on every platform anyway, so there's no responsiveness cost over
    what the OS dialog already imposes.

    Backends, in the order they're tried:
      Linux   - zenity --file-selection, falling back to kdialog
                --getopenfilename if zenity isn't installed
      Windows - GetOpenFileNameW (comdlg32)
      macOS   - osascript "choose file" (AppleScript), since macOS
                ships osascript on every install with no extra deps

    Returns true and fills out_path on success. Returns false (leaving
    out_path untouched) on cancel, on any I/O failure, or when no
    backend is available -- callers should keep the existing text
    field as a fallback rather than assuming this always works. */
bool filepicker_open_image(char *out_path, int out_path_size);

#endif /* DGE_FILEPICKER_H */
