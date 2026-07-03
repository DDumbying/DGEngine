#ifndef DGE_TILESET_H
#define DGE_TILESET_H

/*  Tileset — the project's own list of terrain types.

    This replaces the old TerrainType enum (grass/dirt/sand/water/stone,
    hardcoded into the engine with a hardcoded fallback color each). The
    engine has no business knowing what "grass" is — that's the project's
    content, not the engine's. A Tileset is just an ordered list of slots,
    each carrying:

      name      whatever the project calls it ("Grass", "Lava",
                "Floor A" — the engine never reads this string for
                behavior, it's display-only)
      sprite_id index into the project's SpriteAtlas. -1 means
                "no sprite assigned yet" — rendered as an honest
                missing-texture checker (see world_render()), not a
                guessed color. There is no built-in fallback palette;
                guessing what an unassigned tile "should" look like is
                exactly the kind of engine-presumes-meaning mistake
                this replaces.
      walkable  the ONE piece of meaning the engine actually needs,
                because pathfinding has to know it. Everything else
                about a tile — what it looks like, what it's "for" —
                is the project's business, not the engine's.

    Tile.type (see tile.h) is no longer an enum value — it's an index
    into the active project's Tileset. A brand new project's Tileset is
    empty (count == 0): you define your first tile type before you can
    paint anything, which is the honest state, not a quietly-provided
    default game.

    Slot removal is deliberately NOT supported yet. Tile.type values
    already painted into a World reference slots by index; removing a
    slot out from under existing tiles needs either a remap step or an
    explicit "these tiles are now undefined" decision, and that's real
    design work being left for later rather than papered over here.
    Today: add, rename, reassign sprite, reassign walkable. That's it. */

#include <stdbool.h>
#include <string.h>

#define TILESET_MAX_SLOTS 64
#define TILESET_NAME_MAX  32

/* Bounded copy that always NUL-terminates, unlike raw strncpy (which
   won't append a NUL if src is >= dst_size bytes — the exact case gcc's
   -Wstringop-truncation is warning about at every strncpy(name, ...)
   call in this file). Small enough to keep as a static inline here
   rather than pulling in a strlcpy dependency for one field. */
static inline void tileset_strcpy(char *dst, size_t dst_size, const char *src) {
    size_t i = 0;
    for (; i + 1 < dst_size && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
}

typedef struct {
    char name[TILESET_NAME_MAX];
    int  sprite_id;   /* -1 = unassigned, renders as missing-texture checker */
    bool walkable;
} TileSlot;

typedef struct {
    TileSlot slots[TILESET_MAX_SLOTS];
    int      count;   /* 0 for a brand new project — nothing defined yet */
} Tileset;

static inline void tileset_init(Tileset *ts) {
    memset(ts, 0, sizeof(*ts));
}

/* Returns the new slot's index, or -1 if the Tileset is full. The new
   slot starts with no sprite assigned (renders as missing-texture)
   and walkable=true, since "can't walk here" is a much more surprising
   default for a freshly-defined tile than "looks unfinished." */
static inline int tileset_add_slot(Tileset *ts, const char *name) {
    if (ts->count >= TILESET_MAX_SLOTS) return -1;
    int idx = ts->count++;
    TileSlot *s = &ts->slots[idx];
    memset(s, 0, sizeof(*s));
    tileset_strcpy(s->name, TILESET_NAME_MAX, name);
    s->sprite_id = -1;
    s->walkable  = true;
    return idx;
}

static inline bool tileset_slot_valid(const Tileset *ts, int index) {
    return index >= 0 && index < ts->count;
}

static inline void tileset_rename(Tileset *ts, int index, const char *name) {
    if (!tileset_slot_valid(ts, index)) return;
    tileset_strcpy(ts->slots[index].name, TILESET_NAME_MAX, name);
}

static inline void tileset_set_sprite(Tileset *ts, int index, int sprite_id) {
    if (!tileset_slot_valid(ts, index)) return;
    ts->slots[index].sprite_id = sprite_id;
}

static inline void tileset_set_walkable(Tileset *ts, int index, bool walkable) {
    if (!tileset_slot_valid(ts, index)) return;
    ts->slots[index].walkable = walkable;
}

/* The only "meaning" the engine itself needs from a tile. An
   out-of-range index (undefined slot, or empty Tileset) is treated as
   NOT walkable — refusing to path into ground that was never actually
   defined is the safe default, the same way an unassigned sprite shows
   as a checker rather than quietly standing in for grass. */
static inline bool tileset_is_walkable(const Tileset *ts, int index) {
    if (!tileset_slot_valid(ts, index)) return false;
    return ts->slots[index].walkable;
}

static inline int tileset_sprite_for(const Tileset *ts, int index) {
    if (!tileset_slot_valid(ts, index)) return -1;
    return ts->slots[index].sprite_id;
}

static inline const char *tileset_name_for(const Tileset *ts, int index) {
    if (!tileset_slot_valid(ts, index)) return "(undefined)";
    return ts->slots[index].name;
}

#endif /* DGE_TILESET_H */
