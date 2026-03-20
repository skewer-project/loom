#include "platform/Window.hpp"
#include <iostream>

int main() {
    loom::Window window(1280, 720, "Loom");

    std::cout << "Initializing Loom..." << std::endl;

    while (!window.shouldClose()) {
        window.pollEvents();
    }

    std::cout << "Shutting down Loom..." << std::endl;

    return 0;
}
