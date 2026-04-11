#include <cstdlib>  // For EXIT_SUCCESS and EXIT_FAILURE
#include <iostream>

#include "gpu/VulkanContext.hpp"
#include "platform/Window.hpp"

int main() {
    try {
        std::cout << "Initializing Loom..." << std::endl;

        loom::Window window(1280, 720, "Loom");
        loom::VulkanContext vulkan;
        vulkan.init(window, "Loom");

        std::cout << "Loom successfully initialized!" << std::endl;

        while (!window.shouldClose()) {
            window.pollEvents();
        }

        std::cout << "Shutting down Loom..." << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Fatal Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
