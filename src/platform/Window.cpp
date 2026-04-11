#include "platform/Window.hpp"

#include <iostream>
#include <stdexcept>

namespace loom {

Window::Window(int width, int height, const std::string& title) {
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }

    // Set GLFW_CLIENT_API to GLFW_NO_API to prevent creating an OpenGL context
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);  // Simple start

    m_window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!m_window) {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window");
    }
}

Window::~Window() {
    glfwDestroyWindow(m_window);
    glfwTerminate();
}

bool Window::shouldClose() const { return glfwWindowShouldClose(m_window); }

void Window::pollEvents() const { glfwPollEvents(); }

}  // namespace loom
