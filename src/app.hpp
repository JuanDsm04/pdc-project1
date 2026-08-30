#pragma once

#include <vector>

#include <SDL2/SDL.h>

#include "camera.hpp"
#include "framebuffer.hpp"
#include "particles.hpp"

class App {
public:
    bool init(int width, int height);
    void run();
    void shutdown();

private:
    void handleEvents();
    void update(double dt);
    void render();
    void projectParticles();
    void splatParticles();
    void drawPlaceholderGlows();

    SDL_Window*   m_window   = nullptr;
    SDL_Renderer* m_renderer = nullptr;
    SDL_Texture*  m_texture  = nullptr;

    Framebuffer    m_framebuffer;
    Camera         m_camera;
    ParticleSystem m_particles;

    // Filled by projectParticles, one entry per particle, consumed by splatParticles.
    // Splitting the projection out of the splat loop is what lets the projection run in
    // parallel: it only computes into this array, it never touches the framebuffer.
    std::vector<Projected> m_projected;

    int    m_width   = 0;
    int    m_height  = 0;
    bool   m_running = false;
    double m_time    = 0.0;
};