#ifndef DGE_TEXTURE_H
#define DGE_TEXTURE_H

/* Texture loading will be implemented in Phase 2 (stb_image).
   The struct is defined here so renderer.h can reference it later. */
typedef struct {
    unsigned int id;
    int width, height;
} Texture;

#endif /* DGE_TEXTURE_H */
