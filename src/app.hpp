#pragma once

#include <SDL2/SDL.h>

#include "camera.hpp"
#include "framebuffer.hpp"

class App {
public:
    bool init(int width, int height);
    void run();
    void shutdown();

private:
    void handleEvents();
    void update(double dt);
    void render();
    void drawPlaceholderGlows();

    SDL_Window*   m_window   = nullptr;
    SDL_Renderer* m_renderer = nullptr;
    SDL_Texture*  m_texture  = nullptr;

    Framebuffer m_framebuffer;
    Camera      m_camera;

    int    m_width   = 0;
    int    m_height  = 0;
    bool   m_running = false;
    double m_time    = 0.0;
};
