#include "asset_library.h"

#include <stdio.h>
#include <string.h>
#include "../core/log.h"

void asset_library_init(AssetLibrary *lib) {
    memset(lib, 0, sizeof(*lib));
}

void asset_library_destroy(AssetLibrary *lib) {
    for (int i = 0; i < lib->count; i++)
        if (lib->assets[i].loaded)
            texture_destroy(&lib->assets[i].tex);
    memset(lib, 0, sizeof(*lib));
}

bool asset_library_is_asset_id(int sprite_id) {
    return sprite_id >= ASSET_ID_BASE;
}

static ImportedAsset *find_by_name(AssetLibrary *lib, const char *name) {
    for (int i = 0; i < lib->count; i++)
        if (strncmp(lib->assets[i].name, name, ASSET_NAME_MAX) == 0)
            return &lib->assets[i];
    return NULL;
}

int asset_library_import(AssetLibrary *lib, const char *path, const char *name) {
    if (!name || !name[0]) {
        LOG_WARN("asset_library_import: a name is required");
        return -1;
    }

    ImportedAsset *slot = find_by_name(lib, name);
    if (slot) {
        /* Re-importing under an existing name updates it in place --
           same sprite_id as before, so anything already referencing
           this name by id doesn't need to re-resolve. */
        if (slot->loaded) texture_destroy(&slot->tex);
    } else {
        if (lib->count >= ASSET_LIBRARY_MAX) {
            LOG_WARN("asset_library_import: library full (%d assets)", ASSET_LIBRARY_MAX);
            return -1;
        }
        slot = &lib->assets[lib->count++];
        memset(slot, 0, sizeof(*slot));
    }

    if (!texture_load(&slot->tex, path)) {
        LOG_WARN("asset_library_import: failed to load '%s'", path);
        slot->loaded = false;
        return -1;
    }

    snprintf(slot->name, sizeof(slot->name), "%s", name);
    snprintf(slot->path, sizeof(slot->path), "%s", path);
    slot->loaded = true;

    int index = (int)(slot - lib->assets);
    LOG_INFO("Asset imported: '%s' <- '%s' (%dx%d, id=%d)",
             name, path, slot->tex.width, slot->tex.height, ASSET_ID_BASE + index);
    return ASSET_ID_BASE + index;
}

void asset_library_remove(AssetLibrary *lib, const char *name) {
    for (int i = 0; i < lib->count; i++) {
        if (strncmp(lib->assets[i].name, name, ASSET_NAME_MAX) != 0) continue;
        if (lib->assets[i].loaded) texture_destroy(&lib->assets[i].tex);
        /* Shift the tail down rather than leaving a hole -- this DOES
           change the removed-and-shifted entries' sprite_ids, which is
           an accepted sharp edge (same one sprites_tab.c's grid ids
           would have if it ever supported deleting a cell): anything
           that cached an id across a remove needs to re-resolve by
           name. Nothing in this codebase caches ids long-term except
           RenderableComponent on already-placed entities, which is why
           ObjectDef stores sprite *names*, not ids -- re-resolution
           already happens every time an object is (re)spawned. */
        for (int j = i; j < lib->count - 1; j++)
            lib->assets[j] = lib->assets[j + 1];
        lib->count--;
        LOG_INFO("Asset removed: '%s'", name);
        return;
    }
}

int asset_library_find_id(const AssetLibrary *lib, const char *name) {
    for (int i = 0; i < lib->count; i++)
        if (lib->assets[i].loaded && strncmp(lib->assets[i].name, name, ASSET_NAME_MAX) == 0)
            return ASSET_ID_BASE + i;
    return -1;
}

const char *asset_library_get_name(const AssetLibrary *lib, int sprite_id) {
    if (!asset_library_is_asset_id(sprite_id)) return "";
    int i = sprite_id - ASSET_ID_BASE;
    if (i < 0 || i >= lib->count || !lib->assets[i].loaded) return "";
    return lib->assets[i].name;
}

const Texture *asset_library_get_texture(const AssetLibrary *lib, int sprite_id) {
    if (!asset_library_is_asset_id(sprite_id)) return NULL;
    int i = sprite_id - ASSET_ID_BASE;
    if (i < 0 || i >= lib->count || !lib->assets[i].loaded) return NULL;
    return &lib->assets[i].tex;
}

void asset_library_save_meta(const AssetLibrary *lib) {
    FILE *f = fopen("assets/imported.meta", "w");
    if (!f) { LOG_WARN("asset_library_save_meta: cannot write assets/imported.meta"); return; }
    for (int i = 0; i < lib->count; i++)
        if (lib->assets[i].loaded)
            fprintf(f, "%s=%s\n", lib->assets[i].name, lib->assets[i].path);
    fclose(f);
    LOG_INFO("Imported assets meta saved (%d entries).", lib->count);
}

void asset_library_load_meta(AssetLibrary *lib) {
    FILE *f = fopen("assets/imported.meta", "r");
    if (!f) return; /* no imports yet -- not an error */

    char line[ASSET_NAME_MAX + ASSET_PATH_MAX + 2];
    int loaded = 0;
    while (fgets(line, sizeof line, f)) {
        char name[ASSET_NAME_MAX], path[ASSET_PATH_MAX];
        char *eq = strchr(line, '=');
        if (!eq) continue;
        size_t name_len = (size_t)(eq - line);
        if (name_len == 0 || name_len >= sizeof(name)) continue;
        memcpy(name, line, name_len);
        name[name_len] = '\0';

        char *path_src = eq + 1;
        size_t path_len = strlen(path_src);
        while (path_len > 0 && (path_src[path_len-1] == '\n' || path_src[path_len-1] == '\r'))
            path_len--;
        if (path_len == 0 || path_len >= sizeof(path)) continue;
        memcpy(path, path_src, path_len);
        path[path_len] = '\0';

        if (asset_library_import(lib, path, name) >= 0) loaded++;
    }
    fclose(f);
    LOG_INFO("Imported assets reloaded: %d.", loaded);
}
