# Loom

A C++20 project utilizing GLFW and modern CMake practices.

## Prerequisites

- **CMake** (3.14 or newer)
- **C++20 Compiler** (e.g., GCC 10+, Clang 10+, or MSVC 19.26+)
- **Git** (for FetchContent)

## Building the Project

This project uses CMake to manage the build process. All compiled binaries and objects will be placed in the `build/` directory.

1. **Create and enter the build directory:**
   ```bash
   mkdir build
   cd build
   ```

2. **Configure the project:**
   ```bash
   cmake ..
   ```
   *Note: GLFW will be automatically downloaded and configured during this step via `FetchContent`.*

3. **Build the project:**
   ```bash
   cmake --build .
   ```

## Structure

- `src/`: Source files.
- `include/`: Public headers.
- `build/`: Compilation artifacts and binaries.
- `build/bin/`: Compiled executable files.
