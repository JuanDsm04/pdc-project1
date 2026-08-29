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

constexpr float kExposure = 1.0f;

// Canonical stone colors, used here to exercise the tone mapper and reused once the
// stones themselves exist.
struct Glow {
    float r, g, b;
    double phase;
};

const Glow kGlows[6] = {
    {0.20f, 0.45f, 1.00f, 0.0},  // space
    {1.00f, 0.85f, 0.15f, 1.0},  // mind
    {1.00f, 0.15f, 0.20f, 2.0},  // reality
    {0.65f, 0.25f, 1.00f, 3.0},  // power
    {0.25f, 1.00f, 0.40f, 4.0},  // time
    {1.00f, 0.45f, 0.10f, 5.0},  // soul
};

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

    m_texture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_ARGB8888,
                                  SDL_TEXTUREACCESS_STREAMING, m_width, m_height);
    if (!m_texture) {
        std::fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        return false;
    }

    m_framebuffer.resize(m_width, m_height);

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

// Stand in for the particle splatting that arrives at step 6. Six orbiting HDR blobs are
// enough to prove the buffer sums past white where they overlap and rolls back off again.
void App::drawPlaceholderGlows() {
    const float radius = 0.16f * static_cast<float>(m_height);
    const float sigma = radius / 3.0f;
    const float invTwoSigmaSq = 1.0f / (2.0f * sigma * sigma);
    const int span = static_cast<int>(radius);

    for (const Glow& glow : kGlows) {
        const double angle = m_time * 0.35 + glow.phase;
        const int cx = static_cast<int>(m_width * 0.5 + std::cos(angle) * m_width * 0.28);
        const int cy = static_cast<int>(m_height * 0.5 + std::sin(angle * 1.3) * m_height * 0.30);

        for (int dy = -span; dy <= span; ++dy) {
            for (int dx = -span; dx <= span; ++dx) {
                const float distSq = static_cast<float>(dx * dx + dy * dy);
                const float falloff = std::exp(-distSq * invTwoSigmaSq);
                const float intensity = falloff * 6.0f;

                m_framebuffer.deposit(cx + dx, cy + dy, glow.r * intensity,
                                      glow.g * intensity, glow.b * intensity);
            }
        }
    }
}

void App::render() {
    m_framebuffer.clear();
    drawPlaceholderGlows();
    m_framebuffer.tonemap(kExposure);

    SDL_UpdateTexture(m_texture, nullptr, m_framebuffer.pixels(), m_framebuffer.pitch());
    SDL_RenderCopy(m_renderer, m_texture, nullptr, nullptr);
    SDL_RenderPresent(m_renderer);
}

void App::shutdown() {
    if (m_texture) SDL_DestroyTexture(m_texture);
    if (m_renderer) SDL_DestroyRenderer(m_renderer);
    if (m_window) SDL_DestroyWindow(m_window);
    m_texture = nullptr;
    m_renderer = nullptr;
    m_window = nullptr;
    SDL_Quit();
}
