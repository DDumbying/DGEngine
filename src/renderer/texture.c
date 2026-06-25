#define STB_IMAGE_IMPLEMENTATION
#include "../../external/stb/stb_image.h"

#include "texture.h"
#include "../core/log.h"
#include <string.h>

bool texture_load(Texture *t, const char *path) {
    memset(t, 0, sizeof(*t));
    stbi_set_flip_vertically_on_load(0);   /* y-down matches our screen space */

    int w, h, channels;
    unsigned char *data = stbi_load(path, &w, &h, &channels, 4);
    if (!data) {
        LOG_ERROR("texture_load: failed to load '%s': %s", path, stbi_failure_reason());
        return false;
    }
    bool ok = texture_from_pixels(t, w, h, data);
    stbi_image_free(data);
    if (ok) LOG_INFO("texture_load: '%s' (%dx%d)", path, w, h);
    return ok;
}

bool texture_from_pixels(Texture *t, int width, int height,
                          const unsigned char *pixels) {
    memset(t, 0, sizeof(*t));
    glGenTextures(1, &t->id);
    glBindTexture(GL_TEXTURE_2D, t->id);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    glBindTexture(GL_TEXTURE_2D, 0);
    t->width  = width;
    t->height = height;
    return true;
}

void texture_bind(const Texture *t, int unit) {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, t->id);
}

void texture_unbind(int unit) {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void texture_destroy(Texture *t) {
    if (t->id) glDeleteTextures(1, &t->id);
    memset(t, 0, sizeof(*t));
}
