# Loom

Loom is a node-based deep compositor. 

---

## Development

This section provides technical details on the project structure, dependencies, and instructions for setting up your local environment to contribute.

### Project Structure

- `src/`: Core implementation files (.cpp).
- `include/`: Public header files (.hpp).
- `external/`: Third-party libraries and dependencies (e.g., Dear ImGui).
- `shaders/`: GLSL/Vulkan shader source files.
- `build/`: Compilation artifacts and binaries.
- `build/bin/`: Location of the compiled executable files.

### Prerequisites

To build and run Loom, you will need:
- **Vulkan SDK**: Required for graphics rendering.
- **C++20 Compiler**: Such as GCC 10+, Clang 10+, or MSVC 19.26+.
- **CMake**: Version 3.14 or newer.
- **Git**: For dependency management.

### Vulkan Dependencies

Loom requires the **Vulkan SDK** to be installed and correctly configured on your system.

1.  **Download and Install**: Get the latest SDK from [LunarG's Vulkan SDK page](https://vulkansdk.lunarg.com/).
2.  **Environment Variables**: Ensure your environment is configured so the application can find the Vulkan drivers and layers.
    - **macOS/Linux**: Source the `setup-env.sh` script included in the SDK:
      ```bash
      source /path/to/VulkanSDK/version/setup-env.sh
      ```
      *Tip: Add this to your `~/.zshrc` or `~/.bashrc` to make it permanent.*
    - **Windows**: The installer typically sets the `VULKAN_SDK` environment variable automatically.

### Building and Running (Standard)

1. **Create and enter the build directory:**
   ```bash
   mkdir build
   cd build
   ```

2. **Configure the project:**
   You can specify the build type using `-DCMAKE_BUILD_TYPE`. Options include `Debug` (default), `Release`, and `Sanitize` (enables Address and Undefined Behavior sanitizers).
   ```bash
   cmake -DCMAKE_BUILD_TYPE=Debug ..
   ```
   *Note: Dependencies like GLFW and GoogleTest will be automatically downloaded during this step.*

3. **Build the project:**
   ```bash
   cmake --build . --parallel
   ```

4. **Run the executable or tests:**
   ```bash
   ./bin/Loom
   ./bin/LoomTests
   ```

### Building and Running (Modern Presets)

If you have CMake 3.21+ installed, you can use presets for a more streamlined workflow:

1. **Configure with a preset:**
   ```bash
   cmake --preset debug
   # or 'release', 'sanitize'
   ```

2. **Build with a preset:**
   ```bash
   cmake --build --preset debug
   ```

3. **Run tests with a preset:**
   ```bash
   ctest --preset debug
   ```

### Development Workflow

#### Code Formatting
We use `clang-format` and the `pre-commit` framework to maintain a consistent code style (based on Google style with 4-space indents).

1.  **Install `pre-commit`**:
    - **macOS**: `brew install pre-commit`
    - **Windows/Linux**: `pip install pre-commit`
2.  **Activate the hooks**: Run `pre-commit install` in the project root.

Once activated, `git commit` will automatically format your code. If any files are reformatted, the commit will be stopped; you must `git add` the changes before attempting the commit again.

#### CI/CD
GitHub Actions run style checks on every push and pull request. Use the pre-commit hooks locally to ensure your code matches the project's formatting standards and passes CI.

---

## Built With
* **C++20 & Vulkan 1.3** — Core engine and GPU infrastructure.
* **Dear ImGui** — Node editor and viewport UI.
* **AI Collaboration** — **Google Gemini** and **Anthropic Claude Sonnet** assisted in architectural review, feature planning, programming, bug fixing, and documentation drafting.
