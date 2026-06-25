#ifndef DGE_TEXTINPUT_H
#define DGE_TEXTINPUT_H

#include <stdbool.h>

/*  Single-line text input widget — Phase H, the foundation everything
    after it (project names, object/sprite names, file paths, property
    values) types into instead of each screen reinventing its own
    ad-hoc key handling.

    Same philosophy as panel.c: immediate-mode, no widget tree, no
    registration. The caller owns a TextInput value and calls
    textinput_update() once per frame to feed it input, then
    textinput_render() once per frame to draw it — same two-call shape
    as panel_update()/panel_render(). It does not draw a background box
    or border; that's the caller's job, same as draw_button() in
    panel.c owns its own background separately from the label drawn
    inside it.

    Only one field is expected to be focused at a time. SDL's text-input
    mode (needed for SDL_TEXTINPUT events / on-screen keyboards) is a
    single global flag, not a per-widget one — textinput_focus()/
    textinput_unfocus() turn it on/off globally, so callers must
    unfocus the old field before focusing a new one. Nothing in this
    phase calls focus() yet (there's no screen to host a field in
    until the Project Manager in Phase I) — this is the widget itself,
    verified standalone. */

#define TEXTINPUT_MAX_LEN 127

typedef struct {
    char buf[TEXTINPUT_MAX_LEN + 1];  /* always NUL-terminated */
    int  len;                          /* cached strlen(buf) */
    int  cursor;                        /* byte index into buf, 0..len */
    bool focused;

    int  max_len;        /* effective cap for this field, 1..TEXTINPUT_MAX_LEN */
    bool numeric_only;    /* true rejects every non-digit character */

    /* dge_time_elapsed() at the moment focus was last gained. The
       blink phase is computed relative to this (not stored as its own
       on/off flag) so the caret always starts the instant a field
       gains focus, never mid-fade-out of whatever phase happened to
       be active. */
    double focus_time;

    /* Backspace/Delete/Left/Right key-repeat while held. One shared
       timer is enough: in practice at most one of these four is ever
       held at a time, and even if two were, sharing a timer just means
       the second one repeats in lockstep with the first instead of on
       its own independent clock — not worth a 4-slot array for. */
    int    repeat_scancode;  /* SDL_SCANCODE_UNKNOWN (0) when idle */
    double repeat_next_at;    /* dge_time_elapsed() value of the next repeat fire */
} TextInput;

/* max_len is clamped to [1, TEXTINPUT_MAX_LEN]. numeric_only restricts
   insertion to '0'-'9' — meant for grid-size fields later; nothing in
   this phase reads it yet. */
void textinput_init(TextInput *t, int max_len, bool numeric_only);

/* Replaces the buffer outright (e.g. pre-filling a rename field with
   the current name). Truncates to max_len if str is longer. Moves the
   cursor to the end of the new contents. */
void textinput_set(TextInput *t, const char *str);

const char *textinput_get(const TextInput *t);

/* Gives the field focus: enables SDL text-input mode, resets the caret
   blink to visible. Call when the user clicks into the field (or when
   a screen wants this field focused as soon as it opens). */
void textinput_focus(TextInput *t);

/* Drops focus: disables SDL text-input mode. Call on click-away,
   Enter, Escape, or right before focusing a different field. Safe to
   call on an already-unfocused field. */
void textinput_unfocus(TextInput *t);

/* Per-frame update — a no-op unless t->focused. Reads typed characters,
   Backspace/Delete/Left/Right (held-down repeat included)/Home/End,
   and a left-click inside the field to reposition the cursor.

   (x, y, scale) must match what textinput_render() will be called with
   this frame for this field — click-to-cursor needs it to map a click
   x-position back to a character index using the same monospace
   glyph-advance math render() draws with. `width` is the field's full
   clickable hit-box width in screen pixels, independent of how much
   text is actually in the buffer (an empty field is still clickable
   across its whole box).

   Returns true on the one frame Enter is pressed — the caller decides
   what that means (submit a name, confirm a rename...); the field
   does not auto-unfocus on Enter. */
bool textinput_update(TextInput *t, float x, float y, float width, float scale);

/* Draws the current buffer, plus a blinking caret while focused, with
   the existing monospace text renderer. Call between
   renderer_begin_ui() and renderer_end(). */
void textinput_render(const TextInput *t, float x, float y, float scale,
                       float r, float g, float b, float a);

#endif /* DGE_TEXTINPUT_H */
