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
