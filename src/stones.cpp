#include "stones.hpp"

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
    {0.65f, 0.25f, 1.00f, 0.70f, 0.51f, 3.4f, 0.28f},  // power
    {0.25f, 1.00f, 0.40f, 0.45f, 0.73f, 4.2f, 0.15f},  // time
    {1.00f, 0.45f, 0.10f, 0.88f, 0.36f, 5.1f, 0.40f},  // soul
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

// Harmonic numbers summed into a silhouette, low to high. Skipping 1 and 2 avoids a shape
// that is mostly just recentered or egg shaped; starting at 3 guarantees at least three
// lobes so the outline reads as faceted rather than merely lopsided.
constexpr int kFacetHarmonicNumber[kFacetHarmonics] = {3, 4, 5, 6, 7};

// Deterministic, not random: two stones built from the same seed would look identical, and
// a seed has to stay fixed across a run for the silhouette not to swim from frame to frame.
void generateFacets(uint32_t seed, Stone& stone) {
    uint32_t h = seed;
    for (int k = 0; k < kFacetHarmonics; ++k) {
        // Higher harmonics get a smaller amplitude ceiling, the way a rock's outline has
        // one or two dominant lobes with progressively finer chipping on top rather than
        // every frequency contributing equally.
        const float maxAmplitude = 0.30f / static_cast<float>(k + 1);

        h = hashCombine(h, static_cast<uint32_t>(k * 2 + 1));
        stone.facetAmplitude[k] = maxAmplitude * (0.5f + 0.5f * randomFloat(h));

        h = hashCombine(h, static_cast<uint32_t>(k * 2 + 2));
        stone.facetPhase[k] = randomFloat(h) * 2.0f * kPi;
    }
}

}  // namespace

float stoneShapeRadius(const Stone& stone, float angle) {
    float multiplier = 1.0f;
    for (int k = 0; k < kFacetHarmonics; ++k) {
        const float harmonic = static_cast<float>(kFacetHarmonicNumber[k]);
        multiplier += stone.facetAmplitude[k] *
                      std::cos(harmonic * angle + stone.facetPhase[k]);
    }
    // The harmonic sum can push the multiplier below zero or well above 1 if enough phases
    // happen to line up; clamping keeps the silhouette a recognizable blob instead of a
    // spike or an inverted hole.
    if (multiplier < 0.5f) return 0.5f;
    if (multiplier > 1.75f) return 1.75f;
    return multiplier;
}

Stones::Stones() {
    m_stones.resize(6);
    for (size_t i = 0; i < 6; ++i) {
        m_stones[i].kind = static_cast<StoneKind>(i);
        m_stones[i].r = kOrbits[i].r;
        m_stones[i].g = kOrbits[i].g;
        m_stones[i].b = kOrbits[i].b;
    }

    // One seed per stone so their silhouettes do not match each other. Space, Time,
    // Reality and Mind keep facetAmplitude at zero (a plain circle) until their own turn
    // at steps 11 and 12.
    generateFacets(0xA17B2E11u, m_stones[static_cast<size_t>(StoneKind::Soul)]);
    generateFacets(0xC9F13D45u, m_stones[static_cast<size_t>(StoneKind::Power)]);
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