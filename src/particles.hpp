#pragma once

#include <cstdint>
#include <vector>

#include "math3d.hpp"

// Half width of the cube the simulation is confined to, in world units.
constexpr float kWorldHalfExtent = 1.25f;

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

private:
    int m_count = 0;
};