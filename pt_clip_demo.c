#define PT_CLIP_IMPLEMENTATION
#include "pt_clip.h"
#include "SDL3/SDL.h"
#include "ogl_util.h"

int main(int argc, char** argv) {
    // Setup window and openGL
    SDL_Init(SDL_INIT_VIDEO);
    SDL_WindowFlags flags = 0;
    // flags |= SDL_WINDOW_OPENGL;
    // flags |= SDL_WINDOW_HIDDEN;
    SDL_Renderer* renderer;
    SDL_Window* window;
    SDL_CreateWindowAndRenderer("pt_clip demo", 800, 600, flags, &window, &renderer);
    // SDL_ShowWindow(window);

    // Main loop
    int wants_to_quit = 0;

    while (!wants_to_quit) {
        // process events
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    wants_to_quit = 1;
                    break;
                // case SDL_EVENT_WINDOW_RESIZED:
                //     glViewport(0, 0, event.window.data1, event.window.data2);
                //     break;
            }
        }

        // render
        SDL_SetRenderDrawColorFloat(renderer, 0, 0, 0, 1);
        SDL_RenderClear(renderer);

        SDL_FRect rect;
        rect.x = 100;
        rect.y = 100;
        rect.w = 100;
        rect.h = 100;
        SDL_SetRenderDrawColorFloat(renderer, 1, 0, 0, 1);
        SDL_RenderRect(renderer, &rect);

        SDL_RenderPresent(renderer);
        // glClearColor(0, 0, 0, 1);
        // glClear(GL_COLOR_BUFFER_BIT);
        // SDL_GL_SwapWindow(window);
    }

    // Cleanup
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}