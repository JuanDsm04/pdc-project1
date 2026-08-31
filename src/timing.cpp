#include "timing.hpp"

void FrameTimer::beginFrame() {
    if (m_freq == 0.0) m_freq = static_cast<double>(SDL_GetPerformanceFrequency());

    for (float& ms : m_stageMs) ms = 0.0f;
    m_frameStart = SDL_GetPerformanceCounter();
}

void FrameTimer::begin(Stage stage) {
    m_stageStart[static_cast<size_t>(stage)] = SDL_GetPerformanceCounter();
}

void FrameTimer::end(Stage stage) {
    const Uint64 now = SDL_GetPerformanceCounter();
    const size_t index = static_cast<size_t>(stage);
    const Uint64 elapsed = now - m_stageStart[index];

    m_stageMs[index] += static_cast<float>(static_cast<double>(elapsed) / m_freq * 1000.0);
}

void FrameTimer::endFrame() {
    const Uint64 now = SDL_GetPerformanceCounter();
    const Uint64 elapsed = now - m_frameStart;
    m_frameMs = static_cast<float>(static_cast<double>(elapsed) / m_freq * 1000.0);
}