
#include "simulation.h"

#include <stdbool.h>
#include <limits.h>
#include "../core/log.h"

/* ---- SimClock ---- */

void simclock_init(SimClock *clk) {
    clk->elapsed    = 0.0;
    clk->speed      = 1.0f;
    clk->saved_speed = 1.0f;
}

float simclock_tick(SimClock *clk, float dt) {
    float gdt = dt * clk->speed;
    clk->elapsed += (double)gdt;
    return gdt;
}

void simclock_pause(SimClock *clk) {
    if (clk->speed != 0.0f) {
        clk->saved_speed = clk->speed;
        clk->speed = 0.0f;
    }
}

void simclock_resume(SimClock *clk) {
    if (clk->speed == 0.0f)
        clk->speed = clk->saved_speed;
}

bool simclock_is_paused(const SimClock *clk) {
    return clk->speed == 0.0f;
}

/* ---- ResourceStore ---- */

void resource_store_init(ResourceStore *rs) {
    rs->wood  = 0;
    rs->stone = 0;
}

static void clamp_store(ResourceStore *rs) {
    if (rs->wood  < 0) rs->wood  = 0;
    if (rs->stone < 0) rs->stone = 0;
}

void resource_store_add_wood(ResourceStore *rs, int amount) {
    if (amount > 0 && rs->wood > INT_MAX - amount) {
        rs->wood = INT_MAX;
    } else if (amount < 0 && rs->wood < INT_MIN - amount) {
        rs->wood = INT_MIN;
    } else {
        rs->wood += amount;
    }
    clamp_store(rs);
    resource_store_log(rs);
}

void resource_store_add_stone(ResourceStore *rs, int amount) {
    if (amount > 0 && rs->stone > INT_MAX - amount) {
        rs->stone = INT_MAX;
    } else if (amount < 0 && rs->stone < INT_MIN - amount) {
        rs->stone = INT_MIN;
    } else {
        rs->stone += amount;
    }
    clamp_store(rs);
    resource_store_log(rs);
}

void resource_store_log(const ResourceStore *rs) {
    LOG_INFO("Resources: wood=%d  stone=%d", rs->wood, rs->stone);
}
