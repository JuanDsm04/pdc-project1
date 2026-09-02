#include "raster.hpp"

#include <algorithm>
#include <cmath>

#include "parallel.hpp"

void Rasterizer::resize(int width, int height) {
    m_width  = width;
    m_height = height;
    m_tilesX = (width + kTileSize - 1) / kTileSize;
    m_tilesY = (height + kTileSize - 1) / kTileSize;

    const int tileCount = m_tilesX * m_tilesY;
    m_tileBins.assign(tileCount, {});

    // Sized for the thread count the machine reports today. bin() grows this further if a
    // later run asks for more threads than that, which only matters once --threads can
    // exceed the hardware count from the CLI.
    const int maxThreads = omp_get_max_threads();
    m_scratch.assign(maxThreads, std::vector<std::vector<int>>(tileCount));
}

// Splats are appended into tiles from a loop over particles, which is the natural order to
// visit them in but the wrong order to write tiles in: two threads could easily land in the
// same tile on the same frame. Giving every thread its own private set of tile bins avoids
// that without a lock, at the cost of a merge pass once the parallel region ends.
void Rasterizer::bin(const std::vector<Splat>& splats) {
    const int numThreads = g_parallel ? std::max(1, g_threads) : 1;

    if (static_cast<int>(m_scratch.size()) < numThreads) {
        m_scratch.resize(numThreads, std::vector<std::vector<int>>(m_tileBins.size()));
    }

    // Only active thread buffers participate. Clearing every buffer allocated for the
    // configured maximum would charge the serial baseline for work belonging to threads
    // it did not use and would distort the reported speedup.
    for (int thread = 0; thread < numThreads; ++thread) {
        for (auto& tile : m_scratch[thread]) tile.clear();
    }

    const int count = static_cast<int>(splats.size());

    #pragma omp parallel for if(g_parallel) num_threads(g_threads) schedule(static)
    for (int i = 0; i < count; ++i) {
        const Splat& s = splats[i];
        if (s.peak <= 0.0f) continue;

        const int tileMinX = std::max(0, (s.cx - s.span) / kTileSize);
        const int tileMaxX = std::min(m_tilesX - 1, (s.cx + s.span) / kTileSize);
        const int tileMinY = std::max(0, (s.cy - s.span) / kTileSize);
        const int tileMaxY = std::min(m_tilesY - 1, (s.cy + s.span) / kTileSize);
        if (tileMinX > tileMaxX || tileMinY > tileMaxY) continue;

        std::vector<std::vector<int>>& perThread = m_scratch[omp_get_thread_num()];
        for (int ty = tileMinY; ty <= tileMaxY; ++ty) {
            for (int tx = tileMinX; tx <= tileMaxX; ++tx) {
                perThread[ty * m_tilesX + tx].push_back(i);
            }
        }
    }

    // Merged tile by tile rather than thread by thread. Both orders visit the same data,
    // but this one gives each tile to exactly one thread, so the merge parallelises for free
    // where the thread major version had to be serial to avoid two threads appending to the
    // same bin. It also writes each destination bin once instead of revisiting it per
    // thread, so the copies stay in cache.
    const int tileCount = static_cast<int>(m_tileBins.size());

    #pragma omp parallel for if(g_parallel) num_threads(g_threads) schedule(static)
    for (int tile = 0; tile < tileCount; ++tile) {
        std::vector<int>& destination = m_tileBins[tile];
        destination.clear();

        size_t total = 0;
        for (int thread = 0; thread < numThreads; ++thread) total += m_scratch[thread][tile].size();
        if (total == 0) continue;
        destination.reserve(total);

        for (int thread = 0; thread < numThreads; ++thread) {
            const std::vector<int>& source = m_scratch[thread][tile];
            if (!source.empty()) destination.insert(destination.end(), source.begin(), source.end());
        }
    }
}

// A splat that straddles a tile boundary was appended to every tile it overlaps, so each
// tile clips to its own rectangle here. Without the clip, the overlap region would be
// deposited once per tile that shares it, brightening every seam in the image.
void Rasterizer::rasterizeTiles(const std::vector<Splat>& splats,
                                Framebuffer& framebuffer) const {
    const int tileCount = static_cast<int>(m_tileBins.size());

    #pragma omp parallel for if(g_parallel) num_threads(g_threads) schedule(dynamic)
    for (int tile = 0; tile < tileCount; ++tile) {
        const std::vector<int>& bin = m_tileBins[tile];
        if (bin.empty()) continue;

        const int tx = tile % m_tilesX;
        const int ty = tile / m_tilesX;
        const int tileX0 = tx * kTileSize;
        const int tileY0 = ty * kTileSize;
        const int tileX1 = std::min(tileX0 + kTileSize, m_width);
        const int tileY1 = std::min(tileY0 + kTileSize, m_height);

        for (const int index : bin) {
            const Splat& s = splats[index];
            const int x0 = std::max(tileX0, s.cx - s.span);
            const int x1 = std::min(tileX1 - 1, s.cx + s.span);
            const int y0 = std::max(tileY0, s.cy - s.span);
            const int y1 = std::min(tileY1 - 1, s.cy + s.span);

            for (int y = y0; y <= y1; ++y) {
                const int dy = y - s.cy;
                for (int x = x0; x <= x1; ++x) {
                    const int dx = x - s.cx;
                    const float distSq = static_cast<float>(dx * dx + dy * dy);
                    const float intensity = std::exp(-distSq * s.invTwoSigmaSq) * s.peak;

                    framebuffer.deposit(x, y, s.r * intensity, s.g * intensity,
                                        s.b * intensity);
                }
            }
        }
    }
}
