# Loom — Architecture

This document is a one-page overview of the Loom system. For per-subsystem conventions, see [CONVENTIONS.md](CONVENTIONS.md). For the per-phase logs, see [archive/refactor-cleanup-2026.md](archive/refactor-cleanup-2026.md) (cleanup branch) and [archive/feature-open-exr-2026.md](archive/feature-open-exr-2026.md) (deep-EXR / NVS feature branch, Phase A onwards).

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
       │   DispatchManager → HazardTracker  (image + buffer keys)            │
       │   DisplayPass                                                       │
       │   StagingArena  (host-visible bump arena; uploadDeepImage)          │
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
       │   Per-node Params (Param + buildParams)                             │
       │                                                                     │
       │   Camera             (RH/Y-up perspective, lazy view/proj)          │
       │   DeepLayout         (interned channel schema for deep EXR)         │
       │   RenderCache        (pin × region → ResourceRef, frame-retired)    │
       │   ColorManagement    (linear scene-referred → display transform)    │
       │                                                                     │
       │   Headless. No Vulkan headers included.                             │
       │                                                                     │
       └────────────────────────────────────────────────────────────────────┘

       ┌────────────────────────────────────────────────────────────────────┐
       │                            loom::io                                 │
       │   ─────────────────────────────────────────────────────────────     │
       │   ParsedDeepImage   (CPU SoA payload; Phase B reader output shape)  │
       │   (Phase B: IDeepReader / IDeepWriter via OpenEXR)                  │
       └────────────────────────────────────────────────────────────────────┘
```

---

## Layers

### `loom::core` — headless engine
Pure C++. No Vulkan, no GLFW, no ImGui. The compositor's data model and evaluation order. Reusable in a headless batch-render context, a unit test, or a future CPU evaluator.

**Key types:** `Graph`, `Node`, `Pin`, `Link`, `Handle<Tag>`, `SlotMap`, `RenderCache`, `Region`, `Tile`, `ColorManagement`, `EvaluationContext`, `Camera`, `DeepLayout`, `Param`.

### `loom::gpu` — Vulkan layer
Owns every `Vk*` handle in the application. Translates `EvaluationContext::tasks` (produced by `core/`) into recorded command buffers.

**Key types:** `Instance`, `Device`, `Swapchain`, `ResourceFactory`, `FrameLoop`, `BindlessHeap`, `TransientImagePool`, `TransientBufferPool`, `PipelineCache`, `DispatchManager`, `HazardTracker`, `DisplayPass`, `ResourceRef`, `StagingArena`, `uploadDeepImage`.

### `loom::io` — file I/O
CPU-side parsed representations of input files plus (Phase B onwards) the OpenEXR Deep reader / writer. Sits alongside `core/` rather than under it because file paths and I/O futures are inherently non-headless concerns.

**Key types:** `ParsedDeepImage`. (Phase B: `IDeepReader`, `IDeepWriter`.)

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
gpu   →  core, io, platform         (Vulkan, VMA, GLFW for surface)
io    →  core                       (OpenEXR; Phase B onwards)
core  →  (nothing — headless)       (glm header-only is the only third-party dep)
platform → (GLFW only)
```

`core/` cannot include `gpu/`, `io/`, `ui/`, or `platform/`. This is the invariant that keeps the headless engine portable.

---

## Out-of-scope as of this document

See [CONVENTIONS.md §18](CONVENTIONS.md#18-out-of-scope-this-cleanup-branch) for the cleanup-branch deferral list. Phase A (this branch) lands the foundational layer for deep-EXR / NVS — `DeepLayout`, `Camera`, `Param`, `StagingArena`, `uploadDeepImage`, hazard tracking for buffers — but does **not** yet wire any user-facing deep node, file reader, or animation playback. Those land in Phases B (deep viewer), C (animation + compositing), and D (novel-view synthesis). See [archive/feature-open-exr-2026.md](archive/feature-open-exr-2026.md) for the per-sub-phase log.
