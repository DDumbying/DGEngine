#include "inspector_pane.h"
#include <stdio.h>
#include <SDL2/SDL.h>
#include "../platform/input.h"
#include "../renderer/renderer.h"
#include "text.h"
#include "theme.h"

#define SECTION_GAP 8
#define FIELD_H     20
#define HEADER_H    36

void inspector_pane_init(InspectorPane *ip) {
    ip->is_collapsed = false;
    ip->scroll_y = 0;
    ip->editing_field = 0;
    ip->editing_entity = ENTITY_NULL;
    textinput_init(&ip->input_field, 10, false); /* 10 chars max, allow negative/floats so numeric_only=false */
}

bool inspector_pane_update(InspectorPane *ip, Editor *ed, Registry *reg,
                           Entity selected_entity, int viewport_w, int viewport_h) {
    (void)ed;
    int px, py;
    input_mouse_pos(&px, &py);

    int pane_w = ip->is_collapsed ? 24 : INSPECTOR_PANE_WIDTH;
    int start_x = viewport_w - pane_w;
    bool hovering = (px >= start_x && px < viewport_w && py >= 34 && py < viewport_h);
    bool clicked = input_mouse_button_pressed(SDL_BUTTON_LEFT);

    /* Collapse toggle */
    if (clicked && px >= start_x && px < viewport_w && py >= 34 && py < 34 + HEADER_H) {
        ip->is_collapsed = !ip->is_collapsed;
        return hovering;
    }

    if (ip->is_collapsed) return hovering;

    if (hovering) {
        int sx, sy;
        input_mouse_scroll(&sx, &sy);
        (void)sx;
        if (sy) {
            ip->scroll_y -= sy * 3;
            if (ip->scroll_y < 0) ip->scroll_y = 0;
        }
    }

    if (ip->editing_field != 0 && ip->editing_entity == selected_entity) {
        /* We just pass dummy y to textinput_update since we only need it for click-to-cursor,
           and it relies on the Y we pass to be accurate if we want multi-line, but this is single line */
        bool enter = textinput_update(&ip->input_field, (float)(start_x + 90), 0.0f, 100.0f, 1.0f);
        if (enter) {
            float val = (float)atof(textinput_get(&ip->input_field));
            if (ip->editing_field == 1) reg->transform[selected_entity].x = val;
            else if (ip->editing_field == 2) reg->transform[selected_entity].y = val;
            textinput_unfocus(&ip->input_field);
            ip->editing_field = 0;
        } else if (clicked && hovering) {
            /* If they clicked somewhere else in the inspector, we'll let the standard click handling below catch it or just unfocus */
        }
    }

    if (hovering && clicked && selected_entity != ENTITY_NULL && entity_alive(reg, selected_entity)) {
        float pane_y = 34;
        float y = pane_y + HEADER_H + 8 - ip->scroll_y;
        y += 28.0f;
        y += SECTION_GAP;
        
        bool click_handled = false;

        if (reg->has_transform[selected_entity]) {
            y += 26.0f; /* draw_section height */
            
            /* X field */
            if (py >= y && py < y + FIELD_H && px >= start_x + 90) {
                ip->editing_field = 1;
                ip->editing_entity = selected_entity;
                char buf[32]; snprintf(buf, sizeof(buf), "%.1f", reg->transform[selected_entity].x);
                textinput_set(&ip->input_field, buf);
                textinput_focus(&ip->input_field);
                click_handled = true;
            }
            y += FIELD_H;
            
            /* Y field */
            if (!click_handled && py >= y && py < y + FIELD_H && px >= start_x + 90) {
                ip->editing_field = 2;
                ip->editing_entity = selected_entity;
                char buf[32]; snprintf(buf, sizeof(buf), "%.1f", reg->transform[selected_entity].y);
                textinput_set(&ip->input_field, buf);
                textinput_focus(&ip->input_field);
                click_handled = true;
            }
            y += FIELD_H;
            y += SECTION_GAP;
        }

        if (!click_handled && ip->editing_field != 0) {
            textinput_unfocus(&ip->input_field);
            ip->editing_field = 0;
        }
    }

    /* If we unselected the entity or it died, stop editing */
    if (ip->editing_field != 0 && (selected_entity == ENTITY_NULL || !entity_alive(reg, selected_entity) || ip->editing_entity != selected_entity)) {
        textinput_unfocus(&ip->input_field);
        ip->editing_field = 0;
    }

    return hovering;
}

/* Draws a section header (component name) with accent underline */
static float draw_section(float x, float y, float w, const Theme *th, const char *label) {
    text_draw(x, y, 1.2f, th->accent_r, th->accent_g, th->accent_b, 1.0f, label);
    y += 20.0f;
    renderer_draw_quad((int)x, (int)y, (int)(w - 20), 1,
                       th->border_r, th->border_g, th->border_b, 0.6f);
    return y + 6.0f;
}

/* Draws a key=value field row */
static float draw_field(float x, float y, const Theme *th, const char *key, const char *value) {
    text_draw(x, y, 1.0f, th->text_dim_r, th->text_dim_g, th->text_dim_b, 1.0f, key);
    text_draw(x + 80, y, 1.0f, th->text_r, th->text_g, th->text_b, 1.0f, value);
    return y + FIELD_H;
}

void inspector_pane_render(const InspectorPane *ip, const Editor *ed, const Registry *reg,
                           Entity selected_entity, int viewport_w, int viewport_h) {
    (void)ed;
    const Theme *th = theme_current();

    int pane_w = ip->is_collapsed ? 24 : INSPECTOR_PANE_WIDTH;
    int sx = viewport_w - pane_w;
    int pane_y = 34;
    int pane_h = viewport_h - pane_y;

    /* Background */
    renderer_draw_quad(sx, pane_y, pane_w, pane_h,
                       th->panel_bg_r, th->panel_bg_g, th->panel_bg_b, 1.0f);

    /* Left border */
    renderer_draw_quad(sx, pane_y, 1, pane_h,
                       th->border_r, th->border_g, th->border_b, 1.0f);

    /* Header strip */
    renderer_draw_quad(sx, pane_y, pane_w, HEADER_H,
                       th->bg_r, th->bg_g, th->bg_b, 1.0f);
                       
    text_draw(sx + 4, pane_y + 10, 1.2f, th->text_r, th->text_g, th->text_b, 1.0f, ip->is_collapsed ? "<<" : ">>");
    
    if (ip->is_collapsed) return;

    text_draw(sx + 24, pane_y + 10, 1.4f,
              th->accent_r, th->accent_g, th->accent_b, 1.0f,
              "Inspector");

    float y = (float)(pane_y + HEADER_H + 8);
    float x = (float)(sx + 10);
    float w = (float)INSPECTOR_PANE_WIDTH;
    char buf[128];

    if (selected_entity == ENTITY_NULL || !entity_alive(reg, selected_entity)) {
        text_draw(x, y, 1.0f,
                  th->text_dim_r, th->text_dim_g, th->text_dim_b, 1.0f,
                  "Select an entity to inspect.");
        return;
    }

    /* Entity ID header */
    if (reg->has_definition[selected_entity]) {
        snprintf(buf, sizeof(buf), "%s  (id: %u)", reg->definition[selected_entity].def_name, selected_entity);
    } else {
        snprintf(buf, sizeof(buf), "Entity %u", selected_entity);
    }
    text_draw(x, y, 1.3f, th->text_r, th->text_g, th->text_b, 1.0f, buf);
    y += 28.0f;
    renderer_draw_quad(sx + 6, (int)y, INSPECTOR_PANE_WIDTH - 12, 1,
                       th->border_r, th->border_g, th->border_b, 0.4f);
    y += SECTION_GAP;

    /* --- Transform --- */
    if (reg->has_transform[selected_entity]) {
        y = draw_section(x, y, w, th, "Transform");
        
        if (ip->editing_field == 1 && ip->editing_entity == selected_entity) {
            text_draw(x + 6, y, 1.0f, th->text_dim_r, th->text_dim_g, th->text_dim_b, 1.0f, "X");
            renderer_draw_quad((int)(x + 86), (int)y, 100, 18, th->bg_r, th->bg_g, th->bg_b, 1.0f);
            textinput_render(&ip->input_field, x + 86, y, 1.0f, th->text_r, th->text_g, th->text_b, 1.0f);
            y += FIELD_H;
        } else {
            snprintf(buf, sizeof(buf), "%.1f", reg->transform[selected_entity].x);
            y = draw_field(x + 6, y, th, "X", buf);
        }
        
        if (ip->editing_field == 2 && ip->editing_entity == selected_entity) {
            text_draw(x + 6, y, 1.0f, th->text_dim_r, th->text_dim_g, th->text_dim_b, 1.0f, "Y");
            renderer_draw_quad((int)(x + 86), (int)y, 100, 18, th->bg_r, th->bg_g, th->bg_b, 1.0f);
            textinput_render(&ip->input_field, x + 86, y, 1.0f, th->text_r, th->text_g, th->text_b, 1.0f);
            y += FIELD_H;
        } else {
            snprintf(buf, sizeof(buf), "%.1f", reg->transform[selected_entity].y);
            y = draw_field(x + 6, y, th, "Y", buf);
        }
        y += SECTION_GAP;
    }

    /* --- Renderable --- */
    if (reg->has_renderable[selected_entity]) {
        y = draw_section(x, y, w, th, "Renderable");
        snprintf(buf, sizeof(buf), "%d", reg->renderable[selected_entity].sprite_id);
        y = draw_field(x + 6, y, th, "Sprite", buf);
        snprintf(buf, sizeof(buf), "%.0fx%.0f",
                 reg->renderable[selected_entity].w, reg->renderable[selected_entity].h);
        y = draw_field(x + 6, y, th, "Size", buf);
        if (reg->renderable[selected_entity].frame_count > 1) {
            snprintf(buf, sizeof(buf), "%d @ %.1f fps",
                     reg->renderable[selected_entity].frame_count,
                     reg->renderable[selected_entity].frame_fps);
            y = draw_field(x + 6, y, th, "Anim", buf);
        }
        y += SECTION_GAP;
    }

    /* --- Health --- */
    if (reg->has_health[selected_entity]) {
        y = draw_section(x, y, w, th, "Health");
        snprintf(buf, sizeof(buf), "%d / %d",
                 reg->health[selected_entity].current,
                 reg->health[selected_entity].max);
        y = draw_field(x + 6, y, th, "HP", buf);
        y += SECTION_GAP;
    }

    /* --- Resource --- */
    if (reg->has_resource[selected_entity]) {
        y = draw_section(x, y, w, th, "Resource");
        y = draw_field(x + 6, y, th, "Kind", reg->resource[selected_entity].kind);
        snprintf(buf, sizeof(buf), "%d", reg->resource[selected_entity].yield_per_hit);
        y = draw_field(x + 6, y, th, "Yield", buf);
        y += SECTION_GAP;
    }

    /* --- Move --- */
    if (reg->has_move[selected_entity]) {
        y = draw_section(x, y, w, th, "Movement");
        snprintf(buf, sizeof(buf), "%.1f tiles/s", reg->move[selected_entity].speed);
        y = draw_field(x + 6, y, th, "Speed", buf);
        y = draw_field(x + 6, y, th, "Moving",
                       reg->move[selected_entity].moving ? "Yes" : "No");
        y += SECTION_GAP;
    }

    /* --- Task --- */
    if (reg->has_task[selected_entity]) {
        y = draw_section(x, y, w, th, "Task");
        const char *kind_str = "Idle";
        switch (reg->task[selected_entity].kind) {
            case TASK_MOVE_TO: kind_str = "Move To"; break;
            case TASK_HARVEST: kind_str = "Harvest"; break;
            case TASK_BUILD:   kind_str = "Build";   break;
            default: break;
        }
        y = draw_field(x + 6, y, th, "Kind", kind_str);
        if (reg->task[selected_entity].kind != TASK_IDLE) {
            snprintf(buf, sizeof(buf), "(%d, %d)",
                     reg->task[selected_entity].target_x,
                     reg->task[selected_entity].target_y);
            y = draw_field(x + 6, y, th, "Target", buf);
        }
        y += SECTION_GAP;
    }

    /* --- Construction --- */
    if (reg->has_construction[selected_entity]) {
        y = draw_section(x, y, w, th, "Construction");
        y = draw_field(x + 6, y, th, "Def", reg->construction[selected_entity].def_name);
        snprintf(buf, sizeof(buf), "%.1f / %.1f",
                 reg->construction[selected_entity].build_time_done,
                 reg->construction[selected_entity].build_time_total);
        y = draw_field(x + 6, y, th, "Progress", buf);
        y = draw_field(x + 6, y, th, "Complete",
                       reg->construction[selected_entity].complete ? "Yes" : "No");
        y += SECTION_GAP;
    }

    /* --- Definition --- */
    if (reg->has_definition[selected_entity]) {
        y = draw_section(x, y, w, th, "Definition");
        y = draw_field(x + 6, y, th, "Name", reg->definition[selected_entity].def_name);
        y += SECTION_GAP;
    }

    /* --- Level Transition --- */
    if (reg->has_level_transition[selected_entity]) {
        y = draw_section(x, y, w, th, "Level Transition");
        y = draw_field(x + 6, y, th, "Level", reg->level_transition[selected_entity].target_level);
        y = draw_field(x + 6, y, th, "Marker", reg->level_transition[selected_entity].target_marker);
        y += SECTION_GAP;
    }

    (void)y;
}
