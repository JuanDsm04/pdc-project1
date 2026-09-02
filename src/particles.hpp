#pragma once

#include <cstdint>
#include <vector>

#include "chambers.hpp"
#include "math3d.hpp"
#include "partition.hpp"

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
    // chamberWalls false drops the globes and confines to the outer bound instead, which is
    // what lets the cloud cross the ring during a merge.
    void integrate(float dt, bool chamberWalls);

    // Reorders every per particle array so particles are grouped by chamber, and records
    // where each chamber's block starts. Step 15 launches one loop per block, which is what
    // removes the six way gate from the force pass.
    //
    // Any caller must treat particle indices as invalidated afterwards. Nothing outside this
    // class may hold an index across a regroup; the Time stone's position history is exactly
    // such a thing, which is why the merge at step 16 has to drop it when it reassigns
    // chambers.
    void regroup();

    int count() const { return m_count; }

    int chamberBegin(int chamber) const { return m_chamberStart[chamber]; }
    int chamberEnd(int chamber) const { return m_chamberStart[chamber + 1]; }
    int chamberSize(int chamber) const {
        return m_chamberStart[chamber + 1] - m_chamberStart[chamber];
    }

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
    int m_chamberStart[kChamberCount + 1] = {};

    KeyPartition m_partition;
    std::vector<float>   m_scratchFloat;
    std::vector<uint8_t> m_scratchByte;
};