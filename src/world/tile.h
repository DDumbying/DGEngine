#ifndef DGE_TILE_H
#define DGE_TILE_H

/*  Terrain types.
    Each has a base color (used until textures land) and a movement cost
    used by pathfinding in Phase 6. Add new types at the end — the integer
    value is what gets written to save files. */
typedef enum {
    TERRAIN_GRASS = 0,
    TERRAIN_DIRT,
    TERRAIN_SAND,
    TERRAIN_WATER,
    TERRAIN_STONE,
    TERRAIN_COUNT
} TerrainType;

typedef struct {
    TerrainType type;
    float height;      /* reserved for Phase 7 (terrain layers / elevation) */
    unsigned char variant; /* 0-3, picks a shade so flat color fields don't look uniform */
} Tile;

typedef struct {
    float r, g, b;
} TileColor;

/* Base color per terrain type. */
static inline TileColor tile_base_color(TerrainType t) {
    switch (t) {
        case TERRAIN_GRASS: return (TileColor){0.36f, 0.56f, 0.33f};
        case TERRAIN_DIRT:  return (TileColor){0.50f, 0.38f, 0.27f};
        case TERRAIN_SAND:  return (TileColor){0.76f, 0.70f, 0.50f};
        case TERRAIN_WATER: return (TileColor){0.22f, 0.42f, 0.62f};
        case TERRAIN_STONE: return (TileColor){0.45f, 0.45f, 0.47f};
        default:            return (TileColor){1.0f, 0.0f, 1.0f}; /* unmissable error color */
    }
}

/* Whether an entity can walk on this terrain. Used by Phase 6 pathfinding. */
static inline int tile_is_walkable(TerrainType t) {
    return t != TERRAIN_WATER;
}

#endif /* DGE_TILE_H */
