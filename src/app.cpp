#include "app.hpp"

#include <cmath>
#include <cstdio>

namespace {

// Physics advances in fixed increments so the simulation stays stable and reproducible
// regardless of how fast the machine renders. Rendering still happens once per loop.
constexpr double kFixedStep = 1.0 / 120.0;

// Caps how much simulated time a single slow frame can request, so that a stall does not
// trigger a burst of catch-up steps that stalls the loop further.
constexpr double kMaxFrameTime = 0.25;

constexpr int kMinWidth  = 640;
constexpr int kMinHeight = 480;

}  // namespace

bool App::init(int width, int height) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    m_width  = width  < kMinWidth  ? kMinWidth  : width;
    m_height = height < kMinHeight ? kMinHeight : height;

    m_window = SDL_CreateWindow("Infinity Gauntlet", SDL_WINDOWPOS_CENTERED,
                                SDL_WINDOWPOS_CENTERED, m_width, m_height,
                                SDL_WINDOW_SHOWN);
    if (!m_window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    m_renderer = SDL_CreateRenderer(
        m_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!m_renderer) {
        std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return false;
    }

    m_running = true;
    return true;
}

void App::run() {
    const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
    Uint64 previous = SDL_GetPerformanceCounter();
    double accumulator = 0.0;

    while (m_running) {
        const Uint64 now = SDL_GetPerformanceCounter();
        double frameTime = static_cast<double>(now - previous) / freq;
        previous = now;

        if (frameTime > kMaxFrameTime) frameTime = kMaxFrameTime;
        accumulator += frameTime;

        handleEvents();

        while (accumulator >= kFixedStep) {
            update(kFixedStep);
            accumulator -= kFixedStep;
        }

        render();
    }
}

void App::handleEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                m_running = false;
                break;
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_ESCAPE) m_running = false;
                break;
            default:
                break;
        }
    }
}

void App::update(double dt) {
    m_time += dt;
}

void App::render() {
    // Placeholder until the accumulation framebuffer lands. The pulse is here so that a
    // running loop is visually distinguishable from a frozen one.
    const double pulse = 0.5 + 0.5 * std::sin(m_time);
    const Uint8 level = static_cast<Uint8>(8.0 + 20.0 * pulse);

    SDL_SetRenderDrawColor(m_renderer, level / 3, level / 4, level, 255);
    SDL_RenderClear(m_renderer);
    SDL_RenderPresent(m_renderer);
}

void App::shutdown() {
    if (m_renderer) SDL_DestroyRenderer(m_renderer);
    if (m_window) SDL_DestroyWindow(m_window);
    m_renderer = nullptr;
    m_window = nullptr;
    SDL_Quit();
}
