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

## Project Structure & Namespaces
**Standard Adopted:** Nested namespaces (`loom::core`, `loom::gpu`, `loom::platform`, `loom::ui`).

### Rationale
- **Architectural Walls:** Prevents "spaghetti code" by making cross-layer dependencies explicit.
- **Namespace Aliases:** In `.cpp` files, we use `namespace core = loom::core;` to balance brevity with clarity.
- **Directory Mirroring:** The logical namespace structure directly mirrors the physical directory structure for developer predictability.
