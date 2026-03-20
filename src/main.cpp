#include "platform/Window.hpp"
#include <iostream>

int main() {
    try {
        loom::Window window(800, 600, "Loom Engine");

        std::cout << "Window initialized successfully!" << std::endl;

        while (!window.shouldClose()) {
            window.pollEvents();
        }

    } catch (const std::exception& e) {
        std::cerr << "Window Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
