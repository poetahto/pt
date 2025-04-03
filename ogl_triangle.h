#ifndef OGL_TRIANGLE_H
#define OGL_TRIANGLE_H

typedef struct ogl_triangle {
    unsigned int program;
    unsigned int vao;
    unsigned int vbo;
} ogl_triangle;

void init_ogl_triangle(ogl_triangle* triangle);
void draw_ogl_triangle(const ogl_triangle* triangle);
void free_ogl_triangle(ogl_triangle* triangle);

#endif // OGL_TRIANGLE_H