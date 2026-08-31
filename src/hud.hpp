#pragma once

#include <string>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include "timing.hpp"

// FPS text, drawn top left over the finished frame. The overlay's own text is only
// re-rasterized a few times a second: doing it every frame would let SDL_ttf's font
// rendering cost leak into the very number it is reporting.
class Hud {
public:
    bool init();
    void shutdown();

    // Formats the FPS of the latest completed frame into the pending text, throttled
    // internally so repeated calls between refreshes are cheap no-ops. Takes the FrameTimer
    // rather than a bare number because the per stage timings it also tracks feed the
    // benchmark CSV once step 16 lands, even though the HUD itself only shows FPS.
    void update(const FrameTimer& timer);

    // Rebuilds the texture from the pending text if it changed since the last call, then
    // blits it. Texture creation needs the renderer, which is why this is split from
    // update() instead of building the texture there.
    void render(SDL_Renderer* renderer);

private:
    static constexpr Uint32 kUpdateIntervalMs = 100;

    TTF_Font*    m_font    = nullptr;
    SDL_Texture* m_texture = nullptr;
    int m_texW = 0, m_texH = 0;

    std::string m_text;
    bool   m_dirty       = false;
    Uint32 m_lastUpdate  = 0;
};