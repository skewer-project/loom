# Loom — Engineering Conventions & Invariants

This document is the **plan of record** for engineering decisions on Loom. Every pull request is expected to conform to the invariants here, and reviewers will cite section headings rather than re-litigate the underlying choice.

For *how* to set up a development environment, see [CONTRIBUTING.md](CONTRIBUTING.md). For *what* the system looks like, see [docs/architecture.md](docs/architecture.md). This file covers *the rules*.

---

## 1. Project layout

```
loom/
├── include/                # Public headers
│   ├── core/               # Headless types: Graph, SlotMap, Handle, RenderCache, ColorManagement, ...
│   ├── gpu/                # Vulkan-dependent: VulkanContext, pools, dispatch, hazard tracking, ...
│   ├── platform/           # OS/windowing: Window (GLFW)
│   └── ui/                 # ImGui + imgui-node-editor
├── src/                    # Implementations, mirroring include/
├── shaders/                # GLSL → SPIR-V compute & display shaders
├── tests/                  # GoogleTest; mirrors src/
│   ├── core/               # Headless tests
│   └── gpu/                # Vulkan-required tests (GTEST_SKIP when no device)
├── external/               # Vendored deps (ImGui, imgui-node-editor, VMA)
├── docs/                   # architecture.md, Doxyfile.in
└── .github/workflows/      # CI
```

Headless `core/` **may not** include any Vulkan header. GPU-dependent code lives under `gpu/`.

---

## 2. Resource ownership

- Every `acquire()` on `TransientImagePool` / `TransientBufferPool` is paired with one of:
  - `release(handle, releaseAtFrame)` on the pool, or
  - `cache.store(pin, region, handle)` on `RenderCache`, which transfers ownership to the cache.
- The cache owns until the entry is evicted (`evict`, `clear`, or `garbageCollect`). Eviction enqueues the handle into `m_pendingImageReleases`; the engine drains the queue once per frame and calls `pool.release(handle, frameValue + MAX_FRAMES_IN_FLIGHT)` for each.
- A released `ImageHandle` remains GPU-valid until the timeline counter reaches `releaseAtFrame`. The standard tag is `frameLoop.currentFrameValue() + MAX_FRAMES_IN_FLIGHT`, which guarantees the slot is not reissued while any in-flight frame still references its descriptor. Code that needs the slot back sooner must `vkDeviceWaitIdle` first.
- Once per frame the engine queries `frameLoop.getRetiredFrameValue()` and forwards it to `imagePool.onFrameRetired(retired)` and `bindlessHeap.onFrameRetired(retired)`. Anything tagged `<= retired` is moved back to its free list / pool entry. `flushPendingReleases()` exists as a synchronous drain for shutdown and unit-test paths — production code must not call it inside the render loop.
- Raw `new` / `delete` is disallowed. Use `std::unique_ptr` / `std::shared_ptr` / VMA / RAII.

---

## 3. Layout invariants

- Transient images live in `VK_IMAGE_LAYOUT_GENERAL` between dispatches.
- Only three subsystems issue layout transitions: `DispatchManager`, `DisplayPass`, and `Swapchain`. Nodes **never** touch image layouts directly.
- The single canonical transition helper lives in `gpu/LayoutTransitions.hpp`. It derives `srcStage` / `srcAccess` / `dstStage` / `dstAccess` from the target layout. Unknown layout pairs `LOOM_ASSERT` — adding a new layout requires extending the helper, not bypassing it.
- All barriers use `vkCmdPipelineBarrier2` with `VkImageMemoryBarrier2` / `VkMemoryBarrier2`. The legacy single-stage barrier API is not used.

---

## 4. Hazard model

`HazardTracker` (in `gpu/HazardTracker.hpp`) is the single source of truth for inter-dispatch hazards.

| Hazard | Tracked | Notes |
|--------|---------|-------|
| RAW (Read after Write) | yes | Emits a `SHADER_WRITE → SHADER_READ` memory barrier. |
| WAW (Write after Write) | yes | Emits a `SHADER_WRITE → SHADER_WRITE` memory barrier. |
| WAR (Write after Read) | **scaffolded** | Set tracked, predicate always returns `false`. Flip the predicate when the first node reads-then-writes the same slot within a frame. |

A barrier clears the tracker's sets — the barrier subsumes all prior accesses. Sets are keyed on `(poolIndex, generation)`, not `bindlessSlot`, so the deferred-release lifetime contract is explicit by construction.

---

## 5. Region semantics

- A node's required region defaults to **equal** to the region requested of it. Pure pointwise / passthrough nodes inherit this without override.
- Nodes with non-identity spatial mappings (future `Blur`, `Transform`, `Reformat`) must override `markRequiredTiles` to widen or narrow the upstream requirement.
- Industry vocabulary alignment: Loom's "required region" maps to Nuke's **RoI** (region of interest); the node's natural output extent maps to **RoD** (region of definition). Use these terms in node-author documentation.
- `Region` is canonicalised (tiles sorted by `(y, x)`) before use as a cache key — two regions with the same tile set in different order hash and compare equal. `RenderCache` canonicalises internally; callers do not have to.
- `Node::pullInput(ctx, region, inputIndex)` threads the requested region to `RenderCache::retrieve`. A miss at a different region produces a fresh evaluation rather than a stale hit — there is no separate "extent-changed" invalidation pass.

---

## 6. Frame lifecycle & synchronisation

- The CPU may be `MAX_FRAMES_IN_FLIGHT` (=2) frames ahead of the GPU.
- Synchronisation uses **timeline semaphores** (Vulkan 1.3). One timeline semaphore monotonically advances per submit; "frame N has retired" means `vkGetSemaphoreCounterValue(timeline) >= N`. Binary fences are not used for frame tracking.
- Swapchain image acquisition still uses a binary semaphore (the only thing `vkAcquireNextImageKHR` accepts).
- `FrameLoop::onFrameRetired(frameValue)` is the single hook for resource retirement. `BindlessHeap`, `TransientImagePool`, and `RenderCache` register with it.

---

## 7. Color management

- **All internal compositing math is linear scene-referred float.** Nodes consume linear, produce linear.
- The display transform is applied **exclusively** in `DisplayPass`, parameterised by `loom::color::DisplayParams`.
- `DisplayTransform::None` is correct when the swapchain is `_SRGB` (hardware applies sRGB on write). `DisplayTransform::sRGB` is correct when the swapchain is `_UNORM`. The choice is made once at `Swapchain` construction and passed forward.
- `ColorManagement` is the abstraction surface; v1 is a hand-rolled implementation. Future PRs may swap the backend to [OpenColorIO](https://opencolorio.org/) without touching call sites.

---

## 8. Push-constant budget

- 128 bytes per pipeline. Enforced at compile time by `ComputeTask::setPushConstants<T>()`'s `static_assert`.
- `std::memcpy(task.pushConstants.data(), &pc, sizeof(pc))` directly is **disallowed** — use the typed helper.
- New larger payloads use a uniform buffer or storage buffer, not push constants.

---

## 9. Pin schema & resource types

- Pin schemas are declared **per node class** via the virtual `Node::getPinSchema() -> std::vector<PinSpec>`. There is no centralised switch.
- Pin payloads are `loom::gpu::ResourceRef`, a tagged union over `Kind::Image` / `Kind::Buffer` / `Kind::Deep` / `None`.
- Image-only nodes use the `pullImageInput` typed accessor, which asserts `Kind::Image` and returns the inner `ImageHandle`. Adding a non-image node type means declaring the new `Kind` in `getPinSchema` and handling it in `execute`.
- `canAddLink` enforces type compatibility at edit time. Wiring a `Buffer` pin to an `Image` pin is rejected before evaluation runs.

---

## 10. Logging

- All output goes through `loom::log::*`:
  - `loom::log::trace(...)` — verbose; off by default.
  - `loom::log::debug(...)` — diagnostic; debug builds.
  - `loom::log::info(...)` — informational.
  - `loom::log::warn(...)` — recoverable issue.
  - `loom::log::error(...)` — failure.
- No raw `std::cout` / `std::cerr` in shippable code paths. Tests may use the GoogleTest streams.
- Log calls are thread-safe (internal `std::mutex`). v1 forwards to `iostream` with severity + timestamp prefix; the implementation is intended to be a swappable façade.

---

## 11. Assertions & error handling

- `LOOM_ASSERT(cond, msg)` (in `core/Assert.hpp`) — survives `NDEBUG`. Use for safety-critical invariants (a violated `LOOM_ASSERT` means the code's preconditions have been broken and the process must terminate to prevent corruption).
- `LOOM_VK_CHECK(expr)` — evaluates a `VkResult`-returning expression; throws a typed exception on non-`VK_SUCCESS`. Used everywhere a Vulkan call could fail (`vkCreate*`, `vkQueueSubmit`, `vkAllocateCommandBuffers`, `vkGetSwapchainImagesKHR`, etc.). No silent ignore paths.
- Bare `assert()` is disallowed — it vanishes under `NDEBUG`.
- Recoverable errors (file not found, shader compile failure, user-input invalid) **throw typed exceptions**, not `std::runtime_error` with stringly-typed messages. The exception hierarchy lives in `core/Errors.hpp` (to be introduced as needed).

---

## 12. Profiling

- `LOOM_PROFILE_SCOPE("name")` — RAII profile sample over the current scope.
- `LOOM_PROFILE_FRAME()` — frame mark, called once per frame at end-of-frame.
- v1 expands to a no-op when `LOOM_ENABLE_PROFILING` is undefined. v2 will swap to [Tracy](https://github.com/wolfpld/tracy) by including `<tracy/Tracy.hpp>` in the `Profile.hpp` impl; call sites do not change.
- Instrument anything plausibly above microsecond cost: `Graph::execute`, `DispatchManager::submit`, `DisplayPass::record`, `FrameLoop::beginFrame`/`endFrame`, shader compilation, pipeline creation.

---

## 13. Thread safety

The engine is **single-threaded as of this branch**. The following subsystems are documented non-thread-safe; concurrent use is undefined behaviour:

- `Graph`, `RenderCache`, `PipelineCache`, `BindlessHeap`, `TransientImagePool`, `TransientBufferPool`, `DispatchManager`, `HazardTracker`, `FrameLoop`.

`loom::log::*` is thread-safe.

Future multithreading (resource upload thread, parallel evaluation) lands behind explicit mutex points, not implicit guarantees. Do not introduce locking speculatively.

---

## 14. Testing

- Every new shader ships with a GPU readback test.
- Every new node ships with a happy-path test and at least one edge case.
- GPU tests `GTEST_SKIP` cleanly when no Vulkan device is available; never `FAIL` on absence of a device.
- Visual changes update `tests/data/golden/*.bin`. Regenerate with `LOOM_REGEN_GOLDENS=1 ctest`. Golden updates are committed in the same PR as the visual change with a one-line justification.
- Vulkan validation layers are enabled in Debug. Tests that exercise dispatch / sync install a validation-message callback and `EXPECT_EQ(warningCount, 0)`. A new test for any dispatch path that does not do this is treated as incomplete.

---

## 15. CI gates

A PR is mergeable when, on the current `main` rebase:

1. Debug, Release, and Sanitize builds pass on Ubuntu (gcc + clang), Windows (MSVC), macOS (clang).
2. `ctest` passes on every matrix entry. GPU tests run against Lavapipe on Ubuntu (software Vulkan); on Windows/macOS the GPU tests skip.
3. `clang-tidy` and `scan-build` report zero new warnings on changed files.
4. Coverage does not regress (no hard percentage gate; trend monitored on Codecov).
5. The `LinearReadbackMatchesInput`, `SRGBTransformApplied`, and golden-image regression tests pass.

The full gate matrix is wired in Phase 9 of the cleanup. Pre-Phase-9 PRs are reviewed against the same standards manually.

---

## 16. Code style

- C++20. `clang-format` is authoritative (`.clang-format` at repo root).
- `[[nodiscard]]` on return values whose discard would be a bug — handles, result codes, `tryAddLink`, `beginFrame`, etc.
- `std::span<const T>` for read-only ranges at API boundaries (callers pass `vector::data()` + `vector::size()` implicitly).
- Prefer composition over inheritance; node hierarchy is the only legitimate use of virtual today.
- No header-only inline implementations for non-trivial methods — moves to `.cpp` keep compile time bounded.

---

## 17. Documentation policy

- This file (`docs/CONVENTIONS.md`) is the conventions / plan-of-record document. Cite it in PRs.
- `docs/architecture.md` is the one-page system overview.
- `docs/README.md` is the index of everything under `docs/`.
- `CHANGELOG.md` (repo root) is the user-visible-change ledger.
- `CONTRIBUTING.md` (repo root) covers dev setup, branch naming, commit style, and the PR checklist.
- `docs/archive/` holds historical per-phase dev logs. They are read-only — consulted for context, never edited. Current entries: `refactor-cleanup-2026.md` (the cleanup-branch log) and `build-out-phases-1-6.md` (the original implementation log).
- Public API additions are documented in Doxygen comments on the header declaration. The Doxygen build is wired in Phase 9 (`cmake --build build --target docs` once Doxygen is installed); public-API additions before then still write the comments, they are simply not yet rendered.

---

## 18. Out-of-scope (this cleanup branch)

The following are explicitly **not** addressed by the `refactor/claudes-review` cleanup. They are deferred to future feature branches and are documented here so that PRs that drift into them can be redirected.

- New node types (`Blur`, `ColorCorrect`, `FileRead`, `Transform`, ...).
- OpenEXR / deep compositing implementation. The `Kind::Deep` slot in `ResourceRef` exists but is never populated by production code in this branch.
- Full OCIO integration. The `ColorManagement` interface is the seam; OCIO becomes a drop-in.
- Multi-viewer UI, undo/redo, parameter ("knob") system, animation, project save/load.
- Per-tile streaming evaluator. `Region` participates in cache keys; node `execute` still allocates a full-extent image.
- Multi-queue (async transfer / async compute) separation.
- Tracy / NSight integration. `LOOM_PROFILE_SCOPE` is a no-op in v1; the swap is a future PR.
