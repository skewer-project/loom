cmake_minimum_required(VERSION 3.14)
project(Loom LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set build output directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Find Vulkan SDK on system
find_package(Vulkan REQUIRED)

# FetchContent for GLFW and GTest
include(FetchContent)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.3.8
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(glfw googletest)

# --- Vendored Libraries ---
# Base DearImGui
add_library(imgui STATIC
    external/imgui/imgui.cpp
    external/imgui/imgui_draw.cpp
    external/imgui/imgui_tables.cpp
    external/imgui/imgui_widgets.cpp
    external/imgui/imgui_demo.cpp
    external/imgui/backends/imgui_impl_glfw.cpp
    external/imgui/backends/imgui_impl_vulkan.cpp
)

target_include_directories(imgui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/backends
)

target_link_libraries(imgui PUBLIC
    glfw
    Vulkan::Vulkan
)

# Optional ImGui compile optimization
if(MSVC)
    target_compile_options(imgui PRIVATE /O2)
else()
    target_compile_options(imgui PRIVATE -O3)
endif()


# ImGui Node Editor
set(NODE_EDITOR_DIR "external/imgui-node-editor")
add_library(imgui_node_editor STATIC
    ${NODE_EDITOR_DIR}/imgui_node_editor.cpp
    ${NODE_EDITOR_DIR}/imgui_node_editor_api.cpp
    ${NODE_EDITOR_DIR}/imgui_canvas.cpp
    ${NODE_EDITOR_DIR}/crude_json.cpp
)

target_include_directories(imgui_node_editor PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/${NODE_EDITOR_DIR}
)

# Node editor needs base ImGui headers
target_link_libraries(imgui_node_editor PUBLIC imgui)


# --- Loom Engine Core ---
# We build the engine as a library so BOTH the main app and tests can link to it
add_library(LoomCore STATIC
    src/platform/Window.cpp
    src/gpu/VulkanContext.cpp
    src/ui/ImGuiRenderer.cpp
    src/ui/NodeEditorPanel.cpp
)

target_include_directories(LoomCore PUBLIC include)

# Because we link imgui and node_editor PUBLICLY, anything that links to
# LoomCore automatically gets their include directories too!
target_link_libraries(LoomCore PUBLIC
    imgui
    imgui_node_editor
    glfw
    Vulkan::Vulkan
)

# Apple-specific config for MoltenVK (Applied to Core so it propagates)
if(APPLE)
    target_compile_definitions(LoomCore PUBLIC VK_USE_PLATFORM_MACOS_MVK)
endif()


# --- Loom Application (The Executable) ---
add_executable(Loom src/main.cpp)

# Just link the Core. CMake handles all the transitive dependencies (ImGui, Vulkan, etc.)
target_link_libraries(Loom PRIVATE LoomCore)


# --- Test Suite ---
enable_testing()
add_executable(LoomTests
    tests/core/SlotMapTest.cpp
    tests/core/GraphTest.cpp
    tests/core/TopoTest.cpp
    tests/core/IdTagTest.cpp
)

# Link tests to our engine library and the Google Test runner
target_link_libraries(LoomTests PRIVATE
    LoomCore
    gtest_main
)

include(GoogleTest)
gtest_discover_tests(LoomTests)
