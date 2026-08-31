#pragma once

#include <vector>

#include "math3d.hpp"
#include "particles.hpp"

enum class StoneKind { Space, Mind, Reality, Power, Time, Soul, Count };

// Number of angular harmonics summed into a stone's silhouette. Five is enough to read as
// a jagged, irregular gem rather than either a perfect circle or noise.
constexpr int kFacetHarmonics = 5;

// A stone's own moving body: what App needs to render it, plus what applyForces needs to
// know where it is. Orbit choreography lives in stones.cpp, not here, since nothing outside
// Stones ever reads it; only kind, color and the current position cross that boundary.
struct Stone {
    StoneKind kind;
    float r, g, b;
    Vec3  position;

    // Angular silhouette as a low order Fourier series: stoneShapeRadius sums these into a
    // radius multiplier that varies with angle instead of staying flat at 1. Left at zero
    // this collapses back to a plain circle, which is what a stone without a shape yet
    // (still generated at steps 11 and 12) renders as.
    float facetAmplitude[kFacetHarmonics] = {};
    float facetPhase[kFacetHarmonics] = {};
};

// Radius multiplier for a stone's silhouette at a given screen space angle. A stone with
// no facets generated returns 1 for every angle, i.e. a circle, so the caller never has to
// branch between faceted and unfaceted stones.
float stoneShapeRadius(const Stone& stone, float angle);

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