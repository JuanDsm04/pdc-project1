#include <SDL2/SDL.h>
#include <omp.h>
#include <cstdio>

int main() {
    SDL_version compiled;
    SDL_VERSION(&compiled);

    std::printf("infinity gauntlet screensaver\n");
    std::printf("sdl2 %d.%d.%d\n", compiled.major, compiled.minor, compiled.patch);
    std::printf("openmp max threads: %d\n", omp_get_max_threads());
    return 0;
}
