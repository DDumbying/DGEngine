#include "renderer.h"

#include <glad/glad.h>
#include <string.h>
#include "shader.h"
#include "../core/log.h"
#include "../math/mat4.h"

/* ---- Batch geometry limits ---- */
#define MAX_QUADS   4096
#define VERTS_PER_QUAD 4
#define IDXS_PER_QUAD  6
#define FLOATS_PER_VERT 8   /* x y  r g b a  u v */

/* ---- Embedded GLSL sources ---- */
static const char *VERT_SRC =
    "#version 330 core\n"
    "layout(location=0) in vec2 a_pos;\n"
    "layout(location=1) in vec4 a_color;\n"
    "layout(location=2) in vec2 a_uv;\n"
    "uniform mat4 u_vp;\n"
    "out vec4 v_color;\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    gl_Position = u_vp * vec4(a_pos, 0.0, 1.0);\n"
    "    v_color = a_color;\n"
    "    v_uv    = a_uv;\n"
    "}\n";

static const char *FRAG_SRC =
    "#version 330 core\n"
    "in vec4 v_color;\n"
    "in vec2 v_uv;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    "    frag_color = v_color;\n"  /* texture support added in Phase 2 */
    "}\n";

/* ---- State ---- */
static Shader  s_shader;
static GLuint  s_vao, s_vbo, s_ebo;
static float   s_verts[MAX_QUADS * VERTS_PER_QUAD * FLOATS_PER_VERT];
static int     s_quad_count = 0;
static float   s_tile_w = 64.0f;
static float   s_tile_h = 32.0f;
static Mat4    s_vp;

/* ---- Internal ---- */
static void flush(void) {
    if (s_quad_count == 0) return;

    glBindVertexArray(s_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    s_quad_count * VERTS_PER_QUAD * FLOATS_PER_VERT * sizeof(float),
                    s_verts);

    glDrawElements(GL_TRIANGLES, s_quad_count * IDXS_PER_QUAD,
                   GL_UNSIGNED_INT, NULL);
    s_quad_count = 0;
}

/* ---- Public API ---- */

bool renderer_init(void) {
    if (!shader_create(&s_shader, VERT_SRC, FRAG_SRC))
        return false;

    /* Build index buffer — pattern is the same for every quad. */
    unsigned int indices[MAX_QUADS * IDXS_PER_QUAD];
    for (int i = 0; i < MAX_QUADS; i++) {
        unsigned int base = i * 4;
        indices[i*6+0] = base+0;
        indices[i*6+1] = base+1;
        indices[i*6+2] = base+2;
        indices[i*6+3] = base+2;
        indices[i*6+4] = base+3;
        indices[i*6+5] = base+0;
    }

    glGenVertexArrays(1, &s_vao);
    glGenBuffers(1, &s_vbo);
    glGenBuffers(1, &s_ebo);

    glBindVertexArray(s_vao);

    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 MAX_QUADS * VERTS_PER_QUAD * FLOATS_PER_VERT * sizeof(float),
                 NULL, GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    /* pos (vec2) */
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                          FLOATS_PER_VERT * sizeof(float), (void*)0);
    /* color (vec4) */
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE,
                          FLOATS_PER_VERT * sizeof(float), (void*)(2*sizeof(float)));
    /* uv (vec2) */
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE,
                          FLOATS_PER_VERT * sizeof(float), (void*)(6*sizeof(float)));

    glBindVertexArray(0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    LOG_INFO("Renderer initialised (max quads: %d)", MAX_QUADS);
    return true;
}

void renderer_shutdown(void) {
    shader_destroy(&s_shader);
    glDeleteVertexArrays(1, &s_vao);
    glDeleteBuffers(1, &s_vbo);
    glDeleteBuffers(1, &s_ebo);
}

void renderer_begin(const Camera *c) {
    s_vp         = camera_view_projection(c);
    s_quad_count = 0;
    glViewport(0, 0, c->viewport_w, c->viewport_h);
    shader_bind(&s_shader);
    shader_set_mat4(&s_shader, "u_vp", mat4_ptr(&s_vp));
}

void renderer_end(void) {
    flush();
    shader_unbind();
}

void renderer_clear(float r, float g, float b) {
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void renderer_draw_quad(float x, float y, float w, float h,
                        float r, float g, float b, float a) {
    if (s_quad_count >= MAX_QUADS) flush();

    float *v = s_verts + s_quad_count * VERTS_PER_QUAD * FLOATS_PER_VERT;

    /* bottom-left */
    v[ 0]=x;   v[ 1]=y;   v[ 2]=r; v[ 3]=g; v[ 4]=b; v[ 5]=a; v[ 6]=0; v[ 7]=0;
    /* bottom-right */
    v[ 8]=x+w; v[ 9]=y;   v[10]=r; v[11]=g; v[12]=b; v[13]=a; v[14]=1; v[15]=0;
    /* top-right */
    v[16]=x+w; v[17]=y+h; v[18]=r; v[19]=g; v[20]=b; v[21]=a; v[22]=1; v[23]=1;
    /* top-left */
    v[24]=x;   v[25]=y+h; v[26]=r; v[27]=g; v[28]=b; v[29]=a; v[30]=0; v[31]=1;

    s_quad_count++;
}

void renderer_set_tile_size(float tw, float th) {
    s_tile_w = tw;
    s_tile_h = th;
}

void renderer_get_tile_size(float *out_w, float *out_h) {
    *out_w = s_tile_w;
    *out_h = s_tile_h;
}

/*  Standard 2:1 isometric projection.
    Grid cell (gx, gy) -> screen-space diamond centre:
      screen_x = (gx - gy) * tile_w/2
      screen_y = (gx + gy) * tile_h/2

    Each tile is inset by GAP_PX so the dark background shows through
    as grid lines between adjacent tiles.                              */
void renderer_draw_iso_tile(int gx, int gy,
                            float r, float g, float b, float a) {
    float cx = (float)(gx - gy) * (s_tile_w * 0.5f);
    float cy = (float)(gx + gy) * (s_tile_h * 0.5f);

    if (s_quad_count >= MAX_QUADS) flush();

    const float GAP_PX = 1.5f;
    float hw = s_tile_w * 0.5f - GAP_PX;
    float hh = s_tile_h * 0.5f - GAP_PX * (s_tile_h / s_tile_w);

    float *v = s_verts + s_quad_count * VERTS_PER_QUAD * FLOATS_PER_VERT;

    /* Diamond corners: top, right, bottom, left */
    v[ 0]=cx;    v[ 1]=cy-hh; v[ 2]=r; v[ 3]=g; v[ 4]=b; v[ 5]=a; v[ 6]=0.5f; v[ 7]=0.0f;
    v[ 8]=cx+hw; v[ 9]=cy;    v[10]=r; v[11]=g; v[12]=b; v[13]=a; v[14]=1.0f;  v[15]=0.5f;
    v[16]=cx;    v[17]=cy+hh; v[18]=r; v[19]=g; v[20]=b; v[21]=a; v[22]=0.5f;  v[23]=1.0f;
    v[24]=cx-hw; v[25]=cy;    v[26]=r; v[27]=g; v[28]=b; v[29]=a; v[30]=0.0f;  v[31]=0.5f;

    s_quad_count++;
}

void renderer_draw_iso_grid(int cols, int rows) {
    /* Uniform green -- grid lines come from the GAP_PX inset above */
    const float R = 0.36f, G = 0.56f, B = 0.33f;
    for (int gy = 0; gy < rows; gy++)
        for (int gx = 0; gx < cols; gx++)
            renderer_draw_iso_tile(gx, gy, R, G, B, 1.0f);
}

void renderer_draw_iso_sprite(float gx, float gy, float w, float h,
                              float r, float g, float b, float a) {
    /* Same center formula as renderer_draw_iso_tile, but gx/gy are
       float here since this can be called with sub-tile positions. */
    float cx = (gx - gy) * (s_tile_w * 0.5f);
    float cy = (gx + gy) * (s_tile_h * 0.5f);

    /* World/screen space here is y-down (see camera.c): smaller y is
       visually higher on screen. Anchor the box's bottom edge to the
       tile's screen center (nudged toward the diamond's front-top tip)
       and have it extend toward smaller y so it reads as standing on
       the tile rather than growing downward into it. */
    float tile_surface_y = cy - (s_tile_h * 0.25f);
    float anchor_x = cx - w * 0.5f;
    float anchor_y = tile_surface_y - h;

    renderer_draw_quad(anchor_x, anchor_y, w, h, r, g, b, a);
}
