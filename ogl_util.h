#ifndef OGL_UTIL_H
#define OGL_UTIL_H

#include "SDL3/SDL.h"
#include "glad/glad.h"

void ogl_init(SDL_Window* window);
void ogl_free();
void ogl_draw_triangle();

#endif // OGL_UTIL_H