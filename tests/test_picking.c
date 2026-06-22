/*  Tests the forward/inverse isometric projection math that editor.c's
    pick_tile() relies on for mouse picking. The formulas are copied
    here rather than included from renderer.c/editor.c, since those
    pull in GL/SDL state this test shouldn't need to set up — if you
    change the projection math in either place, update both. */

#include <stdio.h>
#include <math.h>

static void grid_to_screen(int gx, int gy, float tw, float th, float *cx, float *cy) {
    *cx = (float)(gx - gy) * (tw * 0.5f);
    *cy = (float)(gx + gy) * (th * 0.5f);
}

static void screen_to_grid(float cx, float cy, float tw, float th, int *gx, int *gy) {
    float a = (2.0f * cx) / tw;
    float b = (2.0f * cy) / th;
    float fgx = (a + b) * 0.5f;
    float fgy = (b - a) * 0.5f;
    *gx = (int)floorf(fgx + 0.5f);
    *gy = (int)floorf(fgy + 0.5f);
}

int main(void) {
    const float tw = 64.0f, th = 32.0f;
    int mismatches = 0;

    for (int gy = -50; gy <= 50; gy++) {
        for (int gx = -50; gx <= 50; gx++) {
            float cx, cy;
            grid_to_screen(gx, gy, tw, th, &cx, &cy);

            int rgx, rgy;
            screen_to_grid(cx, cy, tw, th, &rgx, &rgy);

            if (rgx != gx || rgy != gy) {
                printf("MISMATCH: (%d,%d) -> screen (%.2f,%.2f) -> picked (%d,%d)\n",
                       gx, gy, cx, cy, rgx, rgy);
                mismatches++;
            }
        }
    }

    if (mismatches == 0) {
        printf("test_picking: PASS — round-trips exactly for all 101x101 grid "
               "coords tested\n");
        return 0;
    }
    printf("test_picking: FAIL — %d mismatches\n", mismatches);
    return 1;
}
