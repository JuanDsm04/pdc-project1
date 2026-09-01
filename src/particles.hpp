#pragma once

#include <cstdint>
#include <vector>

#include "chambers.hpp"
#include "math3d.hpp"

// Half width of the cube that bounds everything. No longer the wall particles bounce off,
// which is now each chamber's own sphere; this is kept for the merge phase, when the chamber
// walls drop and only an outer limit remains.
constexpr float kWorldHalfExtent = kRingExtent;

// Particles are stored as a structure of arrays rather than an array of structs. The hot
// loops touch positions and velocities in separate passes, so keeping each attribute in
// its own contiguous block means a pass over positions loads only positions into cache
// instead of dragging color and mass along with it, and it leaves the loops in a shape the
// compiler can vectorize.
class ParticleSystem {
public:
    void reset(int count, uint32_t seed);
    void integrate(float dt);

    int count() const { return m_count; }

    std::vector<float> px, py, pz;
    std::vector<float> vx, vy, vz;
    std::vector<float> cr, cg, cb;

    // Written by Stones::applyForces from the Time stone's dilation field, read by
    // integrate. A particle deep inside Time's bubble advances at a fraction of the real
    // step, which is what makes the slow motion sphere visible rather than merely stated.
    std::vector<float> timeScale;

    // Set by Stones::applyForces when the Soul stone currently holds a particle in orbit.
    // Lives here rather than in Stones because it has to sit next to the rest of a
    // particle's state for the integrate loop's cache access pattern to stay coherent, the
    // same reasoning that keeps every other per particle attribute in its own array.
    std::vector<uint8_t> captured;

    // Which chamber owns this particle. Drives confinement, colour, and which stone's force
    // is allowed to touch it, so it is the single source of ownership rather than three
    // separate notions that could drift apart.
    std::vector<uint8_t> chamber;

private:
    int m_count = 0;
};