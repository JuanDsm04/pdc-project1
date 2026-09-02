#include "grid.hpp"

#include <algorithm>

#include "parallel.hpp"
#include "particles.hpp"

namespace {

// Threads take contiguous, ascending ranges rather than whatever OpenMP's scheduler picks.
// The scatter below relies on it: if thread t owns a lower range of particle indices than
// thread t+1, and thread offsets within a cell follow thread order, then every cell ends up
// sorted by particle index no matter how many threads ran.
inline int chunkBegin(int total, int threads, int t) {
    return static_cast<int>(static_cast<long long>(total) * t / threads);
}

}  // namespace

void SpatialGrid::configure(float halfExtent, int dim) {
    m_dim = dim;
    m_halfExtent = halfExtent;
    m_invCellSize = static_cast<float>(dim) / (2.0f * halfExtent);
    m_cells.assign(cellCount(), CellSummary{});
    m_cellStart.assign(cellCount() + 1, 0);
}

int SpatialGrid::axisCell(float coordinate) const {
    const int c = static_cast<int>((coordinate + m_halfExtent) * m_invCellSize);
    return c < 0 ? 0 : (c >= m_dim ? m_dim - 1 : c);
}

void SpatialGrid::build(const ParticleSystem& particles) {
    const int count = particles.count();
    const int cells = cellCount();
    const int threads = g_parallel ? std::max(1, g_threads) : 1;

    m_cellOf.resize(count);
    m_sorted.resize(count);
    m_histogram.assign(static_cast<size_t>(threads) * cells, 0);

    // Pass 1: bucket every particle and count, each thread into its own histogram so the
    // counting needs no atomics.
    #pragma omp parallel for if(g_parallel) num_threads(threads) schedule(static)
    for (int t = 0; t < threads; ++t) {
        int* histogram = m_histogram.data() + static_cast<size_t>(t) * cells;
        const int end = chunkBegin(count, threads, t + 1);

        for (int i = chunkBegin(count, threads, t); i < end; ++i) {
            const int cell = cellIndex(axisCell(particles.px[i]), axisCell(particles.py[i]),
                                       axisCell(particles.pz[i]));
            m_cellOf[i] = cell;
            ++histogram[cell];
        }
    }

    // Pass 2: the exclusive prefix sum, split into three so the only serial part is a walk
    // over cells alone.
    //
    // The obvious way to write this is one serial loop over cells with an inner loop over
    // threads. That is cells times threads of serial work, which grows as threads are added,
    // so adding cores actively made the frame slower. Here the two cells times threads
    // passes are parallel and the serial scan touches each cell once, independent of thread
    // count.
    m_cellTotals.resize(cells);

    #pragma omp parallel for if(g_parallel) num_threads(threads) schedule(static)
    for (int cell = 0; cell < cells; ++cell) {
        int total = 0;
        for (int t = 0; t < threads; ++t) total += m_histogram[static_cast<size_t>(t) * cells + cell];
        m_cellTotals[cell] = total;
    }

    int running = 0;
    for (int cell = 0; cell < cells; ++cell) {
        m_cellStart[cell] = running;
        running += m_cellTotals[cell];
    }
    m_cellStart[cells] = running;

    // Hand each thread its own offset inside the cell it will scatter into. Parallel because
    // a cell's offsets depend only on that cell's own counts.
    #pragma omp parallel for if(g_parallel) num_threads(threads) schedule(static)
    for (int cell = 0; cell < cells; ++cell) {
        int base = m_cellStart[cell];
        for (int t = 0; t < threads; ++t) {
            int& slot = m_histogram[static_cast<size_t>(t) * cells + cell];
            const int here = slot;
            slot = base;
            base += here;
        }
    }

    // Pass 3: scatter. Each thread walks its own ascending range and appends into the slot
    // reserved for it, so a cell's particles come out in ascending index order.
    #pragma omp parallel for if(g_parallel) num_threads(threads) schedule(static)
    for (int t = 0; t < threads; ++t) {
        int* cursor = m_histogram.data() + static_cast<size_t>(t) * cells;
        const int end = chunkBegin(count, threads, t + 1);

        for (int i = chunkBegin(count, threads, t); i < end; ++i) {
            m_sorted[cursor[m_cellOf[i]]++] = i;
        }
    }

    // Pass 4: summarise. One thread owns a whole cell, so the float sums happen in a fixed
    // order and no two threads touch the same accumulator.
    #pragma omp parallel for if(g_parallel) num_threads(threads) schedule(static)
    for (int cell = 0; cell < cells; ++cell) {
        const int begin = m_cellStart[cell];
        const int end = m_cellStart[cell + 1];

        CellSummary summary;
        summary.count = end - begin;

        if (summary.count > 0) {
            Vec3 position;
            Vec3 velocity;
            for (int s = begin; s < end; ++s) {
                const int i = m_sorted[s];
                position += Vec3{particles.px[i], particles.py[i], particles.pz[i]};
                velocity += Vec3{particles.vx[i], particles.vy[i], particles.vz[i]};
            }
            const float inverse = 1.0f / static_cast<float>(summary.count);
            summary.meanPosition = position * inverse;
            summary.meanVelocity = velocity * inverse;
        }

        m_cells[cell] = summary;
    }
}
