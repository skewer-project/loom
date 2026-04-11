#include <cstdlib>  // For EXIT_SUCCESS and EXIT_FAILURE
#include <iostream>

#include "gpu/VulkanContext.hpp"
#include "platform/Window.hpp"
#include "ui/ImGuiRenderer.hpp"

int main() {
    try {
        std::cout << "Initializing Loom..." << std::endl;

        loom::Window window(1280, 720, "Loom");

        loom::VulkanContext vulkan;
        vulkan.init(window, "Loom");
        // init() takes const loom::Window& — pass window directly,
        // not window.getNativeWindow(). This is intentional.

        loom::ImGuiRendererCreateInfo imguiInfo{};
        imguiInfo.window = window.getNativeWindow();
        imguiInfo.instance = vulkan.getVkInstance();
        imguiInfo.physicalDevice = vulkan.getPhysicalDevice();
        imguiInfo.device = vulkan.getDevice();
        imguiInfo.graphicsQueueFamily = vulkan.getGraphicsQueueFamily();
        imguiInfo.graphicsQueue = vulkan.getGraphicsQueue();
        imguiInfo.descriptorPool = vulkan.getDescriptorPool();
        imguiInfo.colorFormat = vulkan.getSwapchainImageFormat();
        imguiInfo.imageCount = static_cast<uint32_t>(vulkan.getSwapchainImageCount());
        imguiInfo.minImageCount = 2;

        loom::ImGuiRenderer imgui;
        imgui.init(imguiInfo);

        std::cout << "Loom initialized successfully." << std::endl;

        while (!window.shouldClose()) {
            window.pollEvents();
            vulkan.drawFrame(imgui);
        }

        vulkan.waitIdle();
        // Must be called before any destructor runs.
        // Ensures GPU finishes all in-flight work before
        // Vulkan resources are destroyed.

        std::cout << "Shutting down Loom..." << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
