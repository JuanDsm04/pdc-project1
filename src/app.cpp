#include "app.hpp"

#include <cmath>
#include <cstdio>

#include "parallel.hpp"

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

// Canonical stone colors on independent world space orbits, used here to exercise the
// projection and reused once the stones themselves exist.
struct Glow {
    float r, g, b;
    float orbitRadius;
    float orbitSpeed;
    float phase;
    float bob;
};

const Glow kGlows[6] = {
    {0.20f, 0.45f, 1.00f, 0.80f, 0.42f, 0.0f, 0.35f},  // space
    {1.00f, 0.85f, 0.15f, 0.55f, 0.61f, 1.0f, 0.20f},  // mind
    {1.00f, 0.15f, 0.20f, 0.95f, 0.29f, 2.1f, 0.45f},  // reality
    {0.65f, 0.25f, 1.00f, 0.70f, 0.51f, 3.4f, 0.28f},  // power
    {0.25f, 1.00f, 0.40f, 0.45f, 0.73f, 4.2f, 0.15f},  // time
    {1.00f, 0.45f, 0.10f, 0.88f, 0.36f, 5.1f, 0.40f},  // soul
};

constexpr float kGlowWorldRadius = 0.16f;

// Stands in for the --particles flag until the CLI lands at step 15.
constexpr int      kDefaultParticleCount = 200000;
constexpr uint32_t kDefaultSeed = 1337u;

constexpr float kParticleBrightness = 0.9f;

// Distance the orbits are tuned around. Attenuation is expressed relative to it so that
// glows read as receding without the near ones saturating the whole frame.
constexpr float kReferenceDepth = 3.2f;

// World space radius of a single particle's splat, before projection.
constexpr float kParticleWorldRadius = 0.012f;

// Pixel radius is clamped instead of left to scale freely with 1/depth. Without a clamp a
// particle that drifts close to the camera would spend hundreds of pixels on one point,
// and the per particle cost has to stay bounded for the thread count to map to frame rate
// the way the assignment wants. The plan's 5x5 footprint target comes from this max.
constexpr float kMinSplatRadius = 1.0f;
constexpr float kMaxSplatRadius = 2.0f;

// Tighter than the placeholder glow sigma so a particle still reads as a point of light
// rather than a soft blob once it has more than a couple of pixels to work with.
constexpr float kSplatSigmaScale = 0.5f;

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
    m_camera.setViewport(m_width, m_height);
    m_particles.reset(kDefaultParticleCount, kDefaultSeed);
    m_projected.resize(kDefaultParticleCount);
    m_rasterizer.resize(m_width, m_height);

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
    m_camera.update(m_time);
    m_particles.integrate(static_cast<float>(dt));
}

// World to screen for every particle. Each iteration only reads particle i and writes
// m_projected[i], so there is no shared state between iterations and this is safe to
// parallelize directly, unlike the splat pass below.
void App::projectParticles() {
    const int count = m_particles.count();

    #pragma omp parallel for if(g_parallel) num_threads(g_threads) schedule(static)
    for (int i = 0; i < count; ++i) {
        m_projected[i] =
            m_camera.project({m_particles.px[i], m_particles.py[i], m_particles.pz[i]});
    }
}

// Reduces every particle's projection into a Splat: screen center, clamped footprint,
// gaussian falloff and color, with no framebuffer access at all. Each iteration writes
// only its own slot in m_splats, so this is safe to parallelize the same way
// projectParticles is. An invisible particle gets peak 0 rather than being skipped, so
// every thread's slice of the loop stays a fixed size range with no shared bookkeeping
// about which indices landed where; m_rasterizer treats peak 0 as nothing to draw.
void App::buildSplats() {
    const int count = m_particles.count();
    m_splats.resize(count);

    #pragma omp parallel for if(g_parallel) num_threads(g_threads) schedule(static)
    for (int i = 0; i < count; ++i) {
        const Projected& p = m_projected[i];
        Splat& s = m_splats[i];

        if (!p.visible) {
            s.peak = 0.0f;
            continue;
        }

        float pixelRadius = kParticleWorldRadius * p.scale;
        if (pixelRadius < kMinSplatRadius) pixelRadius = kMinSplatRadius;
        if (pixelRadius > kMaxSplatRadius) pixelRadius = kMaxSplatRadius;

        const float sigma = pixelRadius * kSplatSigmaScale;

        s.cx = static_cast<int>(p.x);
        s.cy = static_cast<int>(p.y);
        s.span = static_cast<int>(pixelRadius);
        s.invTwoSigmaSq = 1.0f / (2.0f * sigma * sigma);

        const float ratio = kReferenceDepth / p.depth;
        s.peak = kParticleBrightness * ratio * ratio;

        s.r = m_particles.cr[i];
        s.g = m_particles.cg[i];
        s.b = m_particles.cb[i];
    }
}

// Stand in for the particle splatting that arrives at step 6. Six HDR blobs on world
// space orbits, projected through the camera, are enough to show perspective working:
// they shrink and dim with distance and pass in front of and behind each other.
void App::drawPlaceholderGlows() {
    for (const Glow& glow : kGlows) {
        const float angle = static_cast<float>(m_time) * glow.orbitSpeed + glow.phase;
        const Vec3 world = {std::cos(angle) * glow.orbitRadius,
                            std::sin(angle * 1.7f + glow.phase) * glow.bob,
                            std::sin(angle) * glow.orbitRadius};

        const Projected p = m_camera.project(world);
        if (!p.visible) continue;

        const float pixelRadius = kGlowWorldRadius * p.scale;
        const int span = static_cast<int>(pixelRadius);
        if (span < 1) continue;

        const float sigma = pixelRadius / 3.0f;
        const float invTwoSigmaSq = 1.0f / (2.0f * sigma * sigma);

        const float ratio = kReferenceDepth / p.depth;
        const float attenuation = ratio * ratio;
        const float peak = 6.0f * (attenuation > 2.0f ? 2.0f : attenuation);

        const int cx = static_cast<int>(p.x);
        const int cy = static_cast<int>(p.y);

        for (int dy = -span; dy <= span; ++dy) {
            for (int dx = -span; dx <= span; ++dx) {
                const float distSq = static_cast<float>(dx * dx + dy * dy);
                const float intensity = std::exp(-distSq * invTwoSigmaSq) * peak;

                m_framebuffer.deposit(cx + dx, cy + dy, glow.r * intensity,
                                      glow.g * intensity, glow.b * intensity);
            }
        }
    }
}

void App::render() {
    m_framebuffer.clear();
    projectParticles();
    buildSplats();
    m_rasterizer.draw(m_splats, m_framebuffer);
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