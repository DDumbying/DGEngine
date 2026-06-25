#include "textinput.h"

#include <ctype.h>
#include <math.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "font.h"
#include "text.h"
#include "../platform/input.h"
#include "../core/time.h"
#include "../renderer/renderer.h"

/* Caret blink: on for BLINK_PERIOD seconds, off for BLINK_PERIOD
   seconds, repeat — ~1.06s full cycle, a fairly standard terminal-caret
   cadence. */
#define BLINK_PERIOD 0.53

/* Key-repeat timing for Backspace/Delete/Left/Right while held. */
#define REPEAT_INITIAL_DELAY 0.40
#define REPEAT_INTERVAL      0.04

/* ---------------------------------------------------------------------
   Pure buffer/cursor operations. All of these assume cursor/len are
   already valid (0 <= cursor <= len <= max_len) and leave them that
   way — textinput_init()/_set() establish the invariant, nothing below
   can break it. */

static bool char_allowed(const TextInput *t, char c) {
    if (t->numeric_only) return isdigit((unsigned char)c) != 0;
    /* Only accept what the bitmap font can actually draw — typing a
       character that silently renders as a gap (see font.c's comment
       on why '_' needed adding for Phase H) is worse than refusing it
       outright. font_get_glyph() upper-cases internally for lookup, so
       lowercase letters pass this check and are *stored* as typed
       (case preserved in the buffer) even though they currently render
       uppercase — text.c's display limitation, not a reason to lose
       the real case from data that might end up as a filename. */
    return font_get_glyph(c) != NULL;
}

static void insert_char(TextInput *t, char c) {
    if (t->len >= t->max_len) return;
    if (!char_allowed(t, c)) return;
    /* Shift [cursor..len] (len-cursor+1 bytes, including the NUL)
       right by one to open a slot at cursor. */
    memmove(t->buf + t->cursor + 1, t->buf + t->cursor,
            (size_t)(t->len - t->cursor) + 1);
    t->buf[t->cursor] = c;
    t->len++;
    t->cursor++;
}

static void do_backspace(TextInput *t) {
    if (t->cursor == 0) return;
    memmove(t->buf + t->cursor - 1, t->buf + t->cursor,
            (size_t)(t->len - t->cursor) + 1);
    t->len--;
    t->cursor--;
}

static void do_delete_forward(TextInput *t) {
    if (t->cursor == t->len) return;
    memmove(t->buf + t->cursor, t->buf + t->cursor + 1,
            (size_t)(t->len - t->cursor));
    t->len--;
}

static void move_left(TextInput *t)  { if (t->cursor > 0)        t->cursor--; }
static void move_right(TextInput *t) { if (t->cursor < t->len)   t->cursor++; }
static void move_home(TextInput *t)  { t->cursor = 0; }
static void move_end(TextInput *t)   { t->cursor = t->len; }

/* ---------------------------------------------------------------------
   Held-key repeat. Returns true the frame `sc` should "fire" — either
   the initial press, or a repeat tick once it's been held past the
   initial delay, then every REPEAT_INTERVAL after that. Shares one
   timer across all four repeatable keys (see the struct comment in
   textinput.h for why that's an acceptable tradeoff). */
static bool key_fired_with_repeat(TextInput *t, SDL_Scancode sc, double now) {
    if (input_key_pressed(sc)) {
        t->repeat_scancode = (int)sc;
        t->repeat_next_at  = now + REPEAT_INITIAL_DELAY;
        return true;
    }
    if (t->repeat_scancode == (int)sc) {
        if (!input_key_down(sc)) {
            t->repeat_scancode = SDL_SCANCODE_UNKNOWN;
            return false;
        }
        if (now >= t->repeat_next_at) {
            t->repeat_next_at = now + REPEAT_INTERVAL;
            return true;
        }
    }
    return false;
}

/* Pixel width of exactly one character at this scale. text_measure_width()
   is plain strlen()*per-char-width, so any single character (its glyph
   doesn't even need to exist) gives the fixed monospace advance without
   reaching into text.c's private GLYPH_ADVANCE_CELLS constant. */
static float glyph_advance(float scale) {
    return text_measure_width("M", scale);
}

/* ---------------------------------------------------------------------
   Public API */

void textinput_init(TextInput *t, int max_len, bool numeric_only) {
    memset(t, 0, sizeof(*t));
    t->max_len = (max_len > 0 && max_len <= TEXTINPUT_MAX_LEN)
                 ? max_len : TEXTINPUT_MAX_LEN;
    t->numeric_only    = numeric_only;
    t->repeat_scancode = SDL_SCANCODE_UNKNOWN;
}

void textinput_set(TextInput *t, const char *str) {
    size_t n = strlen(str);
    if (n > (size_t)t->max_len) n = (size_t)t->max_len;
    memcpy(t->buf, str, n);
    t->buf[n] = '\0';
    t->len    = (int)n;
    t->cursor = t->len;
}

const char *textinput_get(const TextInput *t) { return t->buf; }

void textinput_focus(TextInput *t) {
    t->focused          = true;
    t->focus_time        = dge_time_elapsed();
    t->repeat_scancode   = SDL_SCANCODE_UNKNOWN;
    input_text_input_activate();
}

void textinput_unfocus(TextInput *t) {
    if (!t->focused) return;
    t->focused = false;
    input_text_input_deactivate();
}

bool textinput_update(TextInput *t, float x, float y, float width, float scale) {
    if (!t->focused) return false;

    double now = dge_time_elapsed();
    bool enter_pressed = false;

    /* Typed characters this frame — filtering (numeric-only / no
       glyph) happens inside insert_char()/char_allowed(). */
    for (const char *p = input_text_typed(); *p; p++)
        insert_char(t, *p);

    if (key_fired_with_repeat(t, SDL_SCANCODE_BACKSPACE, now)) do_backspace(t);
    if (key_fired_with_repeat(t, SDL_SCANCODE_DELETE,    now)) do_delete_forward(t);
    if (key_fired_with_repeat(t, SDL_SCANCODE_LEFT,      now)) move_left(t);
    if (key_fired_with_repeat(t, SDL_SCANCODE_RIGHT,     now)) move_right(t);

    if (input_key_pressed(SDL_SCANCODE_HOME)) move_home(t);
    if (input_key_pressed(SDL_SCANCODE_END))  move_end(t);
    if (input_key_pressed(SDL_SCANCODE_RETURN) ||
        input_key_pressed(SDL_SCANCODE_KP_ENTER))
        enter_pressed = true;

    /* Click inside the field repositions the caret. This does NOT
       grant focus on click outside it, nor drop focus on click
       outside — focus is the caller's job (it owns the screen layout
       and knows which field, if any, a given click should focus); this
       only repositions the caret within a field that's already
       focused. */
    if (input_mouse_button_pressed(SDL_BUTTON_LEFT)) {
        int mx, my;
        input_mouse_pos(&mx, &my);
        float line_h = text_line_height(scale);
        if ((float)mx >= x && (float)mx < x + width &&
            (float)my >= y && (float)my < y + line_h) {
            float advance = glyph_advance(scale);
            int idx = (int)(((float)mx - x + advance * 0.5f) / advance);
            if (idx < 0)       idx = 0;
            if (idx > t->len)  idx = t->len;
            t->cursor = idx;
        }
    }

    return enter_pressed;
}

void textinput_render(const TextInput *t, float x, float y, float scale,
                       float r, float g, float b, float a) {
    text_draw(x, y, scale, r, g, b, a, t->buf);

    if (!t->focused) return;

    /* Caret phase is relative to focus_time, not wall-clock time
       modulo period — see the struct comment for why. */
    double phase = fmod(dge_time_elapsed() - t->focus_time, BLINK_PERIOD * 2.0);
    if (phase >= BLINK_PERIOD) return; /* off half of the cycle */

    float caret_x = x + (float)t->cursor * glyph_advance(scale);
    float caret_h = text_line_height(scale);
    renderer_draw_quad(caret_x, y, scale * 0.6f, caret_h, r, g, b, a);
}
