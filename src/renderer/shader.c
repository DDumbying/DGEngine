#include "shader.h"

#include <stdio.h>
#include <stdlib.h>
#include "../core/log.h"

static GLuint compile_stage(GLenum type, const char *src) {
    GLuint stage = glCreateShader(type);
    glShaderSource(stage, 1, &src, NULL);
    glCompileShader(stage);

    GLint ok;
    glGetShaderiv(stage, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[512];
        glGetShaderInfoLog(stage, sizeof(buf), NULL, buf);
        LOG_ERROR("Shader compile error (%s):\n%s",
                  type == GL_VERTEX_SHADER ? "vert" : "frag", buf);
        glDeleteShader(stage);
        return 0;
    }
    return stage;
}

bool shader_create(Shader *s, const char *vert_src, const char *frag_src) {
    GLuint vert = compile_stage(GL_VERTEX_SHADER,   vert_src);
    GLuint frag = compile_stage(GL_FRAGMENT_SHADER, frag_src);

    if (!vert || !frag) {
        if (vert) glDeleteShader(vert);
        if (frag) glDeleteShader(frag);
        return false;
    }

    s->id = glCreateProgram();
    glAttachShader(s->id, vert);
    glAttachShader(s->id, frag);
    glLinkProgram(s->id);

    GLint ok;
    glGetProgramiv(s->id, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[512];
        glGetProgramInfoLog(s->id, sizeof(buf), NULL, buf);
        LOG_ERROR("Shader link error:\n%s", buf);
        glDeleteProgram(s->id);
        glDeleteShader(vert);
        glDeleteShader(frag);
        return false;
    }

    glDeleteShader(vert);
    glDeleteShader(frag);
    LOG_INFO("Shader program created (id=%u)", s->id);
    return true;
}

void shader_destroy(Shader *s) {
    glDeleteProgram(s->id);
    s->id = 0;
}

void shader_bind(const Shader *s)   { glUseProgram(s->id); }
void shader_unbind(void)            { glUseProgram(0); }

static GLint loc(const Shader *s, const char *name) {
    GLint l = glGetUniformLocation(s->id, name);
    if (l < 0) LOG_WARN("Uniform '%s' not found in shader %u", name, s->id);
    return l;
}

void shader_set_int(const Shader *s, const char *n, int v)                           { glUniform1i(loc(s,n), v); }
void shader_set_float(const Shader *s, const char *n, float v)                       { glUniform1f(loc(s,n), v); }
void shader_set_vec2(const Shader *s, const char *n, float x, float y)               { glUniform2f(loc(s,n), x, y); }
void shader_set_vec4(const Shader *s, const char *n, float x, float y, float z, float w) { glUniform4f(loc(s,n), x,y,z,w); }
void shader_set_mat4(const Shader *s, const char *n, const float *m)                 { glUniformMatrix4fv(loc(s,n), 1, GL_FALSE, m); }
