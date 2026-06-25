# DGEngine Assets

Place your sprite sheet here as `sprites.png`.

## Expected layout

A uniform grid of 32x32 pixel cells, 4 cells wide:

| Index | Cell (col, row) | Sprite                 |
|-------|-----------------|------------------------|
| 0     | (0, 0)          | TREE                   |
| 1     | (1, 0)          | ROCK                   |
| 2     | (2, 0)          | WORKER                 |
| 3     | (3, 0)          | CAMPFIRE (blueprint)   |
| 4     | (0, 1)          | CAMPFIRE (complete)    |

If `sprites.png` is missing, the engine uses a programmatic fallback
atlas (solid colored cells) so it runs correctly without any art files.

## Format

PNG, RGBA. Any art tool works — Aseprite, GIMP, Photoshop, etc.
Pixel art at 32x32 per cell looks best given the isometric tile size
(64x32 px tiles). Keep GL_NEAREST filtering in mind: no anti-aliasing,
pixel edges stay sharp.
