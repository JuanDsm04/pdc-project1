#pragma once

#include <SDL2/SDL.h>

class App {
public:
    bool init(int width, int height);
    void run();
    void shutdown();

private:
    void handleEvents();
    void update(double dt);
    void render();

    SDL_Window*   m_window   = nullptr;
    SDL_Renderer* m_renderer = nullptr;

    int    m_width   = 0;
    int    m_height  = 0;
    bool   m_running = false;
    double m_time    = 0.0;
};
