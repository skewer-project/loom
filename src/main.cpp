#include <cstdlib>  // For EXIT_SUCCESS and EXIT_FAILURE
#include <iostream>

#include "core/Graph.hpp"
#include "gpu/VulkanContext.hpp"
#include "platform/Window.hpp"
#include "ui/ImGuiRenderer.hpp"
#include "ui/NodeEditorPanel.hpp"

int main() {
    try {
        std::cout << "Initializing Loom..." << std::endl;

        loom::platform::Window window(1280, 720, "Loom");

        loom::gpu::VulkanContext vulkan;
        vulkan.init(window, "Loom");

        loom::ui::ImGuiRendererCreateInfo imguiInfo{};
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

        loom::ui::ImGuiRenderer imgui;
        imgui.init(imguiInfo);

        loom::core::Graph graph;
        loom::ui::NodeEditorPanel nodeEditor(&graph);

        std::cout << "Loom initialized successfully." << std::endl;

        while (!window.shouldClose()) {
            window.pollEvents();

            // Build UI
            imgui.beginFrame();
            nodeEditor.draw("Loom Node Editor");

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
