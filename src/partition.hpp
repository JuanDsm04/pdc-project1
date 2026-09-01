#pragma once

#include <cstdint>
#include <vector>

// Parallel counting sort over a small integer key: per thread histograms, an exclusive
// prefix sum, then a scatter. Written as a primitive rather than inlined into the chamber
// code because the Snap needs exactly the same machinery with a two valued key, alive and
// dissolving, and two hand rolled compactions would be two places to get the prefix sum
// subtly wrong.
//
// The result is stable, and stable in a stronger sense than usual: threads take contiguous
// ascending ranges and their offsets within a key follow thread order, so the permutation
// is identical whether one thread ran it or sixteen. That is what keeps a reordered run
// bit comparable against a sequential one.
class KeyPartition {
public:
    void build(const uint8_t* keys, int count, int keyCount);

    // Particle indices grouped by key, ascending within each group.
    const std::vector<int>& order() const { return m_order; }

    int rangeBegin(int key) const { return m_start[key]; }
    int rangeEnd(int key) const { return m_start[key + 1]; }
    int rangeSize(int key) const { return m_start[key + 1] - m_start[key]; }

private:
    std::vector<int> m_order;
    std::vector<int> m_start;      // keyCount + 1 entries
    std::vector<int> m_histogram;  // [thread * keyCount + key]
};
