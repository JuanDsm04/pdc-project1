#pragma once

#include <vector>

#include <SDL2/SDL.h>

#include "camera.hpp"
#include "framebuffer.hpp"
#include "hud.hpp"
#include "particles.hpp"
#include "raster.hpp"
#include "timing.hpp"

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
    void buildSplats();
    void drawPlaceholderGlows();

    SDL_Window*   m_window   = nullptr;
    SDL_Renderer* m_renderer = nullptr;
    SDL_Texture*  m_texture  = nullptr;

    Framebuffer    m_framebuffer;
    Camera         m_camera;
    ParticleSystem m_particles;
    Rasterizer     m_rasterizer;
    Hud            m_hud;
    FrameTimer     m_timer;

    // Filled by projectParticles, one entry per particle, consumed by buildSplats.
    // Splitting the projection out of the splat loop is what lets the projection run in
    // parallel: it only computes into this array, it never touches the framebuffer.
    std::vector<Projected> m_projected;

    // Filled by buildSplats, one entry per particle, consumed by m_rasterizer. Building
    // this is parallel for the same reason projectParticles is: each particle only ever
    // writes its own slot. Handing the framebuffer writes to the rasterizer instead of
    // doing them here is what makes that safe now, where before it was not.
    std::vector<Splat> m_splats;

    int    m_width   = 0;
    int    m_height  = 0;
    bool   m_running = false;
    double m_time    = 0.0;
};