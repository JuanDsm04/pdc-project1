#include "app.hpp"

int main() {
    App app;
    if (!app.init(1280, 720)) {
        app.shutdown();
        return 1;
    }

    app.run();
    app.shutdown();
    return 0;
}
