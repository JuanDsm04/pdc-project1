#pragma once

#include <vector>

#include "math3d.hpp"

class ParticleSystem;

// Everything the flocking rules need to know about one grid cell, gathered in one struct
// on purpose. The Mind stone reads 27 neighbouring cells per particle in no useful order,
// so what matters is that one irregular fetch lands count, position and velocity on the
// same cache line rather than touching three separate arrays for every cell.
struct CellSummary {
    int   count = 0;
    Vec3  meanPosition;
    Vec3  meanVelocity;
    float pad = 0.0f;
};

// Uniform grid over the simulation volume, rebuilt every frame by a parallel counting
// sort: per thread histograms, an exclusive prefix sum, then a scatter.
//
// The sort is not an optimisation, it is what keeps the run reproducible. Summing a cell's
// positions straight out of a parallel loop would add them in whatever order the threads
// happened to arrive, and float addition is not associative, so one thread and sixteen
// threads would produce slightly different cell means, different flocking forces, and a
// visibly different simulation within seconds. Sorting first means each cell is summed by
// exactly one thread, in ascending particle index, and the result no longer depends on how
// many threads ran.
class SpatialGrid {
public:
    void configure(float halfExtent, int dim);
    void build(const ParticleSystem& particles);

    int dim() const { return m_dim; }
    int cellCount() const { return m_dim * m_dim * m_dim; }

    // Grid coordinate on one axis, clamped, so a particle sitting exactly on the wall or
    // pushed a hair outside it still lands in a real cell instead of indexing out of range.
    int axisCell(float coordinate) const;

    int cellIndex(int x, int y, int z) const { return (z * m_dim + y) * m_dim + x; }

    const CellSummary& cell(int index) const { return m_cells[index]; }

private:
    int   m_dim = 0;
    float m_halfExtent = 0.0f;
    float m_invCellSize = 0.0f;

    std::vector<CellSummary> m_cells;
    std::vector<int> m_cellOf;      // cell index per particle
    std::vector<int> m_cellStart;   // exclusive prefix sum, cellCount + 1 entries
    std::vector<int> m_sorted;      // particle indices grouped by cell, ascending within one
    std::vector<int> m_histogram;   // per thread cell counts, [thread * cellCount + cell]
    std::vector<int> m_cellTotals;  // counts summed across threads, one per cell
};
