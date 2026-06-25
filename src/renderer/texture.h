#ifndef DGE_TEXTURE_H
#define DGE_TEXTURE_H

/*  GPU texture — loaded from a PNG/JPEG via stb_image and uploaded as
    a GL_RGBA8 texture. Dimensions are stored so callers can compute UV
    rects without a second GL round-trip.

    Phase E adds a SpriteAtlas on top of this: one Texture that holds
    many named sprites packed into a grid, looked up by SpriteId.     */

#include <stdbool.h>
#include <glad/glad.h>

typedef struct {
    GLuint id;
    int    width, height;
} Texture;

/*  Load from disk (PNG, JPEG, BMP — anything stb_image accepts).
    Returns false and leaves *t zeroed on failure. */
bool texture_load(Texture *t, const char *path);

/*  Upload raw RGBA8 pixel data directly — used by the programmatic
    fallback atlas so the sprite system works before any PNG files
    exist on disk. pixels must be width*height*4 bytes.               */
bool texture_from_pixels(Texture *t, int width, int height,
                          const unsigned char *pixels);

void texture_bind(const Texture *t, int unit);   /* glActiveTexture + bind */
void texture_unbind(int unit);
void texture_destroy(Texture *t);

#endif /* DGE_TEXTURE_H */
