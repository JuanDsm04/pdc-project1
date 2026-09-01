#include "particles.hpp"

#include "parallel.hpp"

namespace {

// Fraction of a chamber the cloud is seeded into, leaving a margin so nothing starts
// already touching the wall.
constexpr float kSpawnFraction = 0.92f;

// Perfectly elastic walls. With no forces acting yet the only energy in the system is
// what spawning put there, so anything below 1.0 bleeds the cloud to a standstill.
constexpr float kRestitution = 1.0f;

// Reflects a particle back into its chamber off the inside of the sphere. The velocity is
// only flipped when it actually points outward: a particle already turning back inward,
// which happens on the step after a bounce while it is still clamped to the surface, would
// otherwise be flipped a second time and pinned to the wall.
inline void confineToChamber(Vec3& position, Vec3& velocity, Vec3 center) {
    const Vec3 offset = position - center;
    const float distanceSq = lengthSq(offset);
    if (distanceSq <= kChamberRadius * kChamberRadius) return;

    const float distance = std::sqrt(distanceSq);
    const Vec3 normal = offset * (1.0f / distance);
    position = center + normal * kChamberRadius;

    const float outward = dot(velocity, normal);
    if (outward > 0.0f) velocity -= normal * (outward * (1.0f + kRestitution));
}

}  // namespace

void ParticleSystem::reset(int count, uint32_t seed) {
    m_count = count;

    px.resize(count); py.resize(count); pz.resize(count);
    vx.resize(count); vy.resize(count); vz.resize(count);
    cr.resize(count); cg.resize(count); cb.resize(count);
    captured.assign(count, 0);
    timeScale.assign(count, 1.0f);
    chamber.assign(count, 0);

    for (int i = 0; i < count; ++i) {
        uint32_t h = hashCombine(seed, static_cast<uint32_t>(i));

        const float u1 = randomFloat(h); h = hashU32(h);
        const float u2 = randomFloat(h); h = hashU32(h);
        const float u3 = randomFloat(h); h = hashU32(h);
        const float u4 = randomFloat(h); h = hashU32(h);

        // Chambers are filled in equal contiguous blocks rather than by hashing the index.
        // Step 14 sorts particles into exactly these ranges, so seeding them already grouped
        // means the very first frame is in the layout the rest of the pipeline expects.
        const int owner = static_cast<int>(static_cast<long long>(i) * kChamberCount / count);
        const Vec3 center = chamberCenter(owner);

        // Cube root of a uniform sample spreads points evenly through the volume of the
        // sphere. Sampling the radius uniformly instead would pile them at the center.
        const float radius = kChamberRadius * kSpawnFraction * std::cbrt(u1);
        const float cosTheta = 2.0f * u2 - 1.0f;
        const float sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);
        const float phi = 2.0f * kPi * u3;

        const Vec3 local = {radius * sinTheta * std::cos(phi), radius * cosTheta,
                            radius * sinTheta * std::sin(phi)};

        // Tangential launch about the chamber's own vertical axis, so each globe starts out
        // swirling rather than expanding straight into its wall.
        const Vec3 tangent = normalize(cross(Vec3{0.0f, 1.0f, 0.0f}, local));
        const float speed = 0.16f + 0.12f * u4;

        const Vec3 position = center + local;
        px[i] = position.x; py[i] = position.y; pz[i] = position.z;
        vx[i] = tangent.x * speed;
        vy[i] = tangent.y * speed + 0.03f * randomSigned(h);
        vz[i] = tangent.z * speed;

        chamber[i] = static_cast<uint8_t>(owner);
        cr[i] = kStoneColor[owner][0];
        cg[i] = kStoneColor[owner][1];
        cb[i] = kStoneColor[owner][2];
    }
}

// Every iteration only reads and writes its own index, so there is nothing to
// synchronize: no shared accumulator, no reduction, no order dependence between
// particles. That is what makes this loop safe to hand straight to OpenMP.
void ParticleSystem::integrate(float dt) {
    Vec3 centers[kChamberCount];
    for (int c = 0; c < kChamberCount; ++c) centers[c] = chamberCenter(c);

    #pragma omp parallel for if(g_parallel) num_threads(g_threads) schedule(static)
    for (int i = 0; i < m_count; ++i) {
        const float step = dt * timeScale[i];

        Vec3 position = {px[i] + vx[i] * step, py[i] + vy[i] * step, pz[i] + vz[i] * step};
        Vec3 velocity = {vx[i], vy[i], vz[i]};

        confineToChamber(position, velocity, centers[chamber[i]]);

        px[i] = position.x; py[i] = position.y; pz[i] = position.z;
        vx[i] = velocity.x; vy[i] = velocity.y; vz[i] = velocity.z;
    }
}