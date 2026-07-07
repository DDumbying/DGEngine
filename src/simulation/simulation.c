
#include "simulation.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
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
    memset(rs, 0, sizeof(*rs));
}

/* Finds an existing entry by name, or NULL if this name has never been
   added. Internal only — callers outside this file always go through
   resource_store_get/has, which handle the "never added" case as 0/no
   rather than exposing a NULL to check. */
static ResourceEntry *find_entry(ResourceStore *rs, const char *kind_name) {
    for (int i = 0; i < rs->count; i++)
        if (strncmp(rs->entries[i].name, kind_name, RESOURCE_NAME_MAX) == 0)
            return &rs->entries[i];
    return NULL;
}

static const ResourceEntry *find_entry_const(const ResourceStore *rs, const char *kind_name) {
    for (int i = 0; i < rs->count; i++)
        if (strncmp(rs->entries[i].name, kind_name, RESOURCE_NAME_MAX) == 0)
            return &rs->entries[i];
    return NULL;
}

/* Finds-or-creates. Returns NULL only if the store is genuinely full
   (RESOURCE_STORE_MAX_KINDS distinct names already in use) — logged
   once at the call site rather than here, so a single "store full"
   condition doesn't spam the log once per attempted add. */
static ResourceEntry *find_or_create_entry(ResourceStore *rs, const char *kind_name) {
    ResourceEntry *e = find_entry(rs, kind_name);
    if (e) return e;
    if (rs->count >= RESOURCE_STORE_MAX_KINDS) return NULL;
    e = &rs->entries[rs->count++];
    memset(e, 0, sizeof(*e));
    strncpy(e->name, kind_name, RESOURCE_NAME_MAX - 1);
    return e;
}

void resource_store_add(ResourceStore *rs, const char *kind_name, int amount) {
    ResourceEntry *e = find_or_create_entry(rs, kind_name);
    if (!e) {
        LOG_WARN("resource_store_add: store full (max %d distinct resource kinds), "
                 "dropping '%s'", RESOURCE_STORE_MAX_KINDS, kind_name);
        return;
    }
    if (amount > 0 && e->amount > INT_MAX - amount) {
        e->amount = INT_MAX;
    } else if (amount < 0 && e->amount < INT_MIN - amount) {
        e->amount = INT_MIN;
    } else {
        e->amount += amount;
    }
    if (e->amount < 0) e->amount = 0;
    resource_store_log(rs);
}

int resource_store_get(const ResourceStore *rs, const char *kind_name) {
    const ResourceEntry *e = find_entry_const(rs, kind_name);
    return e ? e->amount : 0;
}

bool resource_store_has(const ResourceStore *rs, const char *kind_name, int amount) {
    return amount >= 0 && resource_store_get(rs, kind_name) >= amount;
}

bool resource_store_try_spend(ResourceStore *rs, const char *kind_name, int amount) {
    if (!resource_store_has(rs, kind_name, amount)) return false;
    ResourceEntry *e = find_entry(rs, kind_name);
    e->amount -= amount; /* find_entry can't be NULL here: resource_store_has()
                             already confirmed an entry with >= amount exists */
    resource_store_log(rs);
    return true;
}

void resource_store_log(const ResourceStore *rs) {
    if (rs->count == 0) {
        LOG_INFO("Resources: (none defined yet)");
        return;
    }
    /* Small fixed buffer built up manually rather than N separate log
       lines — one line per change, same as the old "wood=%d stone=%d"
       single-line format, just generalized to however many kinds a
       project actually has. */
    char buf[256];
    int  off = 0;
    for (int i = 0; i < rs->count && off < (int)sizeof(buf) - 32; i++) {
        int n = snprintf(buf + off, sizeof(buf) - (size_t)off, "%s%s=%d",
                          i > 0 ? "  " : "", rs->entries[i].name, rs->entries[i].amount);
        if (n > 0) off += n;
    }
    LOG_INFO("Resources: %s", buf);
}

/* ---------------------------------------------------------------------
   Save / load — see the format comment in simulation.h. */

#define DGES_MAGIC   "DGES"
#define DGES_VERSION 2u

bool simulation_save(const SimClock *clk, const ResourceStore *rs, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        LOG_ERROR("simulation_save: could not open '%s' for writing", path);
        return false;
    }

    unsigned int version = DGES_VERSION;
    unsigned int count   = (unsigned int)rs->count;

    bool ok = true;
    ok &= fwrite(DGES_MAGIC, 1, 4, f) == 4;
    ok &= fwrite(&version,          sizeof(version),          1, f) == 1;
    ok &= fwrite(&clk->elapsed,     sizeof(clk->elapsed),     1, f) == 1;
    ok &= fwrite(&clk->speed,       sizeof(clk->speed),       1, f) == 1;
    ok &= fwrite(&clk->saved_speed, sizeof(clk->saved_speed), 1, f) == 1;
    ok &= fwrite(&count, sizeof(count), 1, f) == 1;

    for (int i = 0; ok && i < rs->count; i++) {
        unsigned char name_len = (unsigned char)0;
        while (name_len < (unsigned char)(RESOURCE_NAME_MAX - 1) &&
               rs->entries[i].name[name_len]) name_len++;
        int32_t amount = (int32_t)rs->entries[i].amount;
        ok &= fwrite(&name_len, 1, 1, f) == 1;
        ok &= fwrite(rs->entries[i].name, 1, name_len, f) == name_len;
        ok &= fwrite(&amount, sizeof(amount), 1, f) == 1;
    }

    fclose(f);
    if (!ok) {
        LOG_ERROR("simulation_save: write error writing '%s'", path);
        return false;
    }
    LOG_INFO("Simulation state saved -> '%s' (elapsed=%.1fs, %d resource kinds)",
             path, clk->elapsed, rs->count);
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
    if (version < 1u || version > DGES_VERSION) {
        LOG_ERROR("simulation_load: '%s' has unsupported version %u (expected 1-%u)",
                   path, version, DGES_VERSION);
        fclose(f);
        return false;
    }

    SimClock loaded_clk;
    ResourceStore loaded_rs;
    resource_store_init(&loaded_rs);

    ok &= fread(&loaded_clk.elapsed,     sizeof(loaded_clk.elapsed),     1, f) == 1;
    ok &= fread(&loaded_clk.speed,       sizeof(loaded_clk.speed),       1, f) == 1;
    ok &= fread(&loaded_clk.saved_speed, sizeof(loaded_clk.saved_speed), 1, f) == 1;

    if (version == 1u) {
        /* v1 had a fixed int32 wood + int32 stone, no names at all —
           migrate into the new named store as "wood"/"stone" entries,
           same reasoning as world.c's v3->v4 Tileset migration: an old
           project's data keeps meaning what it used to mean instead of
           silently vanishing. New projects created after Phase 2B
           never go through this path. */
        int32_t wood = 0, stone = 0;
        ok &= fread(&wood,  sizeof(wood),  1, f) == 1;
        ok &= fread(&stone, sizeof(stone), 1, f) == 1;
        if (ok) {
            if (wood  != 0) resource_store_add(&loaded_rs, "wood",  (int)wood);
            if (stone != 0) resource_store_add(&loaded_rs, "stone", (int)stone);
            LOG_INFO("simulation_load: migrated version-1 wood/stone fields into "
                     "the named resource store");
        }
    } else {
        unsigned int count = 0;
        ok &= fread(&count, sizeof(count), 1, f) == 1;
        if (ok && count <= (unsigned int)RESOURCE_STORE_MAX_KINDS) {
            for (unsigned int i = 0; ok && i < count; i++) {
                unsigned char name_len = 0;
                char name_buf[RESOURCE_NAME_MAX];
                memset(name_buf, 0, sizeof name_buf);
                int32_t amount = 0;
                ok &= fread(&name_len, 1, 1, f) == 1;
                if (ok && name_len >= RESOURCE_NAME_MAX) ok = false;
                if (ok) ok &= fread(name_buf, 1, name_len, f) == name_len;
                ok &= fread(&amount, sizeof(amount), 1, f) == 1;
                if (ok) resource_store_add(&loaded_rs, name_buf, (int)amount);
            }
        } else if (ok) {
            ok = false;
        }
    }
    fclose(f);

    if (!ok) {
        LOG_ERROR("simulation_load: truncated/corrupt data in '%s'", path);
        return false;
    }

    *clk = loaded_clk;
    *rs  = loaded_rs;
    LOG_INFO("Simulation state loaded <- '%s' (elapsed=%.1fs, %d resource kinds)",
             path, clk->elapsed, rs->count);
    return true;
}
