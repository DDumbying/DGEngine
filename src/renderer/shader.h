#ifndef DGE_SHADER_H
#define DGE_SHADER_H

#include <stdbool.h>
#include <glad/glad.h>

typedef struct {
    GLuint id;
} Shader;

/* Compile from source strings. Returns false on error. */
bool shader_create(Shader *s, const char *vert_src, const char *frag_src);
void shader_destroy(Shader *s);

void shader_bind(const Shader *s);
void shader_unbind(void);

/* Uniform setters */
void shader_set_int(const Shader *s, const char *name, int v);
void shader_set_float(const Shader *s, const char *name, float v);
void shader_set_vec2(const Shader *s, const char *name, float x, float y);
void shader_set_vec4(const Shader *s, const char *name, float x, float y, float z, float w);
void shader_set_mat4(const Shader *s, const char *name, const float *mat);

#endif /* DGE_SHADER_H */
