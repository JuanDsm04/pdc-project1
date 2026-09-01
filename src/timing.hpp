#pragma once

#include <cstddef>

#include <SDL2/SDL.h>

// Named checkpoints inside one rendered frame. The list mirrors the stages render() and
// update() actually call today. Stages docs/PLAN.md describes but that do not exist in code
// yet are absent rather than reserved as empty slots ahead of time.
enum class Stage {
    Grid,
    Forces,
    Integrate,
    Project,
    BuildSplats,
    Bin,
    Raster,
    Glows,
    Tonemap,
    Present,
    Count
};

// Millisecond duration per stage for the most recently completed frame, plus the frame
// total. begin/end pairs accumulate rather than overwrite, because the fixed timestep loop
// can call update() more than once between two render() calls when a slow frame forces the
// simulation to catch up; without accumulation only the last physics substep would count.
class FrameTimer {
public:
    void beginFrame();
    void begin(Stage stage);
    void end(Stage stage);
    void endFrame();

    float stageMs(Stage stage) const { return m_stageMs[static_cast<size_t>(stage)]; }
    float frameMs() const { return m_frameMs; }

private:
    static constexpr size_t kCount = static_cast<size_t>(Stage::Count);

    double m_freq = 0.0;
    Uint64 m_stageStart[kCount] = {};
    Uint64 m_frameStart = 0;
    float  m_stageMs[kCount] = {};
    float  m_frameMs = 0.0f;
};