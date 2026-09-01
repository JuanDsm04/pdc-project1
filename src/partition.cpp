#include "partition.hpp"

#include <algorithm>

#include "parallel.hpp"

namespace {

inline int chunkBegin(int total, int threads, int t) {
    return static_cast<int>(static_cast<long long>(total) * t / threads);
}

}  // namespace

void KeyPartition::build(const uint8_t* keys, int count, int keyCount) {
    const int threads = g_parallel ? std::max(1, g_threads) : 1;

    m_order.resize(count);
    m_start.assign(keyCount + 1, 0);
    m_histogram.assign(static_cast<size_t>(threads) * keyCount, 0);

    #pragma omp parallel for if(g_parallel) num_threads(threads) schedule(static)
    for (int t = 0; t < threads; ++t) {
        int* histogram = m_histogram.data() + static_cast<size_t>(t) * keyCount;
        const int end = chunkBegin(count, threads, t + 1);
        for (int i = chunkBegin(count, threads, t); i < end; ++i) ++histogram[keys[i]];
    }

    // Exclusive prefix sum over keys, and within a key over threads, so each thread learns
    // where its own contribution to that key starts. Serial, but it walks keys times
    // threads, which is 96 entries here, not particles.
    int running = 0;
    for (int key = 0; key < keyCount; ++key) {
        m_start[key] = running;
        for (int t = 0; t < threads; ++t) {
            int& slot = m_histogram[static_cast<size_t>(t) * keyCount + key];
            const int here = slot;
            slot = running;
            running += here;
        }
    }
    m_start[keyCount] = running;

    #pragma omp parallel for if(g_parallel) num_threads(threads) schedule(static)
    for (int t = 0; t < threads; ++t) {
        int* cursor = m_histogram.data() + static_cast<size_t>(t) * keyCount;
        const int end = chunkBegin(count, threads, t + 1);
        for (int i = chunkBegin(count, threads, t); i < end; ++i) m_order[cursor[keys[i]]++] = i;
    }
}
