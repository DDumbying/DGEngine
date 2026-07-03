#ifndef DGE_PLAY_MODE_H
#define DGE_PLAY_MODE_H

/*  Play Mode — separates "editing the world" from "running the game".

    In EDIT mode:  the full editor UI is visible, camera pans freely,
                   tiles can be painted, objects placed/deleted.
    In PLAY mode:  the editor UI is hidden, simulation runs at full speed,
                   camera is locked to game perspective, no tile editing.
                   The world state is NOT reset on play — save first if
                   you want to restore it (future: auto-snapshot).

    PlayModeState is owned by main.c and threaded through wherever
    needed. ui.c checks it to decide whether to draw editor overlays.
    editor_update() checks it to gate all editing actions.
    The tab bar play button sets/clears it.                              */

#include <stdbool.h>

typedef enum {
    PLAY_MODE_EDIT = 0,   /* editor is active */
    PLAY_MODE_PLAY,       /* game is running, editor hidden */
    PLAY_MODE_PAUSED,     /* game paused mid-play (still hides editor) */
} PlayModeState;

typedef struct {
    PlayModeState state;

    /* Whether to show a brief "PLAY MODE" overlay on first entering play */
    float overlay_timer;  /* counts down from 2.0 seconds, then 0 */
} PlayMode;

static inline void play_mode_init(PlayMode *pm) {
    pm->state = PLAY_MODE_EDIT;
    pm->overlay_timer = 0.0f;
}

static inline bool play_mode_is_playing(const PlayMode *pm) {
    return pm->state == PLAY_MODE_PLAY || pm->state == PLAY_MODE_PAUSED;
}

static inline bool play_mode_is_editing(const PlayMode *pm) {
    return pm->state == PLAY_MODE_EDIT;
}

/* Called by the tab bar's play/pause/stop buttons */
static inline void play_mode_enter_play(PlayMode *pm) {
    pm->state = PLAY_MODE_PLAY;
    pm->overlay_timer = 2.0f;
}

static inline void play_mode_toggle_pause(PlayMode *pm) {
    if (pm->state == PLAY_MODE_PLAY)       pm->state = PLAY_MODE_PAUSED;
    else if (pm->state == PLAY_MODE_PAUSED) pm->state = PLAY_MODE_PLAY;
}

static inline void play_mode_stop(PlayMode *pm) {
    pm->state = PLAY_MODE_EDIT;
    pm->overlay_timer = 0.0f;
}

static inline void play_mode_update(PlayMode *pm, float dt) {
    if (pm->overlay_timer > 0.0f) {
        pm->overlay_timer -= dt;
        if (pm->overlay_timer < 0.0f) pm->overlay_timer = 0.0f;
    }
}

#endif /* DGE_PLAY_MODE_H */
