#include "objects_tab.h"

#include <stdio.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "../platform/input.h"
#include "../renderer/renderer.h"
#include "../core/log.h"
#include "text.h"
#include "tabbar.h"
#include "layout.h"

/* Layout */
#define LIST_W    180.0f
#define INSP_W    280.0f
#define PAD        10.0f
#define BTN_H      26.0f
#define FIELD_H    24.0f
#define GAP         4.0f
#define SCALE_LBL  1.5f
#define SCALE_BODY 1.5f
#define SCALE_SM   1.3f
#define CONTENT_Y  ((float)TABBAR_H)

static bool hittest(float x,float y,float w,float h,int mx,int my){
    return (float)mx>=x&&(float)mx<x+w&&(float)my>=y&&(float)my<y+h;
}
static void draw_border(float x,float y,float w,float h,float r,float g,float b){
    renderer_draw_quad(x,y,w,1,r,g,b,1); renderer_draw_quad(x,y+h-1,w,1,r,g,b,1);
    renderer_draw_quad(x,y,1,h,r,g,b,1); renderer_draw_quad(x+w-1,y,1,h,r,g,b,1);
}
static bool draw_btn(float x,float y,float w,float h,const char*lbl,bool active,int mx,int my){
    bool hov=hittest(x,y,w,h,mx,my);
    bool clk=hov&&input_mouse_button_pressed(SDL_BUTTON_LEFT);
    float br=active?0.14f:(hov?0.18f:0.11f);
    float bg=active?0.48f:(hov?0.22f:0.14f);
    float bb=active?0.28f:(hov?0.14f:0.11f);
    renderer_draw_quad(x,y,w,h,br,bg,bb,1);
    draw_border(x,y,w,h,active?0.30f:0.24f,active?0.80f:0.28f,active?0.45f:0.24f);
    float tw=text_measure_width(lbl,SCALE_BODY),th=text_line_height(SCALE_BODY);
    text_draw(x+(w-tw)*.5f,y+(h-th)*.5f,SCALE_BODY,1,1,1,1,lbl);
    return clk;
}
static void draw_field_box(float x,float y,float w,float h,bool focused){
    float bg=focused?0.16f:0.10f;
    renderer_draw_quad(x,y,w,h,bg,bg,bg,1);
    draw_border(x,y,w,h,focused?0.28f:0.20f,focused?0.72f:0.26f,focused?0.42f:0.20f);
}

/* Focus helpers */
static void unfocus_all(ObjectsTab*ot){
    switch(ot->focus){
        case OBJ_FOCUS_NAME:  textinput_unfocus(&ot->fi_name); break;
        case OBJ_FOCUS_SPRITE:textinput_unfocus(&ot->fi_sprite); break;
        case OBJ_FOCUS_PROP_NAME:  textinput_unfocus(&ot->fi_prop_name[ot->focus_idx]); break;
        case OBJ_FOCUS_PROP_VALUE: textinput_unfocus(&ot->fi_prop_value[ot->focus_idx]); break;
        case OBJ_FOCUS_BEHAV_EVENT:  textinput_unfocus(&ot->fi_behav_event[ot->focus_idx]); break;
        case OBJ_FOCUS_BEHAV_SCRIPT: textinput_unfocus(&ot->fi_behav_script[ot->focus_idx]); break;
        default: break;
    }
    ot->focus=OBJ_FOCUS_NONE; ot->focus_idx=0;
}
static void set_focus(ObjectsTab*ot,ObjFocus f,int idx){
    unfocus_all(ot);
    ot->focus=f; ot->focus_idx=idx;
    switch(f){
        case OBJ_FOCUS_NAME:   textinput_focus(&ot->fi_name); break;
        case OBJ_FOCUS_SPRITE: textinput_focus(&ot->fi_sprite); break;
        case OBJ_FOCUS_PROP_NAME:   textinput_focus(&ot->fi_prop_name[idx]); break;
        case OBJ_FOCUS_PROP_VALUE:  textinput_focus(&ot->fi_prop_value[idx]); break;
        case OBJ_FOCUS_BEHAV_EVENT:  textinput_focus(&ot->fi_behav_event[idx]); break;
        case OBJ_FOCUS_BEHAV_SCRIPT: textinput_focus(&ot->fi_behav_script[idx]); break;
        default: break;
    }
}

/* Load selected def into edit fields */
static void load_editing(ObjectsTab*ot){
    if(ot->selected_idx<0||ot->selected_idx>=ot->registry->count) return;
    ot->editing=ot->registry->defs[ot->selected_idx];
    textinput_set(&ot->fi_name,  ot->editing.name);
    textinput_set(&ot->fi_sprite,ot->editing.sprite);
    for(int i=0;i<ot->editing.prop_count;i++){
        textinput_set(&ot->fi_prop_name[i], ot->editing.props[i].name);
        char val[128]; prop_value_to_str(val,sizeof val,
            ot->editing.props[i].type,&ot->editing.props[i].value);
        textinput_set(&ot->fi_prop_value[i],val);
    }
    for(int i=0;i<ot->editing.behavior_count;i++){
        textinput_set(&ot->fi_behav_event[i], ot->editing.behaviors[i].event);
        textinput_set(&ot->fi_behav_script[i],ot->editing.behaviors[i].script);
    }
}

/* Commit editing back to registry + disk */
static void commit_editing(ObjectsTab*ot){
    if(ot->selected_idx<0) return;
    /* Copy field values back into editing struct */
    strncpy(ot->editing.name,  textinput_get(&ot->fi_name),  OBJDEF_NAME_MAX-1);
    strncpy(ot->editing.sprite,textinput_get(&ot->fi_sprite),OBJDEF_NAME_MAX-1);
    for(int i=0;i<ot->editing.prop_count;i++){
        strncpy(ot->editing.props[i].name,
                textinput_get(&ot->fi_prop_name[i]),OBJDEF_NAME_MAX-1);
        prop_value_from_str(ot->editing.props[i].type,
            textinput_get(&ot->fi_prop_value[i]),
            &ot->editing.props[i].value);
    }
    for(int i=0;i<ot->editing.behavior_count;i++){
        strncpy(ot->editing.behaviors[i].event,
                textinput_get(&ot->fi_behav_event[i]),OBJDEF_NAME_MAX-1);
        strncpy(ot->editing.behaviors[i].script,
                textinput_get(&ot->fi_behav_script[i]),OBJDEF_PATH_MAX-1);
    }
    ot->registry->defs[ot->selected_idx]=ot->editing;
    if(objdef_save(&ot->editing))
        snprintf(ot->status,sizeof ot->status,"SAVED: %s",ot->editing.name);
    else
        snprintf(ot->status,sizeof ot->status,"SAVE FAILED");
}

void objects_tab_init(ObjectsTab*ot,ObjectDefRegistry*registry,SpritesTab*sprites){
    memset(ot,0,sizeof(*ot));
    ot->registry=registry; ot->sprites=sprites;
    ot->selected_idx=-1;
    textinput_init(&ot->fi_name,   OBJDEF_NAME_MAX-1,false);
    textinput_init(&ot->fi_sprite, OBJDEF_NAME_MAX-1,false);
    textinput_init(&ot->fi_new_name,OBJDEF_NAME_MAX-1,false);
    for(int i=0;i<OBJDEF_MAX_PROPS;i++){
        textinput_init(&ot->fi_prop_name[i], OBJDEF_NAME_MAX-1,false);
        textinput_init(&ot->fi_prop_value[i],63,false);
    }
    for(int i=0;i<OBJDEF_MAX_BEHAV;i++){
        textinput_init(&ot->fi_behav_event[i], OBJDEF_NAME_MAX-1,false);
        textinput_init(&ot->fi_behav_script[i],OBJDEF_PATH_MAX-1,false);
    }
}

void objects_tab_update(ObjectsTab*ot,int vw,int vh){
    int mx,my; input_mouse_pos(&mx,&my);

    /* --- Left list --- */
    float ly=CONTENT_Y+PAD;
    float item_h=text_line_height(SCALE_BODY)+GAP*2;
    int visible=(int)(((float)vh-ly-BTN_H-GAP)/(item_h+GAP));
    if(visible<1)visible=1;

    for(int i=ot->list_scroll;i<ot->registry->count&&i<ot->list_scroll+visible;i++){
        float iy2=ly+(float)(i-ot->list_scroll)*(item_h+GAP);
        if(hittest(PAD,iy2,LIST_W-PAD*2,item_h,mx,my)&&
           input_mouse_button_pressed(SDL_BUTTON_LEFT)){
            unfocus_all(ot);
            ot->selected_idx=i;
            ot->new_mode=false;
            load_editing(ot);
        }
    }
    /* New button */
    float new_btn_y=(float)vh-PAD-BTN_H;
    if(draw_btn(PAD,new_btn_y,LIST_W-PAD*2,BTN_H,"+ NEW",false,mx,my)){
        unfocus_all(ot);
        ot->new_mode=true;
        textinput_set(&ot->fi_new_name,"");
        textinput_focus(&ot->fi_new_name);
    }
    if(ot->new_mode){
        if(textinput_update(&ot->fi_new_name,PAD,new_btn_y-FIELD_H-GAP,
                             LIST_W-PAD*2,SCALE_BODY)){
            /* Enter pressed — create the object */
            const char*nm=textinput_get(&ot->fi_new_name);
            if(nm[0]&&ot->registry->count<OBJDEF_MAX_DEFS){
                ObjectDef nd; memset(&nd,0,sizeof nd);
                strncpy(nd.name,nm,OBJDEF_NAME_MAX-1);
                objdef_save(&nd);
                ot->registry->defs[ot->registry->count++]=nd;
                ot->selected_idx=ot->registry->count-1;
                load_editing(ot);
                ot->new_mode=false;
                textinput_unfocus(&ot->fi_new_name);
            }
        }
    }

    if(ot->selected_idx<0) return;

    /* --- Inspector active field update --- */
    /* Handled by ui_layout_field in objects_tab_render now! */
    (void)vw;
}

void objects_tab_render(const ObjectsTab*ot,int vw,int vh){
    renderer_draw_quad(0,CONTENT_Y,(float)vw,(float)vh-CONTENT_Y,0.08f,0.08f,0.10f,1);
    int mx,my; input_mouse_pos(&mx,&my);

    /* --- Left list panel --- */
    renderer_draw_quad(0,CONTENT_Y,LIST_W,(float)vh-CONTENT_Y,0.09f,0.09f,0.11f,1);
    renderer_draw_quad(LIST_W-1,CONTENT_Y,1,(float)vh-CONTENT_Y,0.22f,0.22f,0.28f,1);

    text_draw(PAD,CONTENT_Y+PAD,SCALE_SM,0.52f,0.52f,0.55f,1,"OBJECTS");
    float ly=CONTENT_Y+PAD+text_line_height(SCALE_SM)+GAP;
    float item_h=text_line_height(SCALE_BODY)+GAP*2;
    int visible=(int)(((float)vh-ly-BTN_H-GAP*2)/(item_h+GAP));
    if(visible<1)visible=1;

    for(int i=ot->list_scroll;i<ot->registry->count&&i<ot->list_scroll+visible;i++){
        float iy=ly+(float)(i-ot->list_scroll)*(item_h+GAP);
        bool sel=(i==ot->selected_idx);
        bool hov=hittest(PAD,iy,LIST_W-PAD*2,item_h,mx,my);
        float bg=sel?0.16f:(hov?0.14f:0.10f);
        float bgg=sel?0.48f:(hov?0.20f:0.12f);
        renderer_draw_quad(PAD,iy,LIST_W-PAD*2,item_h,bg,bgg,bg,1);
        text_draw(PAD*2,iy+GAP,SCALE_BODY,sel?1.0f:0.75f,1.0f,sel?1.0f:0.75f,1,
                  ot->registry->defs[i].name);
    }
    if(ot->registry->count==0)
        text_draw(PAD,ly,SCALE_SM,0.35f,0.35f,0.38f,1,"NONE YET");

    /* New button */
    float new_btn_y=(float)vh-PAD-BTN_H;
    draw_btn(PAD,new_btn_y,LIST_W-PAD*2,BTN_H,"+ NEW",ot->new_mode,mx,my);
    if(ot->new_mode){
        float fy=new_btn_y-FIELD_H-GAP;
        draw_field_box(PAD,fy,LIST_W-PAD*2,FIELD_H,true);
        float th=text_line_height(SCALE_BODY);
        textinput_render(&ot->fi_new_name,PAD+4,fy+(FIELD_H-th)*.5f,SCALE_BODY,1,1,1,1);
    }

    /* --- Inspector panel --- */
    if(ot->selected_idx<0){
        text_draw(LIST_W+PAD,CONTENT_Y+PAD*3,SCALE_BODY,0.40f,0.40f,0.45f,1,"SELECT AN OBJECT");
        return;
    }
    float ix=(float)vw-INSP_W;
    renderer_draw_quad(ix,CONTENT_Y,INSP_W,(float)vh-CONTENT_Y,0.09f,0.09f,0.11f,1);
    renderer_draw_quad(ix,CONTENT_Y,1,(float)vh-CONTENT_Y,0.22f,0.22f,0.28f,1);

    UILayout l;
    ui_layout_begin(&l, ix + PAD, CONTENT_Y + PAD, INSP_W - PAD * 2.0f);

    /* Name */
    ui_layout_label(&l, "NAME", true);
    if (ui_layout_field(&l, &((ObjectsTab*)ot)->fi_name, 0, FIELD_H, ot->focus == OBJ_FOCUS_NAME, mx, my, NULL))
        set_focus((ObjectsTab*)ot, OBJ_FOCUS_NAME, 0);

    /* Sprite */
    ui_layout_label(&l, "SPRITE NAME", true);
    if (ui_layout_field(&l, &((ObjectsTab*)ot)->fi_sprite, 0, FIELD_H, ot->focus == OBJ_FOCUS_SPRITE, mx, my, NULL))
        set_focus((ObjectsTab*)ot, OBJ_FOCUS_SPRITE, 0);

    /* Properties header */
    ui_layout_label(&l, "PROPERTIES", true);
    float half = (INSP_W - PAD * 3.0f) * 0.5f;
    for(int i=0;i<ot->editing.prop_count&&l.cursor_y+FIELD_H<(float)vh-BTN_H*2-GAP*4;i++){
        ui_layout_row_begin(&l);
        if (ui_layout_field(&l, &((ObjectsTab*)ot)->fi_prop_name[i], half, FIELD_H, ot->focus == OBJ_FOCUS_PROP_NAME && ot->focus_idx == i, mx, my, NULL))
            set_focus((ObjectsTab*)ot, OBJ_FOCUS_PROP_NAME, i);
            
        ui_layout_row_step_x(&l, half + PAD);
        if (ui_layout_field(&l, &((ObjectsTab*)ot)->fi_prop_value[i], half, FIELD_H, ot->focus == OBJ_FOCUS_PROP_VALUE && ot->focus_idx == i, mx, my, NULL))
            set_focus((ObjectsTab*)ot, OBJ_FOCUS_PROP_VALUE, i);
        ui_layout_row_end(&l);
        
        char tl[8]; snprintf(tl,sizeof tl,"%s",prop_type_name(ot->editing.props[i].type));
        ui_layout_label(&l, tl, true);
    }
    
    /* Add property button */
    if(ot->editing.prop_count<OBJDEF_MAX_PROPS){
        if (ui_layout_button(&l, "+ PROP", 100, BTN_H, false, mx, my)) {
            int n=((ObjectsTab*)ot)->editing.prop_count;
            ObjectProperty*p=&((ObjectsTab*)ot)->editing.props[n];
            strncpy(p->name,"new_prop",OBJDEF_NAME_MAX-1);
            p->type=PROP_INT; p->value.as_int=0;
            ((ObjectsTab*)ot)->editing.prop_count++;
            textinput_set(&((ObjectsTab*)ot)->fi_prop_name[n],"new_prop");
            textinput_set(&((ObjectsTab*)ot)->fi_prop_value[n],"0");
        }
    }
    
    /* Behaviors (Sprite Functions) */
    ui_layout_gap(&l, 10.0f);
    ui_layout_label(&l, "BEHAVIORS (FUNCTIONS)", true);
    for(int i=0;i<ot->editing.behavior_count&&l.cursor_y+FIELD_H<(float)vh-BTN_H*2-GAP*4;i++){
        ui_layout_row_begin(&l);
        if (ui_layout_field(&l, &((ObjectsTab*)ot)->fi_behav_event[i], half, FIELD_H, ot->focus == OBJ_FOCUS_BEHAV_EVENT && ot->focus_idx == i, mx, my, NULL))
            set_focus((ObjectsTab*)ot, OBJ_FOCUS_BEHAV_EVENT, i);
            
        ui_layout_row_step_x(&l, half + PAD);
        if (ui_layout_field(&l, &((ObjectsTab*)ot)->fi_behav_script[i], half, FIELD_H, ot->focus == OBJ_FOCUS_BEHAV_SCRIPT && ot->focus_idx == i, mx, my, NULL))
            set_focus((ObjectsTab*)ot, OBJ_FOCUS_BEHAV_SCRIPT, i);
        ui_layout_row_end(&l);
    }
    
    if (ot->editing.behavior_count < OBJDEF_MAX_BEHAV) {
        if (ui_layout_button(&l, "+ BEHAVIOR", 120, BTN_H, false, mx, my)) {
            int n=((ObjectsTab*)ot)->editing.behavior_count;
            ObjectBehavior*b=&((ObjectsTab*)ot)->editing.behaviors[n];
            strncpy(b->event,"on_interact",OBJDEF_NAME_MAX-1);
            strncpy(b->script,"",OBJDEF_PATH_MAX-1);
            ((ObjectsTab*)ot)->editing.behavior_count++;
            textinput_set(&((ObjectsTab*)ot)->fi_behav_event[n],"on_interact");
            textinput_set(&((ObjectsTab*)ot)->fi_behav_script[n],"");
        }
    }

    /* Save / Delete */
    float bot_y=(float)vh-PAD-BTN_H;
    if(draw_btn(ix+PAD,bot_y,100,BTN_H,"SAVE",false,mx,my))
        commit_editing((ObjectsTab*)ot);
    if(draw_btn(ix+PAD+110,bot_y,90,BTN_H,"DELETE",false,mx,my)){
        objdef_delete(ot->editing.name);
        /* Remove from registry */
        ObjectDefRegistry*r=((ObjectsTab*)ot)->registry;
        int si=((ObjectsTab*)ot)->selected_idx;
        if(si<r->count-1) r->defs[si]=r->defs[r->count-1];
        r->count--;
        ((ObjectsTab*)ot)->selected_idx=-1;
    }

    /* Status */
    if(ot->status[0])
        text_draw(ix+PAD,bot_y-text_line_height(SCALE_SM)-4,SCALE_SM,
                  0.30f,0.85f,0.48f,1,ot->status);
}
