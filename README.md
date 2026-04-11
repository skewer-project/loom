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

## Development

### Code Formatting
This project uses `clang-format` and the `pre-commit` framework to maintain a consistent code style.

1.  **Install `pre-commit`**:
    - **macOS**: `brew install pre-commit`
    - **Windows/Linux**: `pip install pre-commit`
2.  **Activate the hooks**: Run `pre-commit install` in the project root.

Once activated, `git commit` will automatically format your C++ code. If any files are reformatted, the commit will be stopped so you can review and re-stage the changes.

## Structure

- `src/`: Source files.
- `include/`: Public headers.
- `build/`: Compilation artifacts and binaries.
- `build/bin/`: Compiled executable files.
