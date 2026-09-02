#pragma once

#include <cstdint>
#include <string>

// Values that affect workload size or execution mode live here instead of being scattered
// as hard-coded constants. App receives one validated instance and never has to interpret
// raw command-line text.
struct AppConfig {
    int particleCount = 100000;
    int width = 1280;
    int height = 720;
    int threads = 1;
    uint32_t seed = 1337u;

    bool startParallel = true;
    bool vsync = true;
    bool benchmark = false;

    int benchmarkFrames = 120;
    int benchmarkRuns = 10;
};

enum class ParseStatus { Ok, Help, Error };

// Accepts N either as the first positional argument or as --particles N. Every numeric
// option is range checked before SDL or any large allocation is attempted.
ParseStatus parseCommandLine(int argc, char** argv, AppConfig& config, std::string& error);
std::string commandLineHelp(const char* executable);
