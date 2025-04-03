#include "ogl_util.h"
#include "glad/glad.h"

static void ogl_message_callback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, GLchar const* message, void const* user_param);

void ogl_init(SDL_Window* window) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GLContext context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, context);

    gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress);

    int width, height;
    SDL_GetWindowSize(window, &width, &height);
    glViewport(0, 0, width, height);

    glEnable(GL_DEBUG_OUTPUT);
    glDebugMessageCallback(ogl_message_callback, NULL);
}

static void ogl_message_callback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, GLchar const* message, void const* user_param) {
	const char* src_str; 
	const char* type_str; 
	const char* severity_str; 

    switch (source) {
        case GL_DEBUG_SOURCE_API: src_str = "API";
        case GL_DEBUG_SOURCE_WINDOW_SYSTEM: src_str = "WINDOW SYSTEM";
        case GL_DEBUG_SOURCE_SHADER_COMPILER: src_str = "SHADER COMPILER";
        case GL_DEBUG_SOURCE_THIRD_PARTY: src_str = "THIRD PARTY";
        case GL_DEBUG_SOURCE_APPLICATION: src_str = "APPLICATION";
        case GL_DEBUG_SOURCE_OTHER: src_str = "OTHER";
    }

    switch (type) {
		case GL_DEBUG_TYPE_ERROR: type_str = "ERROR";
		case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: type_str = "DEPRECATED_BEHAVIOR";
		case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: type_str = "UNDEFINED_BEHAVIOR";
		case GL_DEBUG_TYPE_PORTABILITY: type_str = "PORTABILITY";
		case GL_DEBUG_TYPE_PERFORMANCE: type_str = "PERFORMANCE";
		case GL_DEBUG_TYPE_MARKER: type_str = "MARKER";
		case GL_DEBUG_TYPE_OTHER: type_str = "OTHER";
    }

    switch (severity) {
        case GL_DEBUG_SEVERITY_NOTIFICATION: severity_str = "NOTIFICATION";
        case GL_DEBUG_SEVERITY_LOW: severity_str = "LOW";
        case GL_DEBUG_SEVERITY_MEDIUM: severity_str = "MEDIUM";
        case GL_DEBUG_SEVERITY_HIGH: severity_str = "HIGH";
    }

    SDL_Log("[SOURCE:%s] [TYPE:%s] [SEVERITY:%s]\n\t%s", src_str, type_str, severity_str, message);
}