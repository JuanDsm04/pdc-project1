#pragma once

#include <vector>

#include "math3d.hpp"
#include "particles.hpp"

enum class StoneKind { Space, Mind, Reality, Power, Time, Soul, Count };

// The two finished stones use hand-authored, convex-ish outlines rather than a noisy
// circle. Keeping the points in normalized screen space makes the silhouette cheap to
// rasterize while still allowing every stone to have a completely different aspect ratio.
constexpr int kMaxStoneOutlinePoints = 16;
constexpr int kMaxStoneFacetSeeds = 8;

struct StoneOutlinePoint {
    float x = 0.0f;
    float y = 0.0f;
};

// A small Voronoi field inside the silhouette gives the stone broad crystal planes. The
// value is the amount of light caught by that plane; it is intentionally authored with the
// outline so the internal structure is different for Soul and Power as well.
struct StoneFacetSeed {
    float x = 0.0f;
    float y = 0.0f;
    float light = 1.0f;
};

// A stone's own moving body: what App needs to render it, plus what applyForces needs to
// know where it is. Orbit choreography lives in stones.cpp, not here, since nothing outside
// Stones ever reads it; only kind, color and the current position cross that boundary.
struct Stone {
    StoneKind kind;
    float r, g, b;
    Vec3  position;

    int outlineCount = 0;
    StoneOutlinePoint outline[kMaxStoneOutlinePoints] = {};

    int facetSeedCount = 0;
    StoneFacetSeed facetSeeds[kMaxStoneFacetSeeds] = {};
};

// Distance from the stone's center to its polygon edge along a screen-space ray. A stone
// with no authored outline returns 1, so unfinished stones retain their circular glow.
float stoneShapeRadius(const Stone& stone, float angle);

// Lighting multiplier for a normalized point inside a stone. Authored facet seeds form
// irregular planes and bright seams; unfinished circular stones simply return 1.
float stoneSurfaceLighting(const Stone& stone, float x, float y);

// One expanding shockwave front spawned by the Power stone. Radius is derived from age
// rather than stored, so aging every front is a single add with nothing else to keep in
// sync.
struct ShockFront {
    float age = 0.0f;
};

// Owns the six stones' positions and the forces the stones that already have physics
// exert on the particle system. Space, Time, Reality and Mind still only move on their
// scripted orbit for now; their powers arrive across steps 11 and 12.
class Stones {
public:
    Stones();

    // Advances stone positions and ages or spawns Power's shockwave fronts. Serial: six
    // bodies and a handful of shockwave fronts is negligible next to the particle loop.
    void update(double time, float dt);

    // Soul's gravity and capture, and Power's shockwave impulses, applied to every
    // particle in one pass. Parallel over particles: each iteration only touches its own
    // particle, plus a shared reduction that counts how many particles Soul currently
    // holds captured.
    void applyForces(ParticleSystem& particles, float dt);

    const Stone& stone(StoneKind kind) const { return m_stones[static_cast<size_t>(kind)]; }
    const std::vector<Stone>& all() const { return m_stones; }

    // Fed by the capture reduction in applyForces; used to brighten Soul's own glow with
    // how much it currently holds, so the capture and release cycle reads visually even
    // before the HUD or a report surfaces the raw count.
    int soulCapturedCount() const { return m_soulCapturedCount; }

private:
    std::vector<Stone> m_stones;
    std::vector<ShockFront> m_shockFronts;

    double m_lastShockSpawn = -1e9;
    int    m_soulCapturedCount = 0;
    bool   m_releasing = false;
};
