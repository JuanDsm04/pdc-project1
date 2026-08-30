#include "particles.hpp"

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kSpawnRadius = 0.95f;

// Perfectly elastic walls. With no forces acting yet the only energy in the system is
// what spawning put there, so anything below 1.0 bleeds the cloud to a standstill.
constexpr float kRestitution = 1.0f;

// Stone colors, assigned at random so the cloud reads as six mingled populations before
// the stones that own them exist.
const Vec3 kPalette[6] = {
    {0.20f, 0.45f, 1.00f}, {1.00f, 0.85f, 0.15f}, {1.00f, 0.15f, 0.20f},
    {0.65f, 0.25f, 1.00f}, {0.25f, 1.00f, 0.40f}, {1.00f, 0.45f, 0.10f},
};

inline void bounceAxis(float& position, float& velocity, float limit) {
    if (position < -limit) {
        position = -limit;
        velocity = -velocity * kRestitution;
    } else if (position > limit) {
        position = limit;
        velocity = -velocity * kRestitution;
    }
}

}  // namespace

void ParticleSystem::reset(int count, uint32_t seed) {
    m_count = count;

    px.resize(count); py.resize(count); pz.resize(count);
    vx.resize(count); vy.resize(count); vz.resize(count);
    cr.resize(count); cg.resize(count); cb.resize(count);

    for (int i = 0; i < count; ++i) {
        uint32_t h = hashCombine(seed, static_cast<uint32_t>(i));

        const float u1 = randomFloat(h); h = hashU32(h);
        const float u2 = randomFloat(h); h = hashU32(h);
        const float u3 = randomFloat(h); h = hashU32(h);
        const float u4 = randomFloat(h); h = hashU32(h);
        const uint32_t stone = h % 6u;

        // Cube root of a uniform sample spreads points evenly through the volume of the
        // sphere. Sampling the radius uniformly instead would pile them at the center.
        const float radius = kSpawnRadius * std::cbrt(u1);
        const float cosTheta = 2.0f * u2 - 1.0f;
        const float sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);
        const float phi = 2.0f * kPi * u3;

        const Vec3 position = {radius * sinTheta * std::cos(phi), radius * cosTheta,
                               radius * sinTheta * std::sin(phi)};

        // Tangential launch around the vertical axis, so the cloud starts out swirling
        // rather than expanding straight outward.
        const Vec3 tangent = normalize(cross(Vec3{0.0f, 1.0f, 0.0f}, position));
        const float speed = 0.30f + 0.20f * u4;

        px[i] = position.x; py[i] = position.y; pz[i] = position.z;
        vx[i] = tangent.x * speed;
        vy[i] = tangent.y * speed + 0.05f * randomSigned(h);
        vz[i] = tangent.z * speed;

        cr[i] = kPalette[stone].x;
        cg[i] = kPalette[stone].y;
        cb[i] = kPalette[stone].z;
    }
}

void ParticleSystem::integrate(float dt) {
    for (int i = 0; i < m_count; ++i) {
        px[i] += vx[i] * dt;
        py[i] += vy[i] * dt;
        pz[i] += vz[i] * dt;

        bounceAxis(px[i], vx[i], kWorldHalfExtent);
        bounceAxis(py[i], vy[i], kWorldHalfExtent);
        bounceAxis(pz[i], vz[i], kWorldHalfExtent);
    }
}
