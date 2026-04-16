cmake_minimum_required(VERSION 3.21)
project(Loom LANGUAGES CXX)

# --- Build Configuration Defaults ---
# Set a default build type if none was specified (for single-config generators like Make/Ninja)
set(default_build_type "Debug")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to '${default_build_type}' as none was specified.")
    set(CMAKE_BUILD_TYPE "${default_build_type}" CACHE STRING "Choose the type of build." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo" "Sanitize")
endif()

# Define a "Sanitize" build type for Address and Undefined Behavior sanitizers
if(NOT MSVC)
    set(CMAKE_CXX_FLAGS_SANITIZE "-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer" CACHE STRING "Flags used by the C++ compiler during Sanitize builds." FORCE)
    set(CMAKE_C_FLAGS_SANITIZE "-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer" CACHE STRING "Flags used by the C compiler during Sanitize builds." FORCE)
    set(CMAKE_EXE_LINKER_FLAGS_SANITIZE "-fsanitize=address,undefined" CACHE STRING "Flags used for linking binaries during Sanitize builds." FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set build output directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# --- Compiler Warnings ---
if(MSVC)
    add_compile_options(/W4 /WX-) # Level 4 warnings
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

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

target_include_directories(imgui SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/backends
)

target_link_libraries(imgui PUBLIC
    glfw
    Vulkan::Vulkan
)

# Optimization for external code using generator expressions
if(MSVC)
    target_compile_options(imgui PRIVATE $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/O2>)
else()
    target_compile_options(imgui PRIVATE $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-O3>)
endif()


# ImGui Node Editor
set(NODE_EDITOR_DIR "external/imgui-node-editor")
add_library(imgui_node_editor STATIC
    ${NODE_EDITOR_DIR}/imgui_node_editor.cpp
    ${NODE_EDITOR_DIR}/imgui_node_editor_api.cpp
    ${NODE_EDITOR_DIR}/imgui_canvas.cpp
    ${NODE_EDITOR_DIR}/crude_json.cpp
)

target_include_directories(imgui_node_editor SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/${NODE_EDITOR_DIR}
)

# Node editor needs base ImGui headers
target_link_libraries(imgui_node_editor PUBLIC imgui)


# Vulkan Memory Allocator
add_library(vma STATIC external/vma/vma_implementation.cpp)

target_include_directories(vma SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/external/vma
)
target_link_libraries(vma PUBLIC Vulkan::Vulkan)

# Silence VMA compiler warnings since it's external code
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
    target_compile_options(vma PRIVATE -w)
endif()

# --- Shader Compilation ---
find_program(SHADER_COMPILER NAMES glslc glslangValidator glslang)
if(SHADER_COMPILER)
    message(STATUS "Using shader compiler: ${SHADER_COMPILER}")

    # glslangValidator needs -V to target Vulkan/SPIR-V
    set(SHADER_COMPILER_FLAGS "")
    if(SHADER_COMPILER MATCHES "glslangValidator" OR SHADER_COMPILER MATCHES "glslang")
        set(SHADER_COMPILER_FLAGS "-V")
    endif()

    file(GLOB_RECURSE SHADER_SOURCES 
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.comp"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.vert"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.frag"
    )
    set(SHADER_SPV_DIR "${CMAKE_BINARY_DIR}/bin/shaders")
    file(MAKE_DIRECTORY ${SHADER_SPV_DIR})

    set(SHADER_SPV_FILES "")
    foreach(SHADER_FILE ${SHADER_SOURCES})
        get_filename_component(SHADER_NAME ${SHADER_FILE} NAME)
        set(SPV_FILE "${SHADER_SPV_DIR}/${SHADER_NAME}.spv")
        
        add_custom_command(
            OUTPUT ${SPV_FILE}
            COMMAND ${SHADER_COMPILER} ${SHADER_COMPILER_FLAGS} ${SHADER_FILE} -o ${SPV_FILE}
            DEPENDS ${SHADER_FILE}
            COMMENT "Compiling shader ${SHADER_FILE} using ${SHADER_COMPILER}"
        )
        list(APPEND SHADER_SPV_FILES ${SPV_FILE})
    endforeach()

    add_custom_target(CompileShaders ALL DEPENDS ${SHADER_SPV_FILES})
    set(HAS_SHADER_COMPILER ON)
else()
    message(WARNING "Shader compiler not found. Shaders will not be compiled. Some tests may fail.")
    set(HAS_SHADER_COMPILER OFF)
endif()

# --- Loom Engine Core ---
add_library(LoomCore STATIC
    src/platform/Window.cpp
    src/gpu/VulkanContext.cpp
    src/gpu/BindlessHeap.cpp
    src/gpu/TransientImagePool.cpp
    src/gpu/TransientBufferPool.cpp
    src/gpu/PipelineCache.cpp
    src/gpu/DispatchManager.cpp
    src/gpu/DisplayPass.cpp
    src/ui/ImGuiRenderer.cpp
    src/ui/NodeEditorPanel.cpp
    src/core/Nodes.cpp
)
if(HAS_SHADER_COMPILER)
    add_dependencies(LoomCore CompileShaders)
    target_compile_definitions(LoomCore PUBLIC LOOM_HAS_SHADER_COMPILER)
endif()

target_include_directories(LoomCore PUBLIC
    include
)

target_compile_definitions(LoomCore PRIVATE LOOM_SHADER_DIR="${SHADER_SPV_DIR}")

target_link_libraries(LoomCore PUBLIC
    imgui
    imgui_node_editor
    vma
    glfw
    Vulkan::Vulkan
)

# Apple-specific config for MoltenVK
if(APPLE)
    target_compile_definitions(LoomCore PUBLIC VK_USE_PLATFORM_MACOS_MVK)
endif()


# --- Loom Application ---
add_executable(Loom src/main.cpp)
target_link_libraries(Loom PRIVATE LoomCore)


# --- Test Suite ---
enable_testing()
add_executable(LoomTests
    tests/core/SlotMapTest.cpp
    tests/core/GraphTest.cpp
    tests/core/TopoTest.cpp
    tests/core/IdTagTest.cpp
    tests/core/NodeBehaviorTest.cpp
    tests/gpu/ResourcePoolTest.cpp
    tests/gpu/DisplayPassTest.cpp
    tests/core/PushPullTest.cpp
    tests/gpu/ComputeDispatchTest.cpp
    tests/gpu/GraphExecutionTest.cpp
    tests/gpu/WindowResizeTest.cpp
)

target_link_libraries(LoomTests PRIVATE
    LoomCore
    gtest_main
)

include(GoogleTest)
gtest_discover_tests(LoomTests)
