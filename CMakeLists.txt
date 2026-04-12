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

add_executable(Loom
    src/main.cpp
    src/platform/Window.cpp
    src/gpu/VulkanContext.cpp
    src/ui/ImGuiRenderer.cpp
)

target_include_directories(Loom PRIVATE 
    include
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(imgui PUBLIC
    glfw
    Vulkan::Vulkan
)

target_link_libraries(Loom PRIVATE
    imgui
    glfw
    Vulkan::Vulkan
)

# Test Suite
enable_testing()
add_executable(LoomTests
    tests/core/SlotMapTest.cpp
    tests/core/GraphTest.cpp
    tests/core/TopoTest.cpp
)
target_include_directories(LoomTests PRIVATE include)
target_link_libraries(LoomTests PRIVATE GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(LoomTests)

# Apple-specific config for MoltenVK
if(APPLE)
    target_compile_definitions(Loom PRIVATE VK_USE_PLATFORM_MACOS_MVK)
endif()

# Optional ImGui compile optimization
if(MSVC)
    target_compile_options(imgui PRIVATE /O2)
else()
    target_compile_options(imgui PRIVATE -O3)
endif()
