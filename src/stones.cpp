#include "stones.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "parallel.hpp"

namespace {

// Same scripted orbit every stone has moved on since the placeholder glow. Kept here now
// that Soul and Power need their position for physics, not just for rendering.
struct OrbitParams {
    float r, g, b;
    float orbitRadius, orbitSpeed, phase, bob;
};

const OrbitParams kOrbits[6] = {
    {0.20f, 0.45f, 1.00f, 0.80f, 0.42f, 0.0f, 0.35f},  // space
    {1.00f, 0.85f, 0.15f, 0.55f, 0.61f, 1.0f, 0.20f},  // mind
    {1.00f, 0.15f, 0.20f, 0.95f, 0.29f, 2.1f, 0.45f},  // reality
    {0.45f, 0.12f, 1.00f, 0.70f, 0.51f, 3.4f, 0.28f},  // power
    {0.25f, 1.00f, 0.40f, 0.45f, 0.73f, 4.2f, 0.15f},  // time
    {1.00f, 0.32f, 0.03f, 0.88f, 0.36f, 5.1f, 0.40f},  // soul
};

Vec3 orbitPosition(const OrbitParams& o, double time) {
    const float angle = static_cast<float>(time) * o.orbitSpeed + o.phase;
    return {std::cos(angle) * o.orbitRadius, std::sin(angle * 1.7f + o.phase) * o.bob,
            std::sin(angle) * o.orbitRadius};
}

// Soul: particles inside kCaptureRadius are held in a circular orbit instead of left to
// free fall, which is what "capture" means here. kOuterRadius is wider, so particles get a
// plain inverse square pull toward the stone before they are close enough to be captured,
// instead of a hard edge where nothing happens until they cross the capture line.
constexpr float kSoulCaptureRadius = 0.45f;
constexpr float kSoulOuterRadius   = 0.90f;
constexpr float kSoulSoftening     = 0.05f;
constexpr float kSoulG             = 1.10f;

// How fast a captured particle's velocity is corrected toward a clean circular orbit.
// Higher settles particles into a tight ring faster; too high and the correction itself
// becomes visible as an oscillation instead of a smooth spiral into the ring.
constexpr float kOrbitDamping = 2.5f;

// Every kSoulReleasePeriod seconds, captured particles are let go for kSoulReleaseDuration
// seconds: the orbit lock is skipped and capture is not reevaluated, so whatever velocity
// a particle already has carries it away from the ring under its own momentum.
constexpr double kSoulReleasePeriod   = 6.0;
constexpr double kSoulReleaseDuration = 1.0;

// Power: a new shockwave front spawns every kShockSpawnPeriod seconds and expands at
// kShockSpeed until it passes kShockMaxRadius, wide enough to clear the whole particle
// volume. The impulse is a thin gaussian shell around the front's current radius rather
// than a hard step, so it reads as a wave passing through instead of a sphere popping.
constexpr double kShockSpawnPeriod = 0.9;
constexpr float  kShockSpeed       = 1.3f;
constexpr float  kShockMaxRadius   = 2.0f;
constexpr float  kShockShellWidth  = 0.08f;
constexpr float  kShockImpulse     = 1.6f;

// Power is a broad, heavy shard with a broken shoulder and a flatter base. Soul is taller,
// narrower and more flame-like. Points run clockwise in screen space and both polygons
// contain the origin, which lets stoneShapeRadius intersect a ray with their edges.
constexpr StoneOutlinePoint kPowerOutline[] = {
    {-0.18f, -1.12f}, { 0.23f, -1.20f}, { 0.58f, -1.02f}, { 0.76f, -0.61f},
    { 0.70f, -0.23f}, { 0.88f,  0.18f}, { 0.70f,  0.68f}, { 0.35f,  1.00f},
    {-0.12f,  1.10f}, {-0.55f,  0.94f}, {-0.79f,  0.57f}, {-0.88f,  0.08f},
    {-0.75f, -0.48f}, {-0.50f, -0.91f},
};

constexpr StoneFacetSeed kPowerFacets[] = {
    {-0.48f, -0.64f, 0.74f}, { 0.02f, -0.82f, 1.20f}, { 0.48f, -0.48f, 0.88f},
    {-0.50f, -0.05f, 1.08f}, { 0.10f, -0.10f, 0.79f}, { 0.55f,  0.21f, 1.17f},
    {-0.31f,  0.59f, 0.82f}, { 0.25f,  0.68f, 1.06f},
};

constexpr StoneOutlinePoint kSoulOutline[] = {
    {-0.10f, -1.36f}, { 0.25f, -1.25f}, { 0.37f, -0.94f}, { 0.55f, -0.68f},
    { 0.48f, -0.31f}, { 0.61f,  0.08f}, { 0.54f,  0.55f}, { 0.40f,  0.98f},
    { 0.15f,  1.27f}, {-0.20f,  1.32f}, {-0.43f,  1.08f}, {-0.55f,  0.66f},
    {-0.49f,  0.22f}, {-0.62f, -0.20f}, {-0.46f, -0.69f}, {-0.35f, -1.12f},
};

constexpr StoneFacetSeed kSoulFacets[] = {
    {-0.25f, -0.98f, 0.78f}, { 0.18f, -0.91f, 1.16f}, {-0.31f, -0.39f, 1.07f},
    { 0.23f, -0.43f, 0.83f}, {-0.05f,  0.02f, 1.22f}, { 0.35f,  0.29f, 0.76f},
    {-0.28f,  0.54f, 0.87f}, { 0.10f,  0.87f, 1.10f},
};

template <size_t N, size_t M>
void setVisual(Stone& stone, const StoneOutlinePoint (&outline)[N],
               const StoneFacetSeed (&facets)[M]) {
    static_assert(N <= kMaxStoneOutlinePoints, "stone outline storage is too small");
    static_assert(M <= kMaxStoneFacetSeeds, "stone facet storage is too small");
    stone.outlineCount = static_cast<int>(N);
    stone.facetSeedCount = static_cast<int>(M);
    std::copy(outline, outline + N, stone.outline);
    std::copy(facets, facets + M, stone.facetSeeds);
}

float cross2(StoneOutlinePoint a, StoneOutlinePoint b) {
    return a.x * b.y - a.y * b.x;
}

}  // namespace

float stoneShapeRadius(const Stone& stone, float angle) {
    if (stone.outlineCount < 3) return 1.0f;

    const StoneOutlinePoint ray = {std::cos(angle), std::sin(angle)};
    float nearest = 1000.0f;

    for (int i = 0; i < stone.outlineCount; ++i) {
        const StoneOutlinePoint a = stone.outline[i];
        const StoneOutlinePoint b = stone.outline[(i + 1) % stone.outlineCount];
        const StoneOutlinePoint edge = {b.x - a.x, b.y - a.y};
        const float denominator = cross2(ray, edge);
        if (std::fabs(denominator) < 1e-6f) continue;

        const float distance = cross2(a, edge) / denominator;
        const float alongEdge = cross2(a, ray) / denominator;
        if (distance >= 0.0f && alongEdge >= 0.0f && alongEdge <= 1.0f) {
            nearest = std::min(nearest, distance);
        }
    }

    // Authored outlines contain the origin, but retaining a safe circular fallback avoids
    // a broken render if a future stone is edited into a non-star-shaped polygon.
    return nearest < 1000.0f ? nearest : 1.0f;
}

float stoneSurfaceLighting(const Stone& stone, float x, float y) {
    if (stone.facetSeedCount < 2) return 1.0f;

    float nearest = 1000.0f;
    float second = 1000.0f;
    float planeLight = 1.0f;

    for (int i = 0; i < stone.facetSeedCount; ++i) {
        const StoneFacetSeed& seed = stone.facetSeeds[i];
        // A small shear stops the cells from looking like regular round bubbles and makes
        // their boundaries read as long crystal planes.
        const float dx = (x - seed.x) + 0.22f * (y - seed.y);
        const float dy = (y - seed.y) * 0.82f;
        const float distanceSq = dx * dx + dy * dy;
        if (distanceSq < nearest) {
            second = nearest;
            nearest = distanceSq;
            planeLight = seed.light;
        } else if (distanceSq < second) {
            second = distanceSq;
        }
    }

    const float seam = std::exp(-std::fabs(second - nearest) * 32.0f);
    const float directional = 1.0f + 0.10f * (-0.65f * x - 0.35f * y);
    const float fineTexture = 0.035f * std::sin(x * 31.0f + y * 19.0f) +
                              0.025f * std::sin(x * 13.0f - y * 37.0f);
    const float light = planeLight * directional + seam * 0.34f + fineTexture;
    return std::max(0.48f, std::min(1.48f, light));
}

Stones::Stones() {
    m_stones.resize(6);
    for (size_t i = 0; i < 6; ++i) {
        m_stones[i].kind = static_cast<StoneKind>(i);
        m_stones[i].r = kOrbits[i].r;
        m_stones[i].g = kOrbits[i].g;
        m_stones[i].b = kOrbits[i].b;
    }

    setVisual(m_stones[static_cast<size_t>(StoneKind::Power)], kPowerOutline, kPowerFacets);
    setVisual(m_stones[static_cast<size_t>(StoneKind::Soul)], kSoulOutline, kSoulFacets);
}

void Stones::update(double time, float dt) {
    for (size_t i = 0; i < m_stones.size(); ++i) {
        m_stones[i].position = orbitPosition(kOrbits[i], time);
    }

    for (ShockFront& front : m_shockFronts) front.age += dt;

    std::vector<ShockFront> alive;
    alive.reserve(m_shockFronts.size());
    for (const ShockFront& front : m_shockFronts) {
        if (front.age * kShockSpeed <= kShockMaxRadius) alive.push_back(front);
    }
    m_shockFronts = std::move(alive);

    if (time - m_lastShockSpawn >= kShockSpawnPeriod) {
        m_lastShockSpawn = time;
        m_shockFronts.push_back(ShockFront{});
    }

    m_releasing = std::fmod(time, kSoulReleasePeriod) < kSoulReleaseDuration;
}

void Stones::applyForces(ParticleSystem& particles, float dt) {
    const Vec3 soulPos  = stone(StoneKind::Soul).position;
    const Vec3 powerPos = stone(StoneKind::Power).position;

    const int count = particles.count();
    const int frontCount = static_cast<int>(m_shockFronts.size());
    const ShockFront* fronts = m_shockFronts.data();

    int capturedCount = 0;

    #pragma omp parallel for if(g_parallel) num_threads(g_threads) schedule(static) \
        reduction(+:capturedCount)
    for (int i = 0; i < count; ++i) {
        Vec3 pos = {particles.px[i], particles.py[i], particles.pz[i]};
        Vec3 vel = {particles.vx[i], particles.vy[i], particles.vz[i]};

        // Soul
        {
            const Vec3 toStone = soulPos - pos;
            const float r = length(toStone);

            const bool releasing = m_releasing;
            if (releasing) {
                particles.captured[i] = 0;
            } else if (r < kSoulCaptureRadius) {
                particles.captured[i] = 1;
            }

            if (particles.captured[i]) {
                const Vec3 fromStone = pos - soulPos;
                const float radius = r > kSoulSoftening ? r : kSoulSoftening;
                const Vec3 radialDir = fromStone * (1.0f / radius);

                const float vRadialMag = dot(vel, radialDir);
                const Vec3 vRadial = radialDir * vRadialMag;
                const Vec3 vTangential = vel - vRadial;
                const float tangentialMag = length(vTangential);

                const float orbitSpeed = std::sqrt(kSoulG / radius);
                const float damp = kOrbitDamping * dt < 1.0f ? kOrbitDamping * dt : 1.0f;

                const float newRadialMag = vRadialMag * (1.0f - damp);
                float newTangentialMag = tangentialMag;
                if (tangentialMag > 1e-5f) {
                    newTangentialMag += (orbitSpeed - tangentialMag) * damp;
                    vel = radialDir * newRadialMag +
                          (vTangential * (1.0f / tangentialMag)) * newTangentialMag;
                } else {
                    vel = radialDir * newRadialMag;
                }
            } else if (r < kSoulOuterRadius) {
                const float r2 = r * r + kSoulSoftening * kSoulSoftening;
                const float accelMag = kSoulG / r2;
                const Vec3 dir = toStone * (1.0f / (r > 1e-5f ? r : 1e-5f));
                vel += dir * (accelMag * dt);
            }

            if (particles.captured[i]) ++capturedCount;
        }

        // Power
        if (frontCount > 0) {
            const Vec3 toStone = pos - powerPos;
            const float r = length(toStone);
            if (r > 1e-5f) {
                const Vec3 dir = toStone * (1.0f / r);
                for (int f = 0; f < frontCount; ++f) {
                    const float frontRadius = fronts[f].age * kShockSpeed;
                    const float delta = r - frontRadius;
                    const float shell =
                        std::exp(-(delta * delta) / (2.0f * kShockShellWidth * kShockShellWidth));
                    vel += dir * (kShockImpulse * shell * dt);
                }
            }
        }

        particles.vx[i] = vel.x;
        particles.vy[i] = vel.y;
        particles.vz[i] = vel.z;
    }

    m_soulCapturedCount = capturedCount;
}
