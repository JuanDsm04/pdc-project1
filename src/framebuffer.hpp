#pragma once

#include <cstdint>
#include <vector>

// High dynamic range accumulation target. Everything the simulation draws is added into
// a float buffer with no upper bound, then compressed to 8 bit color in one pass at the
// end of the frame. Accumulating in float is what lets overlapping glows sum past white
// and bloom out instead of clipping the moment two particles overlap.
class Framebuffer {
public:
    void resize(int width, int height);
    void clear();

    void deposit(int x, int y, float r, float g, float b);

    // Compresses the accumulation buffer into the ARGB8888 output buffer.
    void tonemap(float exposure);

    int width() const { return m_width; }
    int height() const { return m_height; }
    int pitch() const { return m_width * 4; }

    const uint32_t* pixels() const { return m_out.data(); }

private:
    int m_width  = 0;
    int m_height = 0;

    std::vector<float>    m_accum;  // three floats per pixel, linear light
    std::vector<uint32_t> m_out;
};
