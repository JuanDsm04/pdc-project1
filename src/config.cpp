#include "config.hpp"

#include <algorithm>
#include <charconv>
#include <sstream>
#include <system_error>

#include <omp.h>

namespace {

constexpr int kMinParticles = 1;
constexpr int kMaxParticles = 1000000;
constexpr int kMinWidth = 640;
constexpr int kMaxWidth = 3840;
constexpr int kMinHeight = 480;
constexpr int kMaxHeight = 2160;
constexpr int kMaxBenchmarkFrames = 100000;
constexpr int kMinBenchmarkRuns = 10;
constexpr int kMaxBenchmarkRuns = 100;

template <typename T>
bool parseInteger(const char* text, T& value) {
    if (!text || *text == '\0') return false;
    const char* end = text;
    while (*end != '\0') ++end;
    const auto result = std::from_chars(text, end, value);
    return result.ec == std::errc{} && result.ptr == end;
}

bool inRange(int value, int minimum, int maximum) {
    return value >= minimum && value <= maximum;
}

}  // namespace

ParseStatus parseCommandLine(int argc, char** argv, AppConfig& config, std::string& error) {
    const int availableThreads = std::max(1, omp_get_num_procs());
    config.threads = std::min(availableThreads, std::max(1, omp_get_max_threads()));
    bool positionalParticlesSeen = false;

    auto readValue = [&](int& index, const char* option) -> const char* {
        if (index + 1 >= argc) {
            error = std::string("Falta un valor después de ") + option + '.';
            return nullptr;
        }
        return argv[++index];
    };

    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];

        if (argument == "--help" || argument == "-h") return ParseStatus::Help;
        if (argument == "--serial") {
            config.startParallel = false;
            continue;
        }
        if (argument == "--parallel") {
            config.startParallel = true;
            continue;
        }
        if (argument == "--no-vsync") {
            config.vsync = false;
            continue;
        }
        if (argument == "--benchmark") {
            config.benchmark = true;
            config.vsync = false;
            continue;
        }

        const char* valueText = nullptr;
        if (argument == "--particles" || argument == "-n") {
            valueText = readValue(i, argument.c_str());
            if (!valueText) return ParseStatus::Error;
            int value = 0;
            if (!parseInteger(valueText, value) ||
                !inRange(value, kMinParticles, kMaxParticles)) {
                error = "--particles debe estar entre 1 y 1000000.";
                return ParseStatus::Error;
            }
            config.particleCount = value;
            positionalParticlesSeen = true;
        } else if (argument == "--width") {
            valueText = readValue(i, argument.c_str());
            if (!valueText) return ParseStatus::Error;
            int value = 0;
            if (!parseInteger(valueText, value) || !inRange(value, kMinWidth, kMaxWidth)) {
                error = "--width debe estar entre 640 y 3840.";
                return ParseStatus::Error;
            }
            config.width = value;
        } else if (argument == "--height") {
            valueText = readValue(i, argument.c_str());
            if (!valueText) return ParseStatus::Error;
            int value = 0;
            if (!parseInteger(valueText, value) || !inRange(value, kMinHeight, kMaxHeight)) {
                error = "--height debe estar entre 480 y 2160.";
                return ParseStatus::Error;
            }
            config.height = value;
        } else if (argument == "--threads" || argument == "-t") {
            valueText = readValue(i, argument.c_str());
            if (!valueText) return ParseStatus::Error;
            int value = 0;
            if (!parseInteger(valueText, value) || !inRange(value, 1, availableThreads)) {
                std::ostringstream message;
                message << "--threads debe estar entre 1 y " << availableThreads << '.';
                error = message.str();
                return ParseStatus::Error;
            }
            config.threads = value;
        } else if (argument == "--seed") {
            valueText = readValue(i, argument.c_str());
            if (!valueText) return ParseStatus::Error;
            uint32_t value = 0;
            if (!parseInteger(valueText, value)) {
                error = "--seed debe ser un entero entre 0 y 4294967295.";
                return ParseStatus::Error;
            }
            config.seed = value;
        } else if (argument == "--bench-frames") {
            valueText = readValue(i, argument.c_str());
            if (!valueText) return ParseStatus::Error;
            int value = 0;
            if (!parseInteger(valueText, value) ||
                !inRange(value, 1, kMaxBenchmarkFrames)) {
                error = "--bench-frames debe estar entre 1 y 100000.";
                return ParseStatus::Error;
            }
            config.benchmarkFrames = value;
        } else if (argument == "--bench-runs") {
            valueText = readValue(i, argument.c_str());
            if (!valueText) return ParseStatus::Error;
            int value = 0;
            if (!parseInteger(valueText, value) ||
                !inRange(value, kMinBenchmarkRuns, kMaxBenchmarkRuns)) {
                error = "--bench-runs debe estar entre 10 y 100.";
                return ParseStatus::Error;
            }
            config.benchmarkRuns = value;
        } else if (!argument.empty() && argument.front() != '-') {
            if (positionalParticlesSeen) {
                error = "Solo se permite un valor posicional para N.";
                return ParseStatus::Error;
            }
            int value = 0;
            if (!parseInteger(argument.c_str(), value) ||
                !inRange(value, kMinParticles, kMaxParticles)) {
                error = "N debe estar entre 1 y 1000000.";
                return ParseStatus::Error;
            }
            config.particleCount = value;
            positionalParticlesSeen = true;
        } else {
            error = "Opción desconocida: " + argument;
            return ParseStatus::Error;
        }
    }

    return ParseStatus::Ok;
}

std::string commandLineHelp(const char* executable) {
    std::ostringstream output;
    output
        << "Uso: " << executable << " [N] [opciones]\n\n"
        << "N también puede indicarse con --particles N.\n\n"
        << "Opciones:\n"
        << "  -n, --particles N     Partículas (1..1000000, default 100000)\n"
        << "      --width W         Ancho (640..3840, default 1280)\n"
        << "      --height H        Alto (480..2160, default 720)\n"
        << "  -t, --threads T       Hilos OpenMP (default: procesadores disponibles)\n"
        << "      --serial          Iniciar con los kernels secuenciales\n"
        << "      --parallel        Iniciar con OpenMP (default)\n"
        << "      --seed S          Semilla determinista (default 1337)\n"
        << "      --no-vsync        No limitar la presentación al refresco del monitor\n"
        << "      --benchmark       Ejecutar pruebas CPU sin crear ventana y emitir CSV\n"
        << "      --bench-frames F  Frames medidos por prueba (default 120)\n"
        << "      --bench-runs R    Mediciones por hilo (10..100, default 10)\n"
        << "  -h, --help            Mostrar esta ayuda\n";
    return output.str();
}
