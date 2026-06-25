#include <stdio.h>
#include <assert.h>
#include <math.h>
#include "simulation/simulation.h"

static int approx(double a, double b) { return fabs(a - b) < 0.0001; }

int main(void) {
    /* --- Round trip while running normally --- */
    SimClock clk;
    ResourceStore rs;
    simclock_init(&clk);
    resource_store_init(&rs);

    clk.elapsed = 1234.5;
    clk.speed = 2.0f;
    clk.saved_speed = 2.0f;
    resource_store_add_wood(&rs, 42);
    resource_store_add_stone(&rs, 17);

    assert(simulation_save(&clk, &rs, "/tmp/dge_test_sim.dge"));

    SimClock loaded_clk;
    ResourceStore loaded_rs;
    simclock_init(&loaded_clk);
    resource_store_init(&loaded_rs);
    assert(simulation_load(&loaded_clk, &loaded_rs, "/tmp/dge_test_sim.dge"));

    assert(approx(loaded_clk.elapsed, 1234.5));
    assert(approx(loaded_clk.speed, 2.0));
    assert(approx(loaded_clk.saved_speed, 2.0));
    assert(loaded_rs.wood == 42);
    assert(loaded_rs.stone == 17);
    printf("PASS: simulation_save/load round-trip preserves elapsed/speed/resources\n");

    /* --- The case this fix specifically exists for: saved while paused.
       speed must come back as 0 (still paused), and saved_speed must
       come back as the pre-pause value so resuming doesn't snap to 1x. --- */
    SimClock paused_clk;
    simclock_init(&paused_clk);
    paused_clk.elapsed = 500.0;
    paused_clk.speed = 3.0f;
    simclock_pause(&paused_clk);
    assert(simclock_is_paused(&paused_clk));
    assert(approx(paused_clk.saved_speed, 3.0));

    ResourceStore rs2;
    resource_store_init(&rs2);
    assert(simulation_save(&paused_clk, &rs2, "/tmp/dge_test_sim_paused.dge"));

    SimClock loaded_paused;
    ResourceStore loaded_rs2;
    simclock_init(&loaded_paused);
    resource_store_init(&loaded_rs2);
    assert(simulation_load(&loaded_paused, &loaded_rs2, "/tmp/dge_test_sim_paused.dge"));

    assert(simclock_is_paused(&loaded_paused));
    assert(approx(loaded_paused.saved_speed, 3.0));
    simclock_resume(&loaded_paused);
    assert(approx(loaded_paused.speed, 3.0)); /* resumes to pre-pause speed, not 1x */
    printf("PASS: paused state (speed=0, saved_speed preserved) survives save/load\n");

    /* --- Corrupt file: should fail cleanly, not crash --- */
    FILE *f = fopen("/tmp/dge_test_sim_garbage.dge", "wb");
    fwrite("XXXX", 1, 4, f);
    fclose(f);
    SimClock garbage_clk;
    ResourceStore garbage_rs;
    simclock_init(&garbage_clk);
    resource_store_init(&garbage_rs);
    assert(!simulation_load(&garbage_clk, &garbage_rs, "/tmp/dge_test_sim_garbage.dge"));
    printf("PASS: simulation_load rejects bad magic instead of crashing\n");

    printf("test_simulation: ALL TESTS PASSED\n");
    return 0;
}
