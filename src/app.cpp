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

constexpr float kGlowWorldRadius = 0.16f;

// Bounding box for a stone's splat has to cover the widest bulge stoneShapeRadius can
// return (clamped to 1.75 there), or a facet sticking out past a plain circle's radius
// would get clipped by a loop sized for the unfaceted case.
constexpr float kShapeMaxMultiplier = 1.75f;

// Soft bloom just outside a stone's faceted edge, weaker than the core and only reaching a
// third of a pixel radius further out, so it reads as light bleeding off the gem rather
// than blurring the silhouette away.
constexpr float kGlowHaloStrength = 0.35f;
constexpr float kGlowHaloSigmaScale = 0.35f;

// Stands in for the --particles flag until the CLI lands at step 15.
constexpr int      kDefaultParticleCount = 200;
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

    if (!m_hud.init()) return false;

    m_running = true;
    return true;
}

void App::run() {
    const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
    Uint64 previous = SDL_GetPerformanceCounter();
    double accumulator = 0.0;

    while (m_running) {
        m_timer.beginFrame();

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

        m_timer.endFrame();
        m_hud.update(m_timer);
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

    m_timer.begin(Stage::Forces);
    m_stones.update(m_time, static_cast<float>(dt));
    m_stones.applyForces(m_particles, static_cast<float>(dt));
    m_timer.end(Stage::Forces);

    m_timer.begin(Stage::Integrate);
    m_particles.integrate(static_cast<float>(dt));
    m_timer.end(Stage::Integrate);
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

// Draws every stone's own glowing body at its current position. All six still share this
// one rendering pass regardless of whether they have real physics or a real silhouette
// yet: a stone's shape function returns a plain circle until stoneShapeRadius has facets
// to sum, so unfaceted stones (Space, Time, Reality, Mind for now) render exactly as
// before. Soul's glow brightens with how many particles it currently holds, so the
// capture and release cycle its physics drives is visible even without a HUD readout of
// the count.
void App::drawStones() {
    for (const Stone& stone : m_stones.all()) {
        const Projected p = m_camera.project(stone.position);
        if (!p.visible) continue;

        const float pixelRadius = kGlowWorldRadius * p.scale;
        const float haloSigma = pixelRadius * kGlowHaloSigmaScale;
        const int span =
            static_cast<int>(pixelRadius * kShapeMaxMultiplier + haloSigma * 3.0f);
        if (span < 1) continue;

        const float invHaloTwoSigmaSq = 1.0f / (2.0f * haloSigma * haloSigma);

        const float ratio = kReferenceDepth / p.depth;
        const float attenuation = ratio * ratio;
        float peak = 6.0f * (attenuation > 2.0f ? 2.0f : attenuation);

        if (stone.kind == StoneKind::Soul) {
            const float fill = static_cast<float>(m_stones.soulCapturedCount()) /
                               static_cast<float>(m_particles.count());
            peak *= 1.0f + 2.0f * fill;
        }

        const int cx = static_cast<int>(p.x);
        const int cy = static_cast<int>(p.y);

        for (int dy = -span; dy <= span; ++dy) {
            for (int dx = -span; dx <= span; ++dx) {
                const float fx = static_cast<float>(dx);
                const float fy = static_cast<float>(dy);
                const float r = std::sqrt(fx * fx + fy * fy);
                const float angle = std::atan2(fy, fx);

                const float shapeMul = stoneShapeRadius(stone, angle);
                const float shapeRadius = pixelRadius * shapeMul;

                float intensity;
                if (r <= shapeRadius) {
                    // Same falloff shape the old plain circle used (sigma = pixelRadius/3
                    // there), so an unfaceted stone renders pixel identical to before this
                    // change. Bulges catch more brightness than valleys, the way a real
                    // facet angled toward the light does.
                    const float t = shapeRadius > 1e-4f ? r / shapeRadius : 0.0f;
                    const float core = std::exp(-t * t * 4.5f);
                    const float facetShade = 1.0f + 0.35f * (shapeMul - 1.0f);
                    intensity = peak * core * facetShade;
                } else {
                    const float beyond = r - shapeRadius;
                    intensity = peak * kGlowHaloStrength *
                               std::exp(-(beyond * beyond) * invHaloTwoSigmaSq);
                }

                m_framebuffer.deposit(cx + dx, cy + dy, stone.r * intensity,
                                      stone.g * intensity, stone.b * intensity);
            }
        }
    }
}

void App::render() {
    m_framebuffer.clear();

    m_timer.begin(Stage::Project);
    projectParticles();
    m_timer.end(Stage::Project);

    m_timer.begin(Stage::BuildSplats);
    buildSplats();
    m_timer.end(Stage::BuildSplats);

    m_timer.begin(Stage::Bin);
    m_rasterizer.bin(m_splats);
    m_timer.end(Stage::Bin);

    m_timer.begin(Stage::Raster);
    m_rasterizer.rasterizeTiles(m_framebuffer);
    m_timer.end(Stage::Raster);

    m_timer.begin(Stage::Glows);
    drawStones();
    m_timer.end(Stage::Glows);

    m_timer.begin(Stage::Tonemap);
    m_framebuffer.tonemap(kExposure);
    m_timer.end(Stage::Tonemap);

    m_timer.begin(Stage::Present);
    SDL_UpdateTexture(m_texture, nullptr, m_framebuffer.pixels(), m_framebuffer.pitch());
    SDL_RenderCopy(m_renderer, m_texture, nullptr, nullptr);
    m_hud.render(m_renderer);
    SDL_RenderPresent(m_renderer);
    m_timer.end(Stage::Present);
}

void App::shutdown() {
    m_hud.shutdown();
    if (m_texture) SDL_DestroyTexture(m_texture);
    if (m_renderer) SDL_DestroyRenderer(m_renderer);
    if (m_window) SDL_DestroyWindow(m_window);
    m_texture = nullptr;
    m_renderer = nullptr;
    m_window = nullptr;
    SDL_Quit();
}