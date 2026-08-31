#include "hud.hpp"

#include <cstdio>

namespace {

constexpr int kFontSize = 14;
constexpr int kPadding  = 8;
constexpr int kMarginX  = 10;
constexpr int kMarginY  = 10;

constexpr SDL_Color kTextColor = {230, 230, 230, 255};

// The particle cloud can put a bright glow directly behind the top left corner, so the
// overlay gets its own translucent backing rather than relying on contrast with whatever
// the scene happens to render there.
constexpr Uint8 kBackgroundAlpha = 170;

const char* kFontPath = "assets/DejaVuSansMono.ttf";

}  // namespace

bool Hud::init() {
    if (TTF_Init() != 0) {
        std::fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        return false;
    }

    m_font = TTF_OpenFont(kFontPath, kFontSize);
    if (!m_font) {
        std::fprintf(stderr, "TTF_OpenFont failed for %s: %s\n", kFontPath, TTF_GetError());
        return false;
    }

    return true;
}

void Hud::shutdown() {
    if (m_texture) SDL_DestroyTexture(m_texture);
    if (m_font) TTF_CloseFont(m_font);
    m_texture = nullptr;
    m_font = nullptr;
    TTF_Quit();
}

void Hud::update(const FrameTimer& timer) {
    const Uint32 now = SDL_GetTicks();
    if (m_lastUpdate != 0 && now - m_lastUpdate < kUpdateIntervalMs) return;
    m_lastUpdate = now;

    const float frameMs = timer.frameMs();
    const float fps = frameMs > 0.0f ? 1000.0f / frameMs : 0.0f;

    char buf[32];
    std::snprintf(buf, sizeof(buf), "fps %5.1f", fps);

    if (buf == m_text) return;
    m_text  = buf;
    m_dirty = true;
}

void Hud::render(SDL_Renderer* renderer) {
    if (m_dirty) {
        if (m_texture) {
            SDL_DestroyTexture(m_texture);
            m_texture = nullptr;
        }

        SDL_Surface* surface =
            TTF_RenderUTF8_Blended_Wrapped(m_font, m_text.c_str(), kTextColor, 0);
        if (surface) {
            m_texture = SDL_CreateTextureFromSurface(renderer, surface);
            m_texW = surface->w;
            m_texH = surface->h;
            SDL_FreeSurface(surface);
        }

        m_dirty = false;
    }

    if (!m_texture) return;

    const SDL_Rect background{kMarginX - kPadding, kMarginY - kPadding,
                              m_texW + kPadding * 2, m_texH + kPadding * 2};

    SDL_BlendMode previousBlend;
    SDL_GetRenderDrawBlendMode(renderer, &previousBlend);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, kBackgroundAlpha);
    SDL_RenderFillRect(renderer, &background);
    SDL_SetRenderDrawBlendMode(renderer, previousBlend);

    const SDL_Rect destination{kMarginX, kMarginY, m_texW, m_texH};
    SDL_RenderCopy(renderer, m_texture, nullptr, &destination);
}