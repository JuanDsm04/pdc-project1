#include "app.hpp"

#include <iostream>

#include "config.hpp"

int main(int argc, char** argv) {
    AppConfig config;
    std::string error;
    const ParseStatus status = parseCommandLine(argc, argv, config, error);
    if (status == ParseStatus::Help) {
        std::cout << commandLineHelp(argv[0]);
        return 0;
    }
    if (status == ParseStatus::Error) {
        std::cerr << "Error: " << error << "\n\n" << commandLineHelp(argv[0]);
        return 2;
    }

    App app;
    if (!app.init(config)) {
        app.shutdown();
        return 1;
    }

    app.run();
    app.shutdown();
    return 0;
}
