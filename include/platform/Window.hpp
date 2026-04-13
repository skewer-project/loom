#pragma once

#include <GLFW/glfw3.h>

#include <string>

namespace loom::platform {

class Window {
  public:
    Window(int width, int height, const std::string& title);
    ~Window();

    // Prevent copying
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool shouldClose() const;
    void pollEvents() const;

    GLFWwindow* getNativeWindow() const { return m_window; }

    bool wasResized() const { return m_framebufferResized; }
    void resetResizedFlag() { m_framebufferResized = false; }

  private:
    GLFWwindow* m_window;
    bool m_framebufferResized = false;
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
};

}  // namespace loom::platform
