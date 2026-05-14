# Loom — Architecture

This document is a one-page overview of the Loom system. For per-subsystem conventions, see [CONVENTIONS.md](CONVENTIONS.md). For the per-phase log of the active refactor, see [archive/refactor-cleanup-2026.md](archive/refactor-cleanup-2026.md).

---

## System diagram

```
                     ┌─────────────────────────────────────────────────┐
                     │                   Application                   │
                     │                  (src/main.cpp)                 │
                     └──┬──────────────────────────────────────┬───────┘
                        │                                      │
                        ▼                                      ▼
       ┌────────────────────────────────┐   ┌────────────────────────────────┐
       │   loom::platform               │   │   loom::ui                     │
       │   ──────────────               │   │   ────────                     │
       │   Window  (GLFW)               │   │   ImGuiRenderer                │
       │                                │   │   NodeEditorPanel              │
       └───────────────┬────────────────┘   └────────────────┬───────────────┘
                       │                                     │
                       ▼                                     │
       ┌────────────────────────────────────────────────────────────────────┐
       │                            loom::gpu                                │
       │   ─────────────────────────────────────────────────────────────     │
       │                                                                     │
       │   VulkanContext  (façade — composition root, post-Phase-5)          │
       │     ├── Instance         (VkInstance, debug messenger, surface)     │
       │     ├── Device           (physical + logical device, queues)        │
       │     ├── Swapchain        (VkSwapchainKHR, images, recreation)       │
       │     ├── ResourceFactory  (command pool, descriptor pool, VMA,       │
       │     │                     BindlessHeap)                             │
       │     └── FrameLoop        (timeline semaphore, beginFrame/endFrame)  │
       │                                                                     │
       │   BindlessHeap                                                      │
       │   TransientImagePool   TransientBufferPool                          │
       │   PipelineCache  (disk-backed VkPipelineCache)                      │
       │   DispatchManager → HazardTracker                                   │
       │   DisplayPass                                                       │
       │                                                                     │
       └────────────────────────────────┬───────────────────────────────────┘
                                        │  EvaluationContext
                                        ▼
       ┌────────────────────────────────────────────────────────────────────┐
       │                            loom::core                               │
       │   ─────────────────────────────────────────────────────────────     │
       │                                                                     │
       │   Graph (DAG: Nodes, Pins, Links)                                   │
       │     ├── SlotMap<T, Handle<Tag>>     (generational arena)            │
       │     ├── 2-pass evaluator             (mark → topo-sort → execute)   │
       │     └── Topological sort            (Kahn's algorithm)              │
       │                                                                     │
       │   Node hierarchy:  ConstantNode, MergeNode, ViewerNode,             │
       │                    PassthroughNode                                  │
       │                                                                     │
       │   RenderCache  (pin × region → ResourceRef, frame-retired)          │
       │   ColorManagement  (linear scene-referred → display transform)      │
       │                                                                     │
       │   Headless. No Vulkan headers included.                             │
       │                                                                     │
       └────────────────────────────────────────────────────────────────────┘
```

---

## Layers

### `loom::core` — headless engine
Pure C++. No Vulkan, no GLFW, no ImGui. The compositor's data model and evaluation order. Reusable in a headless batch-render context, a unit test, or a future CPU evaluator.

**Key types:** `Graph`, `Node`, `Pin`, `Link`, `Handle<Tag>`, `SlotMap`, `RenderCache`, `Region`, `Tile`, `ColorManagement`, `EvaluationContext`.

### `loom::gpu` — Vulkan layer
Owns every `Vk*` handle in the application. Translates `EvaluationContext::tasks` (produced by `core/`) into recorded command buffers.

**Key types (post-Phase-5):** `Instance`, `Device`, `Swapchain`, `ResourceFactory`, `FrameLoop`, `BindlessHeap`, `TransientImagePool`, `TransientBufferPool`, `PipelineCache`, `DispatchManager`, `HazardTracker`, `DisplayPass`, `ResourceRef`.

### `loom::platform` — OS / windowing
Currently a thin GLFW wrapper. The seam for future native-window backends.

**Key types:** `Window`.

### `loom::ui` — Editor UI
Dear ImGui + imgui-node-editor. Renders the node graph and the viewport into an ImGui dockspace. Mutates `core::Graph` directly in response to user actions.

**Key types:** `ImGuiRenderer`, `NodeEditorPanel`.

---

## Frame lifecycle

One frame, top to bottom:

1. **`Window::pollEvents`** — GLFW input + resize detection.
2. **`FrameLoop::beginFrame`** — wait on timeline semaphore for the slot's previous use to retire; acquire next swapchain image; reset command buffer.
3. **`ImGuiRenderer::beginFrame`** + dockspace + **`NodeEditorPanel::draw`** — UI mutations applied directly to the `core::Graph` (add/remove node, add/remove link). The graph's `isTopoDirty` flag tracks structural changes.
4. **`Graph::execute(ctx, region)`** — 2-pass evaluator: mark required nodes from each `ViewerNode` upstream, topologically sort the active set, then `node->execute(ctx, region)` for each node. Cached output pins skip re-execution.
5. **`DispatchManager::submit`** — record layout transitions, hazard barriers, and `vkCmdDispatch` for each `ComputeTask`. `HazardTracker` decides which barriers are needed.
6. **`DisplayPass::record`** — sample the viewer output into the ImGui viewport image, applying the configured `DisplayTransform`.
7. **`VulkanContext::endFrame` / `FrameLoop::endFrame`** — record ImGui draw calls, transition swapchain image to `PRESENT_SRC_KHR`, submit (signals timeline semaphore at `currentFrameValue + 1`), present.
8. **End-of-frame cleanup** — `RenderCache::garbageCollect`, pending-release drain (gated by frame retirement, post-Phase-6).

---

## Compile-time dependencies

```
ui    →  core, gpu, platform        (Dear ImGui + imgui-node-editor)
gpu   →  core, platform             (Vulkan, VMA, GLFW for surface)
core  →  (nothing — headless)
platform → (GLFW only)
```

`core/` cannot include `gpu/`, `ui/`, or `platform/`. This is the invariant that keeps the headless engine portable.

---

## Out-of-scope as of this document

See [CONVENTIONS.md §18](CONVENTIONS.md#18-out-of-scope-this-cleanup-branch) for the full list. Highlights: no deep compositing, no OCIO, no parameter system, no animation, no project save/load, no per-tile streaming dispatch, no async queues.
