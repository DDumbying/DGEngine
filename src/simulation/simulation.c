
#include "simulation.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
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

bool resource_store_has_wood(const ResourceStore *rs, int amount) {
    return amount >= 0 && rs->wood >= amount;
}

bool resource_store_has_stone(const ResourceStore *rs, int amount) {
    return amount >= 0 && rs->stone >= amount;
}

bool resource_store_try_spend_wood(ResourceStore *rs, int amount) {
    if (!resource_store_has_wood(rs, amount)) return false;
    rs->wood -= amount;
    resource_store_log(rs);
    return true;
}

bool resource_store_try_spend_stone(ResourceStore *rs, int amount) {
    if (!resource_store_has_stone(rs, amount)) return false;
    rs->stone -= amount;
    resource_store_log(rs);
    return true;
}

/* ---------------------------------------------------------------------
   Save / load — see the format comment in simulation.h. */

#define DGES_MAGIC   "DGES"
#define DGES_VERSION 1u

bool simulation_save(const SimClock *clk, const ResourceStore *rs, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        LOG_ERROR("simulation_save: could not open '%s' for writing", path);
        return false;
    }

    unsigned int version = DGES_VERSION;
    int32_t wood  = (int32_t)rs->wood;
    int32_t stone = (int32_t)rs->stone;

    bool ok = true;
    ok &= fwrite(DGES_MAGIC, 1, 4, f) == 4;
    ok &= fwrite(&version,        sizeof(version),        1, f) == 1;
    ok &= fwrite(&clk->elapsed,   sizeof(clk->elapsed),   1, f) == 1;
    ok &= fwrite(&clk->speed,     sizeof(clk->speed),     1, f) == 1;
    ok &= fwrite(&clk->saved_speed, sizeof(clk->saved_speed), 1, f) == 1;
    ok &= fwrite(&wood,  sizeof(wood),  1, f) == 1;
    ok &= fwrite(&stone, sizeof(stone), 1, f) == 1;

    fclose(f);
    if (!ok) {
        LOG_ERROR("simulation_save: write error writing '%s'", path);
        return false;
    }
    LOG_INFO("Simulation state saved -> '%s' (elapsed=%.1fs wood=%d stone=%d)",
             path, clk->elapsed, rs->wood, rs->stone);
    return true;
}

bool simulation_load(SimClock *clk, ResourceStore *rs, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        LOG_ERROR("simulation_load: could not open '%s'", path);
        return false;
    }

    char magic[4];
    unsigned int version;
    bool ok = true;
    ok &= fread(magic, 1, 4, f) == 4;
    ok &= fread(&version, sizeof(version), 1, f) == 1;

    if (!ok || magic[0] != 'D' || magic[1] != 'G' || magic[2] != 'E' || magic[3] != 'S') {
        LOG_ERROR("simulation_load: '%s' is not a valid DGES simulation file", path);
        fclose(f);
        return false;
    }
    if (version != DGES_VERSION) {
        LOG_ERROR("simulation_load: '%s' has unsupported version %u (expected %u)",
                   path, version, DGES_VERSION);
        fclose(f);
        return false;
    }

    SimClock loaded_clk;
    ResourceStore loaded_rs;
    int32_t wood, stone;
    ok &= fread(&loaded_clk.elapsed,     sizeof(loaded_clk.elapsed),     1, f) == 1;
    ok &= fread(&loaded_clk.speed,       sizeof(loaded_clk.speed),       1, f) == 1;
    ok &= fread(&loaded_clk.saved_speed, sizeof(loaded_clk.saved_speed), 1, f) == 1;
    ok &= fread(&wood,  sizeof(wood),  1, f) == 1;
    ok &= fread(&stone, sizeof(stone), 1, f) == 1;
    fclose(f);

    if (!ok) {
        LOG_ERROR("simulation_load: truncated/corrupt data in '%s'", path);
        return false;
    }

    loaded_rs.wood  = (int)wood;
    loaded_rs.stone = (int)stone;

    *clk = loaded_clk;
    *rs  = loaded_rs;
    LOG_INFO("Simulation state loaded <- '%s' (elapsed=%.1fs wood=%d stone=%d)",
             path, clk->elapsed, rs->wood, rs->stone);
    return true;
}
