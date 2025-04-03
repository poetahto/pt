#include "ogl_triangle.h"
#include <GL/gl.h>
#include <stdlib.h>
#include <stddef.h>

typedef struct triangle_vertex {
    float position[3];
    float color[3];
} triangle_vertex;

void init_ogl_triangle(ogl_triangle* triangle) {
    const char* triangle_vs_source = 
        "#version 460 core\n"
        "layout (location = 0) in vec3 aPos;\n"
        "layout (location = 1) in vec3 aColor;\n"
        "void main()\n"
        "{\n"
        "  gl_Position = vec4(aPos.x, aPos.y, aPos.z, 1.0);\n"
        "}\0";
    GLuint triangle_vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(triangle_vs, 1, &triangle_vs_source, NULL);
    glCompileShader(triangle_vs);

    const char* triangle_fs_source = 
        "#version 460 core\n"
        "out vec4 FragColor;\n"
        "void main()\n"
        "{\n"
        "   FragColor = vec4(0, 1, 0, 1);"
        "}\0";
    GLuint triangle_fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(triangle_fs, 1, &triangle_fs_source, NULL);
    glCompileShader(triangle_fs);

    GLuint triangle_program = glCreateProgram();
    glAttachShader(triangle_program, triangle_fs);
    glAttachShader(triangle_program, triangle_vs);
    glLinkProgram(triangle_program);

    glDeleteShader(triangle_fs);
    glDeleteShader(triangle_vs);

    GLuint triangle_vao;
    glCreateVertexArrays(1, &triangle_vao);

    glEnableVertexArrayAttrib(triangle_vao, 0);
    glVertexArrayAttribBinding(triangle_vao, 0, 0);
    glVertexArrayAttribFormat(triangle_vao, 0, 3, GL_FLOAT, GL_FALSE, offsetof(triangle_vertex, position));

    glEnableVertexArrayAttrib(triangle_vao, 1);
    glVertexArrayAttribFormat(triangle_vao, 1, 3, GL_FLOAT, GL_FALSE, offsetof(triangle_vertex, color));
    glVertexArrayAttribBinding(triangle_vao, 1, 0);

    triangle_vertex vertices[] = {
        {{-0.5, 0.0, 0.0}, {1, 0, 0}},
        {{ 0.0, 0.5, 0.0}, {0, 1, 0}},
        {{ 0.5, 0.0, 0.0}, {0, 0, 1}}
    };

    GLuint vertex_buffer;
    glCreateBuffers(1, &vertex_buffer);
    glNamedBufferStorage(vertex_buffer, sizeof(triangle_vertex)*3, vertices, GL_DYNAMIC_STORAGE_BIT);
    glVertexArrayVertexBuffer(triangle_vao, 0, vertex_buffer, 0, sizeof(triangle_vertex));
}

void draw_ogl_triangle(const ogl_triangle* triangle) {
    glUseProgram(triangle->program);
    glBindVertexArray(triangle->vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void free_ogl_triangle(ogl_triangle* triangle) {
    glDeleteProgram(triangle->program);
    glDeleteBuffers(1, &triangle->vbo);
    glDeleteVertexArrays(1, &triangle->vao);
}