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

# FetchContent for GLFW
include(FetchContent)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.3.8 # Or a newer version
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(glfw)

# Executable definition
add_executable(Loom
    src/main.cpp
    src/platform/Window.cpp
)

# Include directories
target_include_directories(Loom PUBLIC include)

# Link libraries
target_link_libraries(Loom PRIVATE glfw Vulkan::Vulkan)

# Apple-specific config for MoltenVK
if(APPLE)
    target_compile_definitions(Loom PRIVATE VK_USE_PLATFORM_MACOS_MVK)

    # Required for modern macOS to find the Vulkan loader/layers
    set_target_properties(Loom PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.loom.engine"
    )
endif()
