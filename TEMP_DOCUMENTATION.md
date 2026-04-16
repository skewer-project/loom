# Loom Headless Data Model - Implementation Documentation

This document tracks the architectural decisions, implementation details, and evolution of the Loom headless C++ data model.

---

## Phase 1.1: Generational Arena (Slot Map)
**Objective:** Create a high-performance, safety-first container for managing object lifetimes without raw pointers.

### Implementation Details
- **Strongly-Typed Handles:** Created a templated `Handle<Tag>` struct containing a 32-bit `index` and 32-bit `generation`. This prevents accidental type-mixing (e.g., passing a `PinHandle` where a `NodeHandle` is expected).
- **Storage Entry (`Slot<T>`):** Uses `std::optional<T>` to manage the lifecycle of data. This ensures that when an item is removed, its destructor is called immediately, preventing leaks for non-trivial types.
- **SlotMap Container:** Implemented a free-list stack mechanism for $O(1)$ insertion and removal.
- **Generation Ticker:** On removal, the slot's generation is incremented. If the ticker wraps around, it skips `0` to avoid matching the default "invalid" state.

### Key Decisions
- **Manual Destruction:** Chose to explicitly `.reset()` the optional data on removal to ensure deterministic cleanup.
- **Pure C++:** Absolutely zero dependencies on external libraries or rendering headers to maintain the "headless" requirement.

---

## Phase 1.2: DAG Data Model (Nodes, Pins, Links)
**Objective:** Define the fundamental structures for the compositor's graph logic.

### Core Structs
- **Pin:** Contains direction (Input/Output), type (Float/DeepBuffer), and handles to the owning node and connected links.
- **Link:** Stores the relationship between an Output pin and an Input pin.
- **Node:** Stores its type, name, and vectors of input/output pins.
- **Graph Container:** Acts as the "God Object" factory. It owns the `SlotMap`s for all three types and manages their inter-relationships.

### Key Decisions
- **Handle by Value:** All handles are passed by value (64-bit total), making them as efficient as a raw pointer but significantly safer.
- **Cascading Deletion:** Implemented logic where removing a node automatically identifies and removes all connected links and child pins, preventing "orphan" data in the SlotMaps.
- **Dirty Tracking:** Introduced a `bool isDirty` flag on nodes to facilitate future lazy evaluation of the compositor pipeline.

---

## Phase 1.3: Topological Sort & Cycle Detection
**Objective:** Enforce DAG integrity and determine the execution order for the evaluator.

### Algorithms
- **Cycle Detection (DFS):** An iterative Depth-First Search implemented in `isReachable`. It prevents the creation of any link that would result in a cycle.
- **Topological Sorting (Kahn's Algorithm):** Generates a linear execution order based on in-degree counts.
- **Lazy Evaluation:** The topological order is cached and only recomputed (`isTopoDirty`) when the graph structure changes.

### Engineering Rationale & Refactoring
- **Allocation Pressure Fix:** Replaced local `std::vector<bool> visited` heap allocations with a persistent `mutable std::vector<uint8_t> m_visitedScratch` member.
- **Byte vs. Bit Vectors:** Chose `uint8_t` over `bool` for the scratchpad. This allows the compiler to use `std::fill` (optimized to `memset`), which is faster than the bit-masking logic required for `std::vector<bool>`.
- **Defensive Programming:** Implemented a "Get-and-Check" pattern. Every SlotMap lookup is checked for null before dereferencing, protecting against stale handles.
- **Schema Integrity:** Reverted temporary "dummy pins" used in early testing. Nodes now strictly follow their logical schema (e.g., `Constant` has 0 inputs), and tests were updated to respect these boundaries.

### Phase 1.3 Engineering Post-Mortem: Anatomy of the Segmentation Faults
During the implementation of cycle detection and topological sorting, several `EXC_BAD_ACCESS` (address `0x0`) errors were encountered. These provided critical insights into the safety requirements of a handle-based system.

#### 1. The Schema Mismatch (Logical Failure)
*   **The Bug:** The initial implementation of `setupNodePins` for `NodeType::Constant` only created an **Output** pin. However, the test code was written with the incorrect assumption that all nodes have at least one input and output, attempting to access `pA->inputs[0]`.
*   **The Result:** Since `std::vector::operator[]` does not perform bounds checking, it calculated a memory offset from a null internal pointer (as the vector was empty), resulting in an immediate `nullptr` dereference.
*   **The Fix:** Updated the unit tests to respect the actual logical schema of the nodes (e.g., intermediate chain links now use `Passthrough` nodes which possess both Input and Output pins).

#### 2. Stale Handle Dereferencing (Traversal Failure)
*   **The Bug:** During graph traversal (DFS and Kahn’s Algorithm), the code iterates through a pin's `links` vector. If a link had been removed but its handle still resided in the vector (a "dangling handle"), `links.get(lh)` would return `nullptr`.
*   **The Result:** The code immediately attempted to access a member of that link (e.g., `link->endPin`), triggering a crash.
*   **The Fix:** Implemented a **"Get-and-Check"** pattern. Every lookup from a `SlotMap` is now followed by an explicit null check before any member access.

#### 3. SlotMap Capacity Desync
*   **The Bug:** Traversal buffers like `visited` and `inDegree` were initialized using `nodes.capacity()`. If the underlying `SlotMap` was forced to reallocate (grow) *during* a complex operation, a valid index from a new handle could exceed the bounds of the buffers allocated at the start of the call.
*   **The Fix:** In addition to the allocation pressure fix, the code now snapshots the capacity at the start of the traversal and includes explicit bounds checks (`current.index >= cap`) to ensure memory safety even if the container size changes.

---

## Phase 2: UI Integration & Node Editor Binding
**Objective:** Provide a modern, interactive interface for manipulating the compositor graph while maintaining a strict boundary between UI and Core logic.

### Core Components
- **ImGuiRenderer (Vulkan Backend):** 
    - Manages the `ImGui_ImplVulkan` lifecycle.
    - **Dynamic Rendering Integration:** Configures `VkPipelineRenderingCreateInfo` to match the swapchain format, allowing ImGui to draw directly into the command buffer without an explicit `VkRenderPass`.
    - **Descriptor Management:** Initializes a dedicated `VkDescriptorPool` with `VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT` to support dynamic image previews in the future.
- **NodeEditorPanel (The Bridge):**
    - Acts as the model-view-controller. It iterates the `core::Graph` and emits `imgui-node-editor` (blueprints-style) primitives.
    - **Immediate Mode Mapping:** On every frame, it queries the `Graph`'s slotmaps and generates the visual representation. It does not store a shadow copy of the graph, ensuring the UI always reflects the "source of truth."

### Design & Architecture Decisions
- **ID Namespace Management:** `imgui-node-editor` requires globally unique 64-bit IDs for nodes, pins, and links. Since our `Handle<Tag>` indices overlap across types (e.g., Node index 0 and Pin index 0), we implemented a **Type-Shifting ID Strategy**:
    - **Nodes:** `raw_handle`
    - **Pins:** `raw_handle | (1ULL << 60)`
    - **Links:** `raw_handle | (1ULL << 61)`
    - This ensures no ID collisions while allowing $O(1)$ recovery of the original core handle.
- **Transactional Link Creation:** Used the `ed::BeginCreate()` and `ed::AcceptNewItem()` API to implement a "Drag-and-Drop" link workflow. The UI only finalizes the visual link if `graph.tryAddLink()` returns `true`, automatically providing user feedback for type-mismatches or cycle violations.
- **Visual Semantics:** 
    - **Pin Coloring:** Defined a color palette for `PinType`: `Float` (Cyan) and `DeepBuffer` (Gold). 
    - **Node Headers:** Distinct colors for `NodeType` (e.g., `Merge` is Green, `Constant` is Blue) to improve graph scannability.

### Phase 2 Engineering Post-Mortem: UI-Core Synchronization
#### 1. The "Ghost Link" Deletion Crash
*   **The Bug:** When a node was deleted via the UI, the `core::Graph` performed a cascading deletion of all attached links. However, the UI loop was still iterating over the `imgui-node-editor` link state from the previous frame.
*   **The Result:** Attempting to query the properties of a link handle that had already been purged from the `SlotMap` triggered a null-check assertion.
*   **The Fix:** Implemented a **deferred deletion queue**. Deletion requests from the UI are buffered and executed at the *very end* of the frame, after the editor has finished its internal cleanup.

#### 2. Layout Persistence vs. Handle Stability
*   **The Problem:** `imgui-node-editor` saves node positions to an `.ini` file based on IDs. Because our handles use a `generation` ticker, if a node is deleted and a new one is created in the same slot, the ID changes, and the layout is lost.
*   **The Decision:** Accepted this as a feature, not a bug. It prevents a new node from "inheriting" the position of a completely unrelated deleted node, forcing a clean layout for new additions.

---

## Phase 3: Vulkan Resource & Bindless Infrastructure
**Objective:** Build a robust, high-performance backend for managing GPU resources using Vulkan Memory Allocator (VMA) and Bindless descriptors.

### Implementation Details
- **Strongly-Typed Handles:** Defined `ImageHandle` and `BufferHandle` structs containing `poolIndex`, `bindlessSlot`, and `generation`.
- **Bindless Descriptor Heap:** Implemented a global heap managing a single `VkDescriptorSet` with 2048 slots for both storage images and buffers. Enabled `descriptorBindingPartiallyBound` and `UpdateAfterBind` features.
- **Transient Resource Pools:** 
    - **Recycling:** Images are reused based on `ImageSpec` bitmask matches; buffers are reused based on size-bucket bounds (requested size up to 2x).
    - **Hazard Management:** Uses a `pendingReleases` queue that is flushed only after the CPU waits for the frame's GPU fence, ensuring resources are not recycled while still in use by the hardware.

### Key Decisions
- **VMA for Memory Management:** Standardized on Vulkan Memory Allocator to handle sub-allocation and heap pressure automatically.
- **Generational Safety:** Increments a generation counter on every resource recycle to detect and abort on stale handle usage.

### Phase 3 Engineering Post-Mortem: CI Stability and API Rigor

#### 1. The "Zombie Device" Destruction Crash
*   **The Bug:** In CI environments (GitHub Actions) lacking a physical GPU, `VulkanContext::init` would fail. The resulting destructor call would pass `VK_NULL_HANDLE` as the `VkDevice` to functions like `vkDestroyCommandPool`.
*   **The Result:** `SIGABRT` / `Subprocess aborted` errors. While Vulkan allows destroying null objects, it crashes if the *device* handle itself is null.
*   **The Fix:** Implemented a **Null-Safe Destructor** pattern. All device-dependent cleanup is now strictly guarded by `if (m_device != VK_NULL_HANDLE)`.

#### 2. VMA Warning Suppression
*   **The Problem:** The VMA header generated ~800 warnings regarding nullability and unused parameters, obscuring actual build errors.
*   **The Fix:** Isolated VMA into a dedicated `vma_implementation.cpp`, marked the include directory as `SYSTEM` in CMake, and applied the `-w` (suppress all warnings) flag specifically to the VMA target.

#### 3. Descriptor Indexing Capability Desync
*   **The Problem:** Some Vulkan implementations require specific flags during `VkDescriptorSetLayout` creation when using `UpdateAfterBind`.
*   **The Fix:** Explicitly enabled `VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT` and correctly chained `VkPhysicalDeviceDescriptorIndexingFeatures` in the device creation `pNext` chain.

---

## Project Structure & Namespaces
**Standard Adopted:** Nested namespaces (`loom::core`, `loom::gpu`, `loom::platform`, `loom::ui`).

### Rationale
- **Architectural Walls:** Prevents "spaghetti code" by making cross-layer dependencies explicit.
- **Namespace Aliases:** In `.cpp` files, we use `namespace core = loom::core;` to balance brevity with clarity.
- **Directory Mirroring:** The logical namespace structure directly mirrors the physical directory structure for developer predictability.

---

## Phase 4: Push-Dirty / Pull-Eval Engine
**Objective:** Implement a two-pass execution model to minimize redundant work and manage Vulkan resource lifetimes across frames.

### Implementation Details
- **EvaluationContext:** A persistent structure passed through the pull phase.
    - **Caching:** Stores `ImageHandle`s keyed by the upstream Output `PinHandle` (`generation << 32 | index`).
    - **Deferred GC:** Maintains vectors for `pendingImageReleases` and `pendingBufferFrees` to be processed after GPU synchronization.
- **Polymorphic Nodes:** Refactored `Node` to be an abstract base class with a virtual `evaluate(EvaluationContext& ctx)` method. Subclasses (`Constant`, `Merge`, `Viewer`, `Passthrough`) implement specific logic.
- **Two-Pass Logic:**
    1. **Push-Dirty:** When a link is added/removed or a node's parameter changes, `markDirty` uses a BFS to propagate the dirty flag downstream, pruning branches that are already dirty.
    2. **Pull-Eval:** Nodes recursively call `pullInput`. 
        - **Cache Hit:** If the upstream node is clean, the cached handle is returned immediately.
        - **Cache Eviction:** If the upstream node is dirty, its old cached outputs are marked for release before re-evaluation.
- **Stub Node Execution:**
    - **ConstantNode:** Allocates a host-visible staging buffer, fills it with color data, and records a `vkCmdCopyBufferToImage` into the frame's command buffer.
    - **MergeNode:** Pulls two inputs (triggering upstream eval if needed) and writes a purple placeholder to its output.

### Key Decisions
- **unique_ptr in SlotMap:** Changed `SlotMap<Node, NodeHandle>` to `SlotMap<std::unique_ptr<Node>, NodeHandle>` to allow polymorphic behavior while preserving handle stability.
- **RAII Re-entrancy Guard:** Used a scoped `EvalGuard` to manage the `isEvaluating` flag, providing robust runtime cycle detection during the pull phase.
- **Start-of-Frame GC:** Added `Graph::startFrameGC` to evict cached resources associated with pins that were deleted since the last frame, preventing memory leaks in the transient pool.

### Phase 4 Engineering Post-Mortem: Staging and Synchronization
#### 1. The Staging Buffer Size Mismatch
*   **The Bug:** Initial stub nodes allocated a fixed 16-byte staging buffer (enough for one RGBA float), but the Vulkan copy command attempted to fill a 100x100 image (160,000 bytes).
*   **The Result:** Vulkan Validation Layers triggered a critical error (`VUID-vkCmdCopyBufferToImage-pRegions-00171`) indicating the source buffer was too small.
*   **The Fix:** Updated the node evaluation logic to calculate staging buffer size dynamically based on the `requestedExtent` provided in the `EvaluationContext`.

#### 2. Handle Namespace Collisions
*   **The Problem:** Despite `using namespace loom::gpu`, the compiler occasionally failed to resolve `ImageHandle` in test code when nested within other namespaces or templates.
*   **The Fix:** Standardized on explicit `loom::gpu::ImageHandle` naming in test suites to ensure portability across different compiler versions (Clang/GCC).

#### 3. SlotMap Iterator Invalidation
*   **The Problem:** `SlotMap::forEach` expects a specific callback signature. Changing the storage to `unique_ptr` broke existing Kahn's algorithm and DFS logic.
*   **The Fix:** Implemented `Graph::forEachNode` wrappers that handle the unique_ptr dereferencing internally, shielding the core algorithms from the underlying storage change.

---

## Post-Phase 4: Refactoring & Safety Hardening
**Objective:** Decouple UI-specific metadata from the headless core and improve error propagation for GPU resource allocation.

### Implementation Details
- **UI/Core Decoupling:**
    - Removed `spawnX`, `spawnY`, and `hasSpawnPos` from the `core::Node` struct.
    - Introduced `UINodeState` in `ui::NodeEditorPanel`.
    - Implemented `std::unordered_map<core::NodeHandle, UINodeState> m_nodeStates` within the UI layer to store layout metadata.
- **Handle Hashing:** Added a `std::hash` specialization for the templated `Handle<Tag>` struct in `core/Handle.hpp`. This allows node, pin, and link handles to be used as keys in standard associative containers without custom hasher structs.
- **VMA Assertions:** Added `assert(res == VK_SUCCESS)` to all VMA buffer creation and memory mapping calls in `src/core/Nodes.cpp`. This ensures that in debug builds, resource allocation failures are caught immediately at the call site rather than failing silently or causing downstream segfaults.

### Engineering Rationale
- **Architectural Purity:** The `core` namespace is now strictly "headless." It contains no logic or data related to screen-space coordinates or UI state, fulfilling the requirement for a clean separation of concerns.
- **Fail-Fast Methodology:** By asserting on `VkResult` return codes from VMA, we identify "Out of Memory" or "Invalid Argument" errors during the recording phase of the pull-eval engine, preventing the submission of corrupted command buffers to the GPU.


---

## Phase 5: Compute Dispatch Manager
**Objective:** Transition from CPU-mapped stubs to GPU compute by separating DAG evaluation from execution and implementing a robust synchronization layer.

### Implementation Details
- **Shader Infrastructure:**
    - Integrated `glslc` into the CMake build process to compile `.comp` shaders to SPIR-V at build time.
    - Created `Fill.comp` for image initialization and `Passthrough.comp` for data transfer.
- **Pipeline Registry (`PipelineCache`):**
    - Centralized `VkPipeline` creation and caching to prevent nodes from managing Vulkan objects directly.
    - Standardized on a single **Global Pipeline Layout** with a 128-byte push constant range, compatible with all compute nodes.
- **The `ComputeTask` Struct:**
    - Introduced a payload-based communication between the evaluator and the recorder.
    - Stores pipeline handles, packed push constant data, group counts, and explicit read/write dependencies.
- **The Dispatch Manager:**
    - **Pass 1 — Batched Layout Transitions:** Automatically detects images not in `VK_IMAGE_LAYOUT_GENERAL` and emits a single `vkCmdPipelineBarrier2` to transition them.
    - **Pass 2 — RAW Hazard Synchronization:** Tracks `bindlessSlot` identities during the recording loop. If a task reads a slot that was written earlier in the same frame, a compute-to-compute memory barrier is injected before the dispatch.
    - **Pass 3 — Viewer Transition:** Transitions the final output image to `SHADER_READ_ONLY_OPTIMAL` for safe ImGui sampling.

### Key Decisions
- **General Layout for Compute:** Standardized on `VK_IMAGE_LAYOUT_GENERAL` for all transient storage images to simplify state tracking while maintaining high-performance read/write access.
- **Vulkan 1.3 Synchronization (Sync2):** Leveraged `VK_KHR_synchronization2` for all barriers to utilize the more expressive and less error-prone `VkImageMemoryBarrier2` API.
- **Decoupled Recording:** Nodes now only generate "Intent" (Tasks); the `DispatchManager` owns the "Action" (Vulkan commands), ensuring that synchronization logic is centralized rather than scattered across node implementations.

### Phase 5 Engineering Post-Mortem: Format Support and Feature Parity
#### 1. The RGBA vs. RGB Storage Failure
*   **The Bug:** Initial implementation used `VK_FORMAT_R32G32B32_SFLOAT` (RGB32F). On many hardware platforms (including Apple Silicon), 3-component formats are not supported for storage image writes.
*   **The Result:** Vulkan Validation Layers reported `VK_ERROR_FORMAT_NOT_SUPPORTED` and the application crashed during texture descriptor validation.
*   **The Fix:** Standardized on `VK_FORMAT_R32G32B32A32_SFLOAT` (RGBA32F) across all nodes and shaders, which has near-universal support for compute storage.

#### 2. SPIR-V Capability Desync
*   **The Bug:** Shaders used `nonuniformEXT` and runtime arrays, which require the `runtimeDescriptorArray` feature to be explicitly enabled in the Vulkan device.
*   **The Result:** Validation error `VUID-VkShaderModuleCreateInfo-pCode-08740` (Capability RuntimeDescriptorArray was declared but not satisfied).
*   **The Fix:** Updated `VulkanContext::createLogicalDevice` to include `VkPhysicalDeviceDescriptorIndexingFeatures` in the `pNext` chain with `runtimeDescriptorArray = VK_TRUE`.

#### 3. Test Environment Segfaults
*   **The Bug:** `PushPullTest` (an older test suite) was not updated to initialize the new `PipelineCache`, resulting in null-pointer dereferences when the refactored nodes attempted to load shaders.
*   **The Fix:** Hardened the test fixtures to provide a valid `PipelineLayout` and `PipelineCache` to the `EvaluationContext`, ensuring legacy tests remain functional as the architecture evolves.

---

## Phase 5.1: Shader Compiler Robustness & Path Resolution
**Objective:** Ensure stable shader compilation across different environments (CI/Local) and allow tests to run from any working directory.

### Implementation Details
- **Multi-Compiler Fallback:** 
    - Updated CMake logic to search for `glslc`, `glslangValidator`, or `glslang`.
    - Automatically injects the `-V` flag when using `glslang` variants to ensure SPIR-V output (Vulkan target).
- **Absolute Shader Pathing (`LOOM_SHADER_DIR`):**
    - Defined a compile-time macro `LOOM_SHADER_DIR` in CMake pointing to the absolute ${CMAKE_BINARY_DIR}/bin/shaders directory.
    - This decouples the application's resource loading from the current working directory (CWD).
- **Robust `PipelineCache` Loading:**
    - Integrated `std::filesystem` into the shader loading pipeline.
    - If a shader path is relative, the `PipelineCache` now prepends the absolute `LOOM_SHADER_DIR`, ensuring shaders are found even when running `ctest` from the build root.

### Key Decisions
- **Compile-Time Macros over Environment Variables:** Chose a macro for the shader directory to avoid requiring users or CI runners to set environment variables, making the "build and run" experience seamless.
- **Base Filenames in Nodes:** Refactored nodes to use base filenames (e.g., `Fill.comp.spv`) instead of hardcoded relative paths (`shaders/...`), centralizing path logic within the GPU infrastructure.

### Phase 5.1 Engineering Post-Mortem: CI Environment Discrepancies
#### 1. The Missing `glslc` in GitHub Actions
*   **The Bug:** The lightweight Vulkan SDK setup in GitHub Actions does not always include the `glslc` (Google) compiler by default, causing CMake configuration failures.
*   **The Fix:** Expanded `find_program` to support `glslangValidator` (Khronos), which is more commonly available in minimal SDK installations.

#### 2. `ctest` Working Directory Failures
*   **The Bug:** Tests were written assuming the CWD was the `bin/` directory. When run via `ctest` from the `build/` root, the relative path `shaders/Fill.comp.spv` could not be resolved.
*   **The Fix:** Moving to absolute path resolution via `LOOM_SHADER_DIR` and `std::filesystem::path` allows tests to remain portable and independent of the execution entry point.

---

## Phase 5.2: CI Resilience & Dependency Management
**Objective:** Address SPIR-V toolchain dependencies in CI and ensure the project remains buildable even when shader compilers are missing.

### Implementation Details
- **Satisfying Glslang Dependencies:** 
    - Added `SPIRV-Tools` to the `vulkan-components` in the GitHub Actions workflow. This provides the necessary optimizer binaries (`spirv-opt`) required when `Glslang` is built from source on certain platforms (e.g., macOS arm64).
- **Optional Shader Compilation:**
    - Refactored `CMakeLists.txt` to make the shader compiler (`glslc`, `glslang`) optional.
    - Introduced the `LOOM_HAS_SHADER_COMPILER` compile-time definition to track compiler availability.
- **Graceful Test Skipping:**
    - Updated `ComputeDispatchTest.cpp` to use `GTEST_SKIP()` if `LOOM_HAS_SHADER_COMPILER` is not defined. This ensures that the test suite reports "skipped" rather than "failed" in environments lacking a shader compiler.

### Engineering Rationale
- **Dependency Isolation:** By explicitly providing `SPIRV-Tools` in the workflow, we resolve the "ENABLE_OPT" build error without having to compromise on compiler features.
- **Headless Compatibility:** Making the shader compiler optional ensures that the "Loom Core" (the C++ headless model) can still be developed and verified on machines that do not have a full Vulkan SDK installed.
- **Signal vs. Noise:** Skipping shader-dependent tests instead of failing them provides a clear signal that the environment is restricted, rather than the code being broken.



## Phase 5.5: ImGui Dockspace & Viewport Layout
**Objective:** Implement a professional, persistent workspace layout using ImGui Docking to separate the node graph from the render output.

### Implementation Details
- **Fullscreen Dockspace:** 
    - Enabled  in the ImGui context.
    - Utilized  to establish a root dock node that covers the entire application window.
- **Conditional DockBuilder API:**
    - **Persistence Guard:** Implemented a check using . This ensures that the programmatic layout is only generated on the first launch (or if `imgui.ini` is deleted), allowing user customizations to persist across sessions.
    - **Layout Topology:** Programmatically split the dockspace into two regions using  with a 0.3f (30%) ratio for the bottom section.
- **Viewport Size Tracking:**
    - Used  inside the "Viewport" panel to track its actual pixel dimensions in real-time.
    - **Vulkan Zero-Size Guard:** Implemented a  check. This prevents the downstream Vulkan pipeline from attempting to create 0x0 framebuffers when the panel is collapsed or minimized, which would trigger undefined driver behavior.
- **Dynamic Extent Integration:** Updated  to feed the tracked  directly into the , ensuring the compute graph always renders at the exact resolution of the UI panel.

### Key Decisions
- **Stable Window Naming:** Standardized on hardcoded strings (`"Viewport"`, `"Node Editor"`) for panel titles. Renaming these would break the link to the saved layout in `imgui.ini`.
- **Internal API Usage:** Included `imgui_internal.h` to access the  symbols, which are required for programmatic layout setup but are not part of the standard ImGui public API.
- **Immediate-Mode Resizing:** Chose to update the viewport extent on every frame rather than via a callback. This provides instantaneous visual feedback during panel resizing without the complexity of an event-driven system.

### Engineering Post-Mortem: Docking and Layout Initialization

#### 1. The "Vanishing Windows" Bug (Initialization Order)
*   **The Bug:** Initially,  was called *after* the layout initialization logic. 
*   **The Result:** Because  implicitly creates the dock node if it doesn't exist, the  check was failing to trigger on the first frame, leaving the "Viewport" and "Node Editor" windows floating and undocked.
*   **The Fix:** Refactored  to retrieve the ID via  first, then perform the layout build, and finally call  to host the dockspace.

#### 2. The Persistence Conflict
*   **The Problem:** Using a simple  to trigger layout setup would overwrite the user's `imgui.ini` every time the application restarted.
*   **The Decision:** Shifted to the  check. This allows ImGui to remain the "source of truth" for the layout after the initial bootstrap, respecting the user's workspace preferences.

#### 3. Redundant Window Definitions
*   **The Problem:** Both  and  were attempting to define the "Node Editor" window, leading to duplicated window logic.
*   **The Fix:** Centralized the window definition.  now establishes the dock node, and  populates it by using the matching window name.

---

## Phase 5.6: Frame Lifecycle Split & Resize Robustness
**Objective:** Resolve the ImGui assertion crash during window resizing and establish a professional, industry-standard render loop.

### The Crash: Anatomy of an Unbalanced Frame
- **The Symptom:** `Assertion failed: ... "Forgot to call Render() or EndFrame() at the end of the previous frame?"`.
- **The Root Cause:** The monolithic `VulkanContext::drawFrame` performed swapchain acquisition and resize checks *after* the UI logic had already called `imgui.beginFrame()`. When a resize was detected, `drawFrame` returned early to recreate the swapchain, skipping `imgui.endFrame()` (which calls `ImGui::Render()`). This left the ImGui state machine in an "active frame" state, causing a crash when the next iteration attempted to start a new frame.

### Implementation: The Split-Lifecycle Pattern
- **Refactored `VulkanContext`:** Split `drawFrame` into `beginFrame()` and `endFrame(cmd, imgui)`.
    - **`beginFrame()`:** Handles fence synchronization, minimization guards (waiting for events if size is 0), and swapchain acquisition. It returns the active `VkCommandBuffer` if successful, or `VK_NULL_HANDLE` if the frame should be skipped.
    - **`endFrame()`:** Handles image layout transitions, dynamic rendering, ImGui recording, command submission, and presentation.
- **Main Loop Restructuring:** The main loop in `main.cpp` now uses a conditional block:
    ```cpp
    if (VkCommandBuffer cmd = vulkan.beginFrame()) {
        imgui.beginFrame();
        imgui.drawDockspace();
        nodeEditor.draw("Node Editor");

        // Evaluate Graph logic here...

        vulkan.endFrame(cmd, imgui);
    }
    ```
    This ensures that ImGui is only invoked if a valid GPU frame is guaranteed, keeping the CPU/UI and GPU/Render lifecycles perfectly synchronized.

### Key Decisions
- **Industry-Standard vs. Band-aid:** Rejected the simple fix of calling `ImGui::EndFrame()` inside the old `drawFrame`. While functional, it would still waste CPU cycles building UI data for a frame that would never be shown. The split-lifecycle approach is the gold standard for high-performance Vulkan engines.
- **Minimization Guard:** Explicitly handled the "zero-extent" case (minimizing the window). The engine now calls `glfwWaitEvents()` and skips rendering until the window is restored, preventing swapchain recreation loops.
- **Compute Readiness:** By exposing the `VkCommandBuffer` to the main loop, we've laid the architectural foundation for Phase 6, where compute dispatches will be recorded into the same buffer as the UI draw calls.

### Phase 5.6 Engineering Post-Mortem: Synchronization and State
#### 1. The "Suboptimal" Reentry
*   **The Problem:** `vkAcquireNextImageKHR` often returns `VK_SUBOPTIMAL_KHR` on macOS during resizing. Treating this as a "success" allowed the frame to proceed, but if the surface was already incompatible, the subsequent `vkQueuePresentKHR` would fail.
*   **The Fix:** Updated the present logic to catch both `VK_ERROR_OUT_OF_DATE_KHR` and `VK_SUBOPTIMAL_KHR`, triggering a swapchain recreation for the *next* frame to ensure continuous stability.

#### 2. Fence Reset Timing
*   **The Bug:** Resetting the in-flight fence *before* acquiring the next image.
*   **The Risk:** If `vkAcquireNextImageKHR` fails or returns early, the fence remains unsignaled, but the CPU has already "forgotten" it waited, potentially leading to a deadlock on the next frame.
*   **The Fix:** Strictly moved `vkResetFences` to occur only *after* a successful image acquisition and before command buffer recording begins.

---

## Phase 6.5: ImGui Viewport Integration & Evaluation Robustness

### Objectives
Bridge the Vulkan Display Pass to the ImGui UI layout and ensure the node graph evaluation is stable, leak-free, and user-friendly during interactive re-wiring.

### 1. Viewport Bridging
*   **Vulkan-to-ImGui Pipeline:** Added a persistent `VkSampler` and `VkDescriptorSet` to `ImGuiRenderer`.
*   **Dynamic Resizing:** Implemented `recreateViewportTarget` which reallocates an offscreen `VkImage` whenever the ImGui "Viewport" panel size changes. This image is registered with ImGui via `ImGui_ImplVulkan_AddTexture`.
*   **Synchronization:** Integrated a final Image Memory Barrier in `DisplayPass` to transition the render target to `SHADER_READ_ONLY_OPTIMAL` before ImGui attempts to sample it.

### 2. Bug Fixes & System Stability

#### Critical Image Pool Leak
*   **Issue:** `ImageHandle`s acquired during graph evaluation were never released. This caused Bindless Heap exhaustion (2048 slots) within seconds, leading to a silent viewport "lockup."
*   **Fix:** Implemented per-frame garbage collection in `main.cpp`. All handles in the `EvaluationContext` cache and pending release list are now explicitly returned to the `TransientImagePool` at the end of the frame.

#### Stale Evaluation Caching
*   **Issue:** `Node::pullInput` incorrectly returned cached results from *previous* frames if a node was not explicitly marked dirty, even though the `EvaluationContext` was fresh.
*   **Fix:** Updated caching logic to strictly enforce that a node must have been evaluated in the *current* frame (existence in current cache) to skip re-evaluation. This ensures the graph always reflects the latest state after every frame loop.

#### Missing Input Handling
*   **Issue:** `PassthroughNode` and `MergeNode` would return early or skip rendering if inputs were missing, leaving the viewport with stale data from the last successful frame.
*   **Fix:** Updated evaluation logic to always generate a `ComputeTask`. If inputs are missing, nodes now explicitly fill their output with black (Passthrough) or dark gray (Merge) placeholders.

### 3. UX & Logic Refinement

#### Smart Merge Logic
*   **Improvement:** `MergeNode` now acts as a passthrough if only one input is connected. It only displays the "missing input" gray if completely disconnected, and the "merged" purple if fully connected. This prevents the graph from feeling "broken" during intermediate wiring steps.

#### Bidirectional Wiring
*   **Issue:** The Graph engine required a strict `(Output, Input)` order for link creation, causing links dragged "backwards" in the UI to be silently rejected.
*   **Fix:** Updated `NodeEditorPanel` to detect drag direction and automatically swap handles to maintain the engine's required invariant, allowing users to wire pins in either direction.

#### Platform Compatibility
*   **Fix:** Updated all node output formats from `VK_FORMAT_R32G32B32_SFLOAT` to `VK_FORMAT_R32G32B32A32_SFLOAT` to resolve `VK_ERROR_FORMAT_NOT_SUPPORTED` crashes on macOS/MoltenVK and other hardware with strict 4-component alignment requirements for storage images.
