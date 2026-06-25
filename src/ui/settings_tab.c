#include "settings_tab.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "../platform/input.h"
#include "../renderer/renderer.h"
#include "text.h"
#include "tabbar.h"

#define PAD       20.0f
#define FIELD_H   24.0f
#define BTN_H     26.0f
#define GAP        8.0f
#define COL_W    320.0f
#define SCALE_LBL 1.5f
#define SCALE_SM  1.3f
#define CONTENT_Y ((float)TABBAR_H)

static bool hittest(float x,float y,float w,float h,int mx,int my){
    return (float)mx>=x&&(float)mx<x+w&&(float)my>=y&&(float)my<y+h;
}
static void draw_border(float x,float y,float w,float h,float r,float g,float b){
    renderer_draw_quad(x,y,w,1,r,g,b,1); renderer_draw_quad(x,y+h-1,w,1,r,g,b,1);
    renderer_draw_quad(x,y,1,h,r,g,b,1); renderer_draw_quad(x+w-1,y,1,h,r,g,b,1);
}
static bool draw_btn(float x,float y,float w,float h,const char*l,int mx,int my){
    bool hov=hittest(x,y,w,h,mx,my);
    bool clk=hov&&input_mouse_button_pressed(SDL_BUTTON_LEFT);
    float bg=hov?0.22f:0.13f, gg=hov?0.60f:0.45f;
    renderer_draw_quad(x,y,w,h,bg,gg,bg+0.05f,1);
    draw_border(x,y,w,h,0.30f,0.78f,0.45f);
    float tw=text_measure_width(l,SCALE_LBL),th=text_line_height(SCALE_LBL);
    text_draw(x+(w-tw)*.5f,y+(h-th)*.5f,SCALE_LBL,1,1,1,1,l);
    return clk;
}
static void draw_field(float x,float y,float w,const char*label,const TextInput*fi,bool foc,int mx,int my){
    (void)mx;(void)my;
    text_draw(x,y,SCALE_SM,0.52f,0.52f,0.55f,1,label);
    float fy=y+text_line_height(SCALE_SM)+2;
    float bg=foc?0.17f:0.10f;
    renderer_draw_quad(x,fy,w,FIELD_H,bg,bg,bg,1);
    draw_border(x,fy,w,FIELD_H,foc?0.30f:0.22f,foc?0.72f:0.28f,foc?0.42f:0.22f);
    float th=text_line_height(SCALE_LBL);
    textinput_render(fi,x+4,fy+(FIELD_H-th)*.5f,SCALE_LBL,1,1,1,1);
}
static float field_h(void){return text_line_height(SCALE_SM)+2+FIELD_H+GAP;}

static void do_focus(SettingsTab*st,SettingsFocus f){
    /* unfocus old */
    switch(st->focus){
        case SETTINGS_FOCUS_NAME:   textinput_unfocus(&st->fi_name);   break;
        case SETTINGS_FOCUS_GRID_W: textinput_unfocus(&st->fi_grid_w); break;
        case SETTINGS_FOCUS_GRID_H: textinput_unfocus(&st->fi_grid_h); break;
        case SETTINGS_FOCUS_TILE_W: textinput_unfocus(&st->fi_tile_w); break;
        case SETTINGS_FOCUS_TILE_H: textinput_unfocus(&st->fi_tile_h); break;
        default: break;
    }
    st->focus=f;
    switch(f){
        case SETTINGS_FOCUS_NAME:   textinput_focus(&st->fi_name);   break;
        case SETTINGS_FOCUS_GRID_W: textinput_focus(&st->fi_grid_w); break;
        case SETTINGS_FOCUS_GRID_H: textinput_focus(&st->fi_grid_h); break;
        case SETTINGS_FOCUS_TILE_W: textinput_focus(&st->fi_tile_w); break;
        case SETTINGS_FOCUS_TILE_H: textinput_focus(&st->fi_tile_h); break;
        default: break;
    }
}

void settings_tab_init(SettingsTab*st,const Project*proj){
    memset(st,0,sizeof(*st));
    textinput_init(&st->fi_name,  PROJECT_NAME_MAX-1,false);
    textinput_init(&st->fi_grid_w,3,true);
    textinput_init(&st->fi_grid_h,3,true);
    textinput_init(&st->fi_tile_w,3,true);
    textinput_init(&st->fi_tile_h,3,true);
    char buf[16];
    textinput_set(&st->fi_name,proj->name);
    snprintf(buf,sizeof buf,"%d",proj->grid_w); textinput_set(&st->fi_grid_w,buf);
    snprintf(buf,sizeof buf,"%d",proj->grid_h); textinput_set(&st->fi_grid_h,buf);
    snprintf(buf,sizeof buf,"%d",proj->tile_w); textinput_set(&st->fi_tile_w,buf);
    snprintf(buf,sizeof buf,"%d",proj->tile_h); textinput_set(&st->fi_tile_h,buf);
    st->pending_grid_w=proj->grid_w; st->pending_grid_h=proj->grid_h;
    st->pending_tile_w=proj->tile_w; st->pending_tile_h=proj->tile_h;
}

bool settings_tab_update(SettingsTab*st,Project*proj,int vw,int vh){
    (void)vw;(void)vh;
    int mx,my; input_mouse_pos(&mx,&my);
    bool saved=false;
    float x=PAD, y=CONTENT_Y+PAD;
    float fw=COL_W;

    /* Name field */
    bool fn=(st->focus==SETTINGS_FOCUS_NAME);
    if(hittest(x,y+text_line_height(SCALE_SM)+2,fw,FIELD_H,mx,my)&&
       input_mouse_button_pressed(SDL_BUTTON_LEFT)) do_focus(st,SETTINGS_FOCUS_NAME);
    if(fn) textinput_update(&st->fi_name,x+4,y+text_line_height(SCALE_SM)+4,fw-8,SCALE_LBL);
    y+=field_h();

    /* Grid W */
    bool fgw=(st->focus==SETTINGS_FOCUS_GRID_W);
    float half=(fw-GAP)*.5f;
    if(hittest(x,y+text_line_height(SCALE_SM)+2,half,FIELD_H,mx,my)&&
       input_mouse_button_pressed(SDL_BUTTON_LEFT)) do_focus(st,SETTINGS_FOCUS_GRID_W);
    if(fgw) textinput_update(&st->fi_grid_w,x+4,y+text_line_height(SCALE_SM)+4,half-8,SCALE_LBL);

    bool fgh=(st->focus==SETTINGS_FOCUS_GRID_H);
    if(hittest(x+half+GAP,y+text_line_height(SCALE_SM)+2,half,FIELD_H,mx,my)&&
       input_mouse_button_pressed(SDL_BUTTON_LEFT)) do_focus(st,SETTINGS_FOCUS_GRID_H);
    if(fgh) textinput_update(&st->fi_grid_h,x+half+GAP+4,y+text_line_height(SCALE_SM)+4,half-8,SCALE_LBL);
    y+=field_h();

    /* Tile W/H */
    bool ftw=(st->focus==SETTINGS_FOCUS_TILE_W);
    if(hittest(x,y+text_line_height(SCALE_SM)+2,half,FIELD_H,mx,my)&&
       input_mouse_button_pressed(SDL_BUTTON_LEFT)) do_focus(st,SETTINGS_FOCUS_TILE_W);
    if(ftw) textinput_update(&st->fi_tile_w,x+4,y+text_line_height(SCALE_SM)+4,half-8,SCALE_LBL);

    bool fth=(st->focus==SETTINGS_FOCUS_TILE_H);
    if(hittest(x+half+GAP,y+text_line_height(SCALE_SM)+2,half,FIELD_H,mx,my)&&
       input_mouse_button_pressed(SDL_BUTTON_LEFT)) do_focus(st,SETTINGS_FOCUS_TILE_H);
    if(fth) textinput_update(&st->fi_tile_h,x+half+GAP+4,y+text_line_height(SCALE_SM)+4,half-8,SCALE_LBL);
    y+=field_h()+GAP;

    /* Apply Grid button */
    if(draw_btn(x,y,140,BTN_H,"APPLY GRID",mx,my)){
        int gw=atoi(textinput_get(&st->fi_grid_w));
        int gh=atoi(textinput_get(&st->fi_grid_h));
        if(gw>=PROJECT_GRID_MIN&&gw<=PROJECT_GRID_MAX&&
           gh>=PROJECT_GRID_MIN&&gh<=PROJECT_GRID_MAX){
            st->pending_grid_w=gw; st->pending_grid_h=gh;
            st->wants_resize=true;
            proj->grid_w=gw; proj->grid_h=gh;
        } else snprintf(st->status,sizeof st->status,"GRID %d-%d ONLY",PROJECT_GRID_MIN,PROJECT_GRID_MAX);
    }

    /* Apply Tile button */
    if(draw_btn(x+148,y,140,BTN_H,"APPLY TILE",mx,my)){
        int tw2=atoi(textinput_get(&st->fi_tile_w));
        int th2=atoi(textinput_get(&st->fi_tile_h));
        if(tw2>=8&&tw2<=256&&th2>=4&&th2<=128){
            st->pending_tile_w=tw2; st->pending_tile_h=th2;
            st->wants_tile_resize=true;
            proj->tile_w=tw2; proj->tile_h=th2;
        } else snprintf(st->status,sizeof st->status,"TILE W 8-256 H 4-128");
    }
    y+=BTN_H+GAP*2;

    /* Save project button */
    if(draw_btn(x,y,130,BTN_H,"SAVE PROJECT",mx,my)){
        strncpy(proj->name,textinput_get(&st->fi_name),PROJECT_NAME_MAX-1);
        project_save(proj);
        snprintf(st->status,sizeof st->status,"PROJECT SAVED.");
        saved=true;
    }
    y+=BTN_H+GAP*2;

    /* Close project -- back to the launcher without quitting the app.
       Saves first (same as the button above) so closing never silently
       drops unsaved world/object/sprite work. */
    if(draw_btn(x,y,160,BTN_H,"CLOSE PROJECT",mx,my)){
        strncpy(proj->name,textinput_get(&st->fi_name),PROJECT_NAME_MAX-1);
        project_save(proj);
        st->wants_close_project = true;
    }
    return saved;
}

void settings_tab_render(const SettingsTab*st,const Project*proj,int vw,int vh){
    renderer_draw_quad(0,CONTENT_Y,(float)vw,(float)vh-CONTENT_Y,0.08f,0.08f,0.10f,1);
    int mx,my; input_mouse_pos(&mx,&my);

    float x=PAD, y=CONTENT_Y+PAD, fw=COL_W;
    float half=(fw-GAP)*.5f;

    /* Title */
    text_draw(x,y,2.0f,0.30f,0.78f,0.48f,1,"SETTINGS");
    y+=text_line_height(2.0f)+GAP*2;

    draw_field(x,y,fw,"PROJECT NAME",&st->fi_name,st->focus==SETTINGS_FOCUS_NAME,mx,my);
    y+=field_h();

    /* Grid W/H side by side */
    text_draw(x,y,SCALE_SM,0.52f,0.52f,0.55f,1,"GRID W");
    text_draw(x+half+GAP,y,SCALE_SM,0.52f,0.52f,0.55f,1,"GRID H");
    float fy2=y+text_line_height(SCALE_SM)+2;
    /* W */
    float bg=st->focus==SETTINGS_FOCUS_GRID_W?0.17f:0.10f;
    renderer_draw_quad(x,fy2,half,FIELD_H,bg,bg,bg,1);
    draw_border(x,fy2,half,FIELD_H,
        st->focus==SETTINGS_FOCUS_GRID_W?0.30f:0.22f,
        st->focus==SETTINGS_FOCUS_GRID_W?0.72f:0.28f,
        st->focus==SETTINGS_FOCUS_GRID_W?0.42f:0.22f);
    float th=text_line_height(SCALE_LBL);
    textinput_render(&st->fi_grid_w,x+4,fy2+(FIELD_H-th)*.5f,SCALE_LBL,1,1,1,1);
    /* H */
    float bg2=st->focus==SETTINGS_FOCUS_GRID_H?0.17f:0.10f;
    renderer_draw_quad(x+half+GAP,fy2,half,FIELD_H,bg2,bg2,bg2,1);
    draw_border(x+half+GAP,fy2,half,FIELD_H,
        st->focus==SETTINGS_FOCUS_GRID_H?0.30f:0.22f,
        st->focus==SETTINGS_FOCUS_GRID_H?0.72f:0.28f,
        st->focus==SETTINGS_FOCUS_GRID_H?0.42f:0.22f);
    textinput_render(&st->fi_grid_h,x+half+GAP+4,fy2+(FIELD_H-th)*.5f,SCALE_LBL,1,1,1,1);
    y+=field_h();

    /* Tile W/H */
    text_draw(x,y,SCALE_SM,0.52f,0.52f,0.55f,1,"TILE W (PX)");
    text_draw(x+half+GAP,y,SCALE_SM,0.52f,0.52f,0.55f,1,"TILE H (PX)");
    float fy3=y+text_line_height(SCALE_SM)+2;
    float bg3=st->focus==SETTINGS_FOCUS_TILE_W?0.17f:0.10f;
    renderer_draw_quad(x,fy3,half,FIELD_H,bg3,bg3,bg3,1);
    draw_border(x,fy3,half,FIELD_H,
        st->focus==SETTINGS_FOCUS_TILE_W?0.30f:0.22f,
        st->focus==SETTINGS_FOCUS_TILE_W?0.72f:0.28f,
        st->focus==SETTINGS_FOCUS_TILE_W?0.42f:0.22f);
    textinput_render(&st->fi_tile_w,x+4,fy3+(FIELD_H-th)*.5f,SCALE_LBL,1,1,1,1);
    float bg4=st->focus==SETTINGS_FOCUS_TILE_H?0.17f:0.10f;
    renderer_draw_quad(x+half+GAP,fy3,half,FIELD_H,bg4,bg4,bg4,1);
    draw_border(x+half+GAP,fy3,half,FIELD_H,
        st->focus==SETTINGS_FOCUS_TILE_H?0.30f:0.22f,
        st->focus==SETTINGS_FOCUS_TILE_H?0.72f:0.28f,
        st->focus==SETTINGS_FOCUS_TILE_H?0.42f:0.22f);
    textinput_render(&st->fi_tile_h,x+half+GAP+4,fy3+(FIELD_H-th)*.5f,SCALE_LBL,1,1,1,1);
    y+=field_h()+GAP;

    /* Buttons row */
    draw_btn(x,y,140,BTN_H,"APPLY GRID",mx,my);
    draw_btn(x+148,y,140,BTN_H,"APPLY TILE",mx,my);
    y+=BTN_H+GAP*2;
    draw_btn(x,y,130,BTN_H,"SAVE PROJECT",mx,my);
    y+=BTN_H+GAP*2;
    draw_btn(x,y,160,BTN_H,"CLOSE PROJECT",mx,my);
    y+=BTN_H+GAP*2;

    /* Current values */
    char cur[128];
    snprintf(cur,sizeof cur,"CURRENT: %s  %dx%d  TILE %dx%d",
             proj->name,proj->grid_w,proj->grid_h,proj->tile_w,proj->tile_h);
    text_draw(x,y,SCALE_SM,0.45f,0.45f,0.50f,1,cur);

    /* Status */
    if(st->status[0])
        text_draw(x,(float)vh-PAD-text_line_height(SCALE_SM),
                  SCALE_SM,0.30f,0.85f,0.48f,1,st->status);
}
