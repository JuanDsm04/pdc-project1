#include "app.hpp"

#include <algorithm>
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

// Covers Soul's tall tip plus the soft halo. Authored outline coordinates stay below 1.4.
constexpr float kShapeMaxMultiplier = 1.42f;

// Soft bloom just outside a stone's faceted edge, weaker than the core and only reaching a
// third of a pixel radius further out, so it reads as light bleeding off the gem rather
// than blurring the silhouette away.
constexpr float kGlowHaloStrength = 0.18f;
constexpr float kGlowHaloSigmaScale = 0.24f;

// Stands in for the --particles flag until the CLI lands at step 15.
constexpr int      kDefaultParticleCount = 1000;
constexpr uint32_t kDefaultSeed = 1337u;

constexpr float kParticleBrightness = 0.9f;

constexpr float kPortalBrightness = 2.6f;

// How much the two wormhole mouths brighten at full throughput. Both use the same figure,
// because them flaring in step is the cue that links the particles vanishing at one to the
// particles appearing at the other.
constexpr float kPortalFlare = 2.4f;
constexpr float kPortalColor[3] = {0.20f, 0.45f, 1.00f};

// How much brighter the Time stone burns during a rewind.
constexpr float kRewindFlare = 2.2f;

constexpr float kCollisionRingBrightness = 3.1f;
constexpr float kCollisionSparkBrightness = 3.8f;
constexpr float kCollisionFlashStrength = 0.07f;

// Distance the orbits are tuned around. Attenuation is expressed relative to it so that
// glows read as receding without the near ones saturating the whole frame.
constexpr float kReferenceDepth = 5.4f;

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

    // Cells a little wider than a particle is likely to travel in one step, so a flock's
    // neighbourhood is genuinely covered by the 27 cells Mind reads.
    // The grid now has to span the whole ring, not one box. Most of it is the empty gaps
    // between globes, which costs a walk over empty cells but keeps a single flat index.
    m_grid.configure(kRingExtent, 28);

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
    m_camera.update(m_time, m_stones.gather());

    // The grid is rebuilt before the forces because Mind reads it and nothing writes it,
    // which is what lets the whole force pass stay one parallel loop over particles.
    m_timer.begin(Stage::Grid);
    m_grid.build(m_particles);
    m_timer.end(Stage::Grid);

    m_timer.begin(Stage::Forces);
    m_stones.update(m_time, static_cast<float>(dt), m_particles);
    m_stones.applyForces(m_particles, m_grid, static_cast<float>(dt));
    m_timer.end(Stage::Forces);

    m_timer.begin(Stage::Integrate);
    m_particles.integrate(static_cast<float>(dt), m_stones.chamberWallsUp());
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
// yet: a stone's shape function returns a plain circle until it has an authored outline,
// so unfinished stones (Space, Time, Reality, Mind for now) render exactly as
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

        // Space flares in step with the exit ring whenever the wormhole is actually moving
        // particles, so the two mouths read as one connected event.
        if (stone.kind == StoneKind::Space) {
            peak *= 1.0f + kPortalFlare * m_stones.spaceActivity();
        }

        // Time flares while it runs its bubble backwards, so the reversal has a visible
        // cause on screen instead of particles inexplicably retracing their paths.
        if (stone.kind == StoneKind::Time && m_stones.timeRewinding()) {
            peak *= kRewindFlare;
        }

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
                    const float t = shapeRadius > 1e-4f ? r / shapeRadius : 0.0f;
                    if (stone.outlineCount >= 3) {
                        const float nx = fx / pixelRadius;
                        const float ny = fy / pixelRadius;
                        const float centralGlow = std::exp(-(nx * nx + ny * ny) * 1.25f);
                        const float rim = 0.58f + 0.42f * std::min(1.0f, (1.0f - t) * 7.0f);
                        const float facets = stoneSurfaceLighting(stone, nx, ny);
                        intensity = peak * (0.09f + 0.30f * centralGlow) * rim * facets;
                    } else {
                        // Unfinished stones deliberately retain the original circular,
                        // gaussian body until their own outlines are authored.
                        intensity = peak * std::exp(-t * t * 4.5f);
                    }
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

// The far mouth of Space's wormhole, drawn as a ring rather than a filled body so it reads
// as an opening. Without it, particles swallowed at the stone would simply reappear across
// the volume with nothing to connect the two events.
void App::drawPortal() {
    const Projected p = m_camera.project(m_stones.spaceExitPortal());
    if (!p.visible) return;

    const float ringRadius = kSpacePortalRadius * p.scale;
    const float thickness = ringRadius * 0.16f;
    const int span = static_cast<int>(ringRadius + thickness * 3.0f);
    if (span < 1 || thickness < 1e-3f) return;

    const float invTwoSigmaSq = 1.0f / (2.0f * thickness * thickness);
    const float ratio = kReferenceDepth / p.depth;
    const float attenuation = ratio * ratio;
    const float flare = 1.0f + kPortalFlare * m_stones.spaceActivity();
    const float peak =
        kPortalBrightness * (attenuation > 2.0f ? 2.0f : attenuation) * flare;

    const int cx = static_cast<int>(p.x);
    const int cy = static_cast<int>(p.y);

    for (int dy = -span; dy <= span; ++dy) {
        for (int dx = -span; dx <= span; ++dx) {
            const float fx = static_cast<float>(dx);
            const float fy = static_cast<float>(dy);
            const float offset = std::sqrt(fx * fx + fy * fy) - ringRadius;
            const float intensity = peak * std::exp(-(offset * offset) * invTwoSigmaSq);

            m_framebuffer.deposit(cx + dx, cy + dy, kPortalColor[0] * intensity,
                                  kPortalColor[1] * intensity, kPortalColor[2] * intensity);
        }
    }
}

// Collision feedback has three layers: a blended expanding ring, a short-lived spark
// burst at the contact, and a very brief full-screen flash. All are drawn into the same HDR
// buffer as the particles, so overlapping impacts add naturally before tone mapping.
void App::drawCollisionEffects() {
    for (const ShockFront& front : m_stones.shockFronts()) {
        if (!front.fromCollision) continue;

        const Projected p = m_camera.project(front.center);
        if (!p.visible) continue;

        const float ringRadius = front.age * front.speed * p.scale;
        const float thickness = std::max(1.0f, front.shellWidth * p.scale);
        const int span = static_cast<int>(ringRadius + thickness * 3.0f);
        const float invTwoSigmaSq = 1.0f / (2.0f * thickness * thickness);
        const float lifetime = front.maxRadius / front.speed;
        const float fade = std::max(0.0f, 1.0f - front.age / lifetime);
        const float attenuation = std::min(2.0f,
            (kReferenceDepth / p.depth) * (kReferenceDepth / p.depth));
        const float peak = kCollisionRingBrightness * fade * attenuation;
        const int cx = static_cast<int>(p.x);
        const int cy = static_cast<int>(p.y);

        for (int dy = -span; dy <= span; ++dy) {
            for (int dx = -span; dx <= span; ++dx) {
                const float fx = static_cast<float>(dx);
                const float fy = static_cast<float>(dy);
                const float offset = std::sqrt(fx * fx + fy * fy) - ringRadius;
                const float intensity = peak *
                    std::exp(-(offset * offset) * invTwoSigmaSq);
                m_framebuffer.deposit(cx + dx, cy + dy,
                                      front.r * intensity,
                                      front.g * intensity,
                                      front.b * intensity);
            }
        }
    }

    for (const CollisionSpark& spark : m_stones.collisionSparks()) {
        const Projected p = m_camera.project(spark.position);
        if (!p.visible) continue;

        const float radius = std::max(1.0f, std::min(2.5f, 0.011f * p.scale));
        const int span = static_cast<int>(radius * 2.5f);
        const float invTwoSigmaSq = 1.0f / (radius * radius);
        const float fade = std::max(0.0f, 1.0f - spark.age / spark.lifetime);
        const float peak = kCollisionSparkBrightness * fade * fade;
        const int cx = static_cast<int>(p.x);
        const int cy = static_cast<int>(p.y);

        for (int dy = -span; dy <= span; ++dy) {
            for (int dx = -span; dx <= span; ++dx) {
                const float distanceSq = static_cast<float>(dx * dx + dy * dy);
                const float intensity = peak * std::exp(-distanceSq * invTwoSigmaSq);
                m_framebuffer.deposit(cx + dx, cy + dy,
                                      spark.r * intensity,
                                      spark.g * intensity,
                                      spark.b * intensity);
            }
        }
    }

    const float flash = m_stones.collisionFlash();
    if (flash <= 0.0f) return;

    const Vec3 color = m_stones.collisionFlashColor();
    const float intensity = kCollisionFlashStrength * flash * flash;
    #pragma omp parallel for if(g_parallel) num_threads(g_threads) schedule(static)
    for (int y = 0; y < m_height; ++y) {
        for (int x = 0; x < m_width; ++x) {
            m_framebuffer.deposit(x, y, color.x * intensity,
                                  color.y * intensity, color.z * intensity);
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
    drawPortal();
    drawCollisionEffects();
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
