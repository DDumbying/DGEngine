#include <stdio.h>
#include <stdbool.h>

#include <SDL2/SDL.h>

#include <glad/glad.h>

int main(void)
{
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        printf("Failed to initialize SDL\n");
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);

    SDL_Window* window = SDL_CreateWindow(
        "DGEngine",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        1280,
        720,
        SDL_WINDOW_OPENGL
    );

    if (!window)
    {
        printf("Failed to create window\n");
        return 1;
    }

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);

    if (!gl_context)
    {
        printf("Failed to create OpenGL context\n");
        return 1;
    }

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress))
    {
        printf("Failed to initialize GLAD\n");
        return 1;
    }

    printf("OpenGL Version: %s\n", glGetString(GL_VERSION));

    bool running = true;

    while (running)
    {
        SDL_Event event;

        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
            {
                running = false;
            }
        }

        glClearColor(0.05f, 0.05f, 0.05f, 1.0f);

        glClear(GL_COLOR_BUFFER_BIT);

        SDL_GL_SwapWindow(window);
    }

    SDL_GL_DeleteContext(gl_context);

    SDL_DestroyWindow(window);

    SDL_Quit();

    return 0;
}
