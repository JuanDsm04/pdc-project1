#pragma once

#include <vector>

#include "framebuffer.hpp"

// One additive gaussian splat, reduced to the handful of numbers the rasterizer needs.
// Built once per particle by the caller and handed off; a tile never has to look back at
// particle data once it owns this.
struct Splat {
    int   cx = 0, cy = 0;    // screen space center
    int   span = 0;          // footprint reaches cx-span..cx+span, cy-span..cy+span
    float invTwoSigmaSq = 0.0f;
    float peak = 0.0f;       // zero marks a particle that was not visible this frame
    float r = 0.0f, g = 0.0f, b = 0.0f;
};

// Bins splats into fixed size screen tiles, then rasterizes tile by tile. Binning first is
// what makes the rasterization pass race free: once every splat that can touch a tile is
// known, that tile's pixels belong to exactly one thread for the rest of the frame, so the
// additive writes into the framebuffer need no atomics and no locks.
class Rasterizer {
public:
    void resize(int width, int height);

    // Bins every splat into tiles (parallel over splats, using per thread scratch bins so
    // no two threads ever write the same bin), then rasterizes every tile into the
    // framebuffer (parallel over tiles, race free because tiles never share a pixel).
    //
    // Public rather than one draw() call so the caller can put a timer around each half:
    // bin and rasterize are separate boundaries in the plan's frame pipeline, and folding
    // them into one call would hide which of the two is actually costing the frame time.
    void bin(const std::vector<Splat>& splats);
    void rasterizeTiles(const std::vector<Splat>& splats, Framebuffer& framebuffer) const;

private:
    static constexpr int kTileSize = 32;

    int m_width = 0, m_height = 0;
    int m_tilesX = 0, m_tilesY = 0;

    // Bins hold indices into the caller's splat array, not copies of the splats. A Splat is
    // 32 bytes and a typical one lands in more than one tile, so copying them cost about
    // 18 MB of traffic per frame between the scatter and the merge. Indices cut that by
    // eight and leave the splat data itself read only and untouched, which is what turned
    // binning from the least scalable stage in the frame into one that keeps up.
    std::vector<std::vector<int>> m_tileBins;

    // Per thread scratch, reused across frames so binning does not reallocate every frame.
    // Indexed [thread][tile]. Grows if a later run asks for more threads than the machine
    // reported at startup.
    std::vector<std::vector<std::vector<int>>> m_scratch;
};