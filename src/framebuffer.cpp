#include "framebuffer.hpp"

#include <algorithm>
#include <cmath>

#include "parallel.hpp"

namespace {

constexpr int kGammaLutSize = 4096;

// Gamma encoding needs a pow per channel, which at three million channels per frame is
// slower than the whole tone map around it. The curve only ever takes inputs in [0,1]
// after the filmic step, so a table costs one clamp and one load instead.
struct GammaLut {
    uint8_t table[kGammaLutSize];

    GammaLut() {
        for (int i = 0; i < kGammaLutSize; ++i) {
            const float linear = static_cast<float>(i) / (kGammaLutSize - 1);
            const float encoded = std::pow(linear, 1.0f / 2.2f);
            table[i] = static_cast<uint8_t>(encoded * 255.0f + 0.5f);
        }
    }
};

const GammaLut g_gamma;

// Narkowicz fit of the ACES filmic curve. Rolls the highlights off smoothly so bright
// cores desaturate toward white instead of clamping to a flat colored disc.
inline float acesFilmic(float x) {
    if (x < 0.0f) x = 0.0f;
    const float mapped = (x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f);
    return mapped > 1.0f ? 1.0f : mapped;
}

inline uint32_t encode(float linear) {
    const int index = static_cast<int>(linear * (kGammaLutSize - 1) + 0.5f);
    return g_gamma.table[index];
}

}  // namespace

void Framebuffer::resize(int width, int height) {
    m_width  = width;
    m_height = height;
    m_accum.assign(static_cast<size_t>(width) * height * 3, 0.0f);
    m_out.assign(static_cast<size_t>(width) * height, 0xff000000u);
}

void Framebuffer::clear() {
    std::fill(m_accum.begin(), m_accum.end(), 0.0f);
}

void Framebuffer::deposit(int x, int y, float r, float g, float b) {
    if (x < 0 || y < 0 || x >= m_width || y >= m_height) return;
    const size_t i = (static_cast<size_t>(y) * m_width + x) * 3;
    m_accum[i + 0] += r;
    m_accum[i + 1] += g;
    m_accum[i + 2] += b;
}

// Every pixel reads and writes only its own three accumulator slots and its own output
// word, so this needs nothing but the pragma. It went unparallelised far longer than it
// should have and was costing 6.7 ms of every frame on its own, which no amount of thread
// count could touch.
void Framebuffer::tonemap(float exposure) {
    const int count = m_width * m_height;

    #pragma omp parallel for if(g_parallel) num_threads(g_threads) schedule(static)
    for (int i = 0; i < count; ++i) {
        const float r = acesFilmic(m_accum[i * 3 + 0] * exposure);
        const float g = acesFilmic(m_accum[i * 3 + 1] * exposure);
        const float b = acesFilmic(m_accum[i * 3 + 2] * exposure);

        m_out[i] = 0xff000000u | (encode(r) << 16) | (encode(g) << 8) | encode(b);
    }
}
