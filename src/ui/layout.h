#ifndef DGE_UI_LAYOUT_H
#define DGE_UI_LAYOUT_H

#include <stdbool.h>
#include "textinput.h"

/* Simple immediate-mode layout cursor to prevent hardcoded Y-offsets */
typedef struct {
    float start_x;
    float width;
    
    float cursor_x;
    float cursor_y;
    float row_height; /* Tracks max height in a row for when we advance Y */
} UILayout;

void ui_layout_begin(UILayout *l, float x, float y, float w);

/* Advance the cursor manually if needed */
void ui_layout_gap(UILayout *l, float gap);

/* Draws a label and advances Y. */
void ui_layout_label(UILayout *l, const char *text, bool small_font);

/* Draws a button and advances Y (if w is 0, uses full width). Returns true if clicked. */
bool ui_layout_button(UILayout *l, const char *label, float w, float h, bool active, int mx, int my);

/* Draws a text input field box and the textinput itself.
   Advances Y.
   w = 0 uses full width.
   Returns true if the field was clicked (caller should focus it).
   *out_enter_pressed is set to true if enter was pressed while focused. */
bool ui_layout_field(UILayout *l, TextInput *ti, float w, float h, bool focused, int mx, int my, bool *out_enter_pressed);

/* For side-by-side elements, we can suppress the automatic Y advance, 
   manually step X, and then manually advance Y after the row is done. */
void ui_layout_row_begin(UILayout *l);
void ui_layout_row_step_x(UILayout *l, float x_offset);
void ui_layout_row_end(UILayout *l);

#endif /* DGE_UI_LAYOUT_H */
