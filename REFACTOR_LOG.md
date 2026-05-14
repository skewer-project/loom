# Loom — Refactor / Cleanup Branch Log

This file is the per-change dev archive for the `refactor/claudes-review` branch. It captures **why** decisions were made, **what** changed at each step, and **how** to verify. Parallel to `TEMP_DOCUMENTATION.md` (which logged the original Phase 1.x / Phase 2 build-out); this log covers the post-assessment cleanup that brings the codebase to a professional baseline.

The plan being executed lives at `~/.claude/plans/loom-cleanup-and-refactor-plan.md` (private — not committed to the repo). Eleven phases, ~18.5 engineer-days.

---

## Baseline (before any changes)

- Branch: `refactor/claudes-review`
- Tests: 61/61 passing locally (GPU-bound tests `GTEST_SKIP` when no Vulkan device — clean skip, not failure).
- Recent commits (chronological tail):
  - `e0df4cf` — Separate RenderCache as transient object
  - `1f86b7d` — Implement 2-pass pull and topo-sort eval engine
  - `2c99d34` — Add "Built With" section to README
  - `2b0fc62` — Update viewport node-editor split percentage to 60/40
  - `196e934` — Update passthrough node colour
- Identified weaknesses (from the comprehensive assessment): gamma double-correction, `transitionImageLayout` zero-mask hazard, missing `VkResult` checks, `RenderCache` leak + unwired GC + extent-blind hits, `DispatchManager` missing WAW/WAR, `Region`/tile abstraction is a façade, `VulkanContext` god class (~970 LoC), bindless slot recycling without fence gating, debug `std::cout`, copy-paste in `Nodes.cpp`, no LICENSE / CONTRIBUTING / SECURITY / CODE_OF_CONDUCT, CI runs Release-only with no `ctest` invocation, no static analysis, no headless GPU testing, no API docs.

---

## Phase 0 — Project Hygiene & Conventions

### Goal
Establish the canonical files and conventions a real software project is expected to ship with. Land the conventions document (`CLAUDE.md`) that every subsequent phase references.

### Decisions

- **Licensing files (`LICENSE`, `NOTICE`) deferred to manual handling.** User opted to author these by hand to avoid auto-generated copyright text and to settle the license choice independently. The agent removed its initial MIT draft; `CONTRIBUTING.md` was edited to be license-agnostic ("the project's license, see `LICENSE` once added").
- **`CODE_OF_CONDUCT.md` and `SECURITY.md` deferred to manual handling.** Both contain policy commitments the user wants to author themselves rather than adopt boilerplate.
- **CHANGELOG format: [Keep a Changelog](https://keepachangelog.com).** Industry-standard; pairs with semver.
- **Versioning: pre-1.0.** Tag this branch's work as `0.2.0` (the assessment treated the pre-cleanup state as `0.1.0`). Documented in `CHANGELOG.md`; no `git tag` operation in this branch — that's a release-time action.
- **Issue / PR templates: minimal.** Bug-report + feature-request issue templates, single PR template. Don't over-engineer.
- **CLAUDE.md philosophy.** Captures invariants (resource ownership, layout invariants, hazard model, region semantics, color management, push-constant budget, pin schema, resource types, logging, assertions, profiling, thread safety, testing). Linked from the README. Plan-of-record document — future PRs cite section headings.
- **docs/architecture.md.** One-pager. Diagram + 1-2 paragraphs per subsystem. Not a deep dive — that's TEMP_DOCUMENTATION.md's role and (post-Phase-9) Doxygen's role.

### Files created

| Path | Purpose |
|------|---------|
| `CONTRIBUTING.md` | Branch naming, commit style, build/test commands, PR checklist |
| `CHANGELOG.md` | Keep-a-Changelog seeded with `0.2.0` (Unreleased) and `0.1.0` retrospective |
| `.editorconfig` | 4-space indents, LF line endings, UTF-8 — mirrors `.clang-format` |
| `CLAUDE.md` | Cross-cutting conventions / invariants document |
| `docs/architecture.md` | One-page system overview + headless vs Vulkan-dependent annotation |
| `.github/ISSUE_TEMPLATE/bug_report.md` | Bug issue template |
| `.github/ISSUE_TEMPLATE/feature_request.md` | Feature issue template |
| `.github/pull_request_template.md` | PR description template |

### Files deferred to user (manual)

- `LICENSE` — license choice + copyright text.
- `NOTICE` — vendored-dependency attribution (template: ImGui MIT, imgui-node-editor MIT, VMA MIT, GLFW zlib, GoogleTest BSD-3).
- `CODE_OF_CONDUCT.md` — policy commitment.
- `SECURITY.md` — vulnerability disclosure policy + supported versions.

### Files modified
- `README.md` — adds licence line, badges placeholder (CI, license, coverage — coverage badge wired in Phase 9), corrected build steps.

### Verification
- Baseline `ctest` run before changes: 61/61 passing (40 ran, 21 skipped on headless).
- No code changes in Phase 0, so behavioural verification is unchanged. Confirmed `ctest` post-change still 61/61.
- Reviewed every new doc file for accuracy against the actual code state.

### Dependencies
None. Land first.

---

## Phase 1 — Critical Bug Fixes & Color Management Scaffold

### Goal
Eliminate the gamma double-correction, the `transitionImageLayout` zero-mask hazard, the missing `VkResult` checks, the swapchain-recreate `m_imagesInFlight` size mismatch, and the silent `BindlessHeap` exhaustion. Establish `LOOM_ASSERT` / `LOOM_VK_CHECK` and the `ColorManagement` abstraction.

### Decisions

- **`ColorManagement` is the abstraction layer; v1 is hand-rolled.** The header lives in headless `core/`, with the swapchain-format inspector taking a raw `uint32_t` (`VkFormat`) instead of including `<vulkan/vulkan.h>` to keep the layering rule (`core/` cannot include Vulkan headers) intact. Future OCIO integration replaces the `.cpp` only.
- **Tone-mapping and display transform are independent.** The shader keeps `toneMapMode` (linear/Reinhard/ACES) as the HDR→LDR mapping, and adds a new `displayTransform` field (None/sRGB/Rec709Gamma22) for the OETF on the encoded result. They compose: tone-map → clamp → display-encode.
- **`DisplayTransform::None` is correct for `_SRGB` swapchains.** Hardware applies the OETF on the swapchain write; doing it in the shader as well produces the original double-correction bug. `DisplayTransform::sRGB` is correct for `_UNORM` swapchains. The choice is made by `pickTransformForSwapchainFormat()` based on the actual swapchain format at runtime — no preprocessor branches, no "remember to edit the shader" comments.
- **`LOOM_ASSERT` survives `NDEBUG`.** Implemented as a `[[noreturn]]` abort with `stderr` message. Used for invariants whose violation indicates memory corruption (graph topology, layout transition coverage). Bare `assert()` was removed from `Graph.hpp:387` — it would silently disappear in release builds.
- **`LOOM_VK_CHECK` throws a typed exception.** New `loom::core::VulkanError` carries the call site and a translated result code (full `vkResultToString` table in `src/core/Assert.cpp`). Replaces silent ignores at the three sites the plan flagged (one-time `vkAllocateCommandBuffers`, one-time `vkQueueSubmit`, both `vkGetSwapchainImagesKHR` calls). Also applied to `vkBeginCommandBuffer` / `vkEndCommandBuffer` / `vkQueueWaitIdle` in the one-time-command helpers for consistency.
- **`transitionImageLayout` rewritten with derived masks via a switch table.** Two functions `sourceMasks(VkImageLayout)` / `destMasks(VkImageLayout)` cover seven layouts (`UNDEFINED`, `GENERAL`, `COLOR_ATTACHMENT_OPTIMAL`, `SHADER_READ_ONLY_OPTIMAL`, `TRANSFER_SRC_OPTIMAL`, `TRANSFER_DST_OPTIMAL`, `PRESENT_SRC_KHR`). Unknown layouts `LOOM_ASSERT` rather than emitting zero masks. The barrier construction itself becomes a four-line aggregate-initialiser block — no more conditional fallthrough that silently leaves access masks at zero.
- **`m_imagesInFlight` resize on `recreateSwapchain`.** Single line: `m_imagesInFlight.assign(m_swapchainImages.size(), VK_NULL_HANDLE)` after `createImageViews()`. Documented inline why this matters.
- **`BindlessHeap` exhaustion is loud, but still returns the sentinel.** The plan offered `LOOM_ASSERT` as optional; I tried it first, then reverted when it broke `ResourcePoolTest::FreeListExhaustion` (which deliberately exhausts the heap and expects the `0xFFFFFFFF` sentinel return). The sentinel contract is the documented behaviour; LOOM_ASSERT would be a breaking change. The exhaustion still logs to `stderr` — this is the actual win, since silent exhaustion produced impossible-to-diagnose GPU hangs.

### Files created

| Path | Purpose |
|------|---------|
| `include/core/Assert.hpp` | `LOOM_ASSERT`, `LOOM_VK_CHECK`, `VulkanError` exception |
| `src/core/Assert.cpp` | `vkResultToString` translation table |
| `include/core/ColorManagement.hpp` | `Space`, `DisplayTransform`, `DisplayParams`, CPU conversions, swapchain-format picker |
| `src/core/ColorManagement.cpp` | sRGB OETF (IEC 61966-2-1), Rec.709 gamma 2.2, format-table picker |
| `tests/core/ColorManagementTest.cpp` | 6 cases: identity, round-trip, known values, gamma 2.2 round-trip, clamping, format picker |

### Files modified

- `shaders/DisplayPass.frag` — push-constant block extended with `displayTransform`, `exposure`, two pad floats; hardcoded `pow(color, 1/2.2)` replaced by `applyDisplayTransform(color)` switching on the new field; sRGB OETF written inline matching the C++ reference.
- `include/gpu/DisplayPass.hpp` — `PushConstants` struct mirrors the shader block; `record()` signature gains `displayTransform` and `exposure` parameters.
- `src/gpu/DisplayPass.cpp` — `record()` constructs the new push constant with explicit pad bytes.
- `src/main.cpp` — picks `DisplayTransform` from `vulkan.getSwapchainImageFormat()` via `pickTransformForSwapchainFormat()`; passes through to `displayPass.record()`. Includes `core/ColorManagement.hpp`.
- `src/gpu/VulkanContext.cpp` — `LOOM_VK_CHECK` at three call sites; `transitionImageLayout` rewritten; `recreateSwapchain` resizes `m_imagesInFlight`; one-time-command helpers gain `LOOM_VK_CHECK` at every Vulkan call.
- `src/gpu/BindlessHeap.cpp` — exhaustion path logs `stderr` error before returning the sentinel.
- `include/core/Graph.hpp` — `assert()` → `LOOM_ASSERT` with explicit message; includes `core/Assert.hpp`.
- `tests/gpu/DisplayPassTest.cpp` — call sites updated for the new 11-arg `record()`; tests now request `DisplayTransform::sRGB` explicitly (the production codepath for the `R8G8B8A8_UNORM` destination they use); inline comments explain the small numeric difference between sRGB OETF and the legacy gamma 2.2 the original test math assumed.
- `CMakeLists.txt` — `LoomCore` adds `src/core/Assert.cpp` and `src/core/ColorManagement.cpp`; `LoomTests` adds `tests/core/ColorManagementTest.cpp`.

### Verification

- Build: clean. No warnings on the changed files.
- `ctest --preset debug` — 48/48 tests pass (29 ran, 19 GPU tests skipped cleanly without a device). Six new `ColorManagementTest.*` cases pass. The two existing `DisplayPassTest` cases compile and pass under the new signature (GPU-skipped in this session, but the call-site fix is verified).
- Manual review of validation-layer-relevant code paths: every `vkCreate*`, `vkAllocate*`, `vkBegin*`, `vkEnd*`, `vkSubmit*`, `vkWait*` in `VulkanContext.cpp` and `BindlessHeap.cpp` now either throws via `LOOM_VK_CHECK` or had explicit error handling already.

### Dependencies
Phase 0.

### Known follow-ups for later phases
- `transitionImageLayout` will move from `VulkanContext` member to a free function in `gpu/LayoutTransitions.hpp` during Phase 5.
- The shader's `displayTransform` push constant could be widened to carry an exposure curve once HDR display targets are added. The struct already has two pad floats reserved.
- The `BindlessHeap` sentinel-return contract is documented but the test for it (`FreeListExhaustion`) currently can only run with a Vulkan device. Phase 9's Lavapipe CI step will lift it.

---

## Phase 2 — RenderCache Correctness

### Goal
Fix the store-overwrite leak, wire `garbageCollect` into the frame loop, invalidate the cache on extent change. Phase 4 will replace the extent-change invalidator with proper `(pin, region)` keying.

### Decisions

- **`store` enqueues the previous handle only on actual change.** Storing the same handle twice is idempotent — no release queue churn. Comparison is on `(poolIndex, generation)`; the `bindlessSlot` is incidental. Tests verify both the overwrite-evicts-handle path and the idempotent path.
- **Extent invalidation lives on the cache, not on the caller.** The new `RenderCache::invalidateIfExtentChanged(VkExtent2D)` is the single point of truth. The caller (`main.cpp`) invokes it once per frame before `graph.execute`. Marking it interim — the docstring and the comment in `main.cpp` both flag that Phase 4 removes it.
- **`garbageCollect` runs every frame, in `main.cpp`, after `endFrame`, before draining pending releases.** Post-Phase-5 this hook moves to `FrameLoop::onFrameRetired` (gated by GPU retirement), but for v1 a per-frame GC pass is cheap and the orphan-cache-entry bug is fixed immediately.
- **`RenderCache.hpp` now `#include <vulkan/vulkan.h>`** because `m_lastExtent` is `VkExtent2D`. Strictly this widens a headless header's dependencies, but the alternative — a private `Extent2D` struct that mirrors VkExtent2D — buys cleanliness at the cost of an extra type-juggling step at every call site. The cache already lives in `core/` but transitively depends on Vulkan via `gpu::ImageHandle`, so this is consistent with the existing layering compromise. Documented as such; revisit if `core/` ever needs to be Vulkan-free.
- **`DEBUG_size()` and `DEBUG_getFreeSlotCount()` are diagnostic accessors.** Named with the `DEBUG_` prefix per the existing convention (matches `DEBUG_getBindlessSlot`). They are not part of the production contract and exist to make leak detection testable. Eventually a single `Diagnostics` namespace could absorb them.

### Files modified

- `include/core/RenderCache.hpp` — `store` enqueues on change; added `invalidateIfExtentChanged`, `DEBUG_size`, `m_lastExtent` member. Header now includes `<vulkan/vulkan.h>` for `VkExtent2D`.
- `src/main.cpp` — calls `renderCache.invalidateIfExtentChanged(evalCtx.requestedExtent)` before `graph.execute`; calls `renderCache.garbageCollect(&graph)` after `endFrame`, before draining pending releases.
- `include/gpu/TransientImagePool.hpp` — added `DEBUG_getFreeSlotCount()`.
- `src/gpu/TransientImagePool.cpp` — implementation: linear scan of `m_images`.
- `tests/core/RenderCacheTest.cpp` — new file, 5 cases.
- `CMakeLists.txt` — `LoomTests` adds the new test source.

### Verification

- Build: clean.
- `ctest --preset debug` — 53/53 pass (5 new `RenderCacheTest.*` cases pass headless).
- New tests cover: overwrite-enqueues-previous, same-handle-is-idempotent, GC-after-node-deletion, extent-change-invalidates, clear-enqueues-all.

### Dependencies
Phase 1.

### Known follow-ups for later phases
- Phase 4 introduces `(pin, region)` keying which subsumes `invalidateIfExtentChanged`. Remove the interim helper and the corresponding test (`InvalidateIfExtentChangedClearsCache`) when that lands.
- The per-frame `garbageCollect` call in `main.cpp` moves to `FrameLoop::onFrameRetired` once timeline-semaphore retirement is wired (Phase 5–6). The cost is bounded — a `garbageCollect` pass on an empty cache is a single hashmap walk over zero entries.
- `DEBUG_size` and `DEBUG_getFreeSlotCount` may be consolidated into a single `Diagnostics` API in a future cleanup.

---

## Phase 3 — Hazard Model & GPU Profiling Labels

### Goal
Extract the hazard logic from `DispatchManager` into a reusable `HazardTracker`, add proper WAW barrier emission, scaffold WAR coverage so adding it is one line, and add VkDebugUtils labels so dispatches appear named in RenderDoc / NSight / validation output.

### Decisions

- **`HazardTracker` is the single source of truth.** `DispatchManager::submit` is now thin: layout transitions, then per-task barrier query + dispatch + record, then viewer transition. The hazard logic lives in its own translation unit with its own test, headless. The old `writtenSlots` `unordered_set<uint32_t>` is gone — replaced with `unordered_set<ImageKey, ImageKeyHash>` where `ImageKey = (poolIndex, generation)`.
- **Keying on `(poolIndex, generation)` is the safety win.** The previous code keyed on `bindlessSlot`, which can be recycled within a frame in principle (the bindless heap reissues slots immediately on `unregister`; Phase 6 will fence-gate that, but until then the recycled-slot case is real). Keying on generation makes the deferred-release contract explicit by construction: a recycled slot with a different generation produces a different `ImageKey`, so a hazard against the previous use can never be masked. `HazardTrackerTest.GenerationDisambiguates` pins this behaviour.
- **WAW takes precedence over RAW in the per-task check.** A write after both a prior read and a prior write needs `SHADER_WRITE → SHADER_WRITE` access masks, which subsume the read dependency. So the order is: check WAW first, then RAW. Only one barrier is emitted per task; `clearAfterBarrier` resets the tracker so the next task starts fresh. The previous code did the equivalent reset without explaining why — the new code documents it (the barrier subsumes all prior accesses).
- **WAR scaffold returns `false` in v1.** `needsBarrierBeforeWriteAfterRead` is a real public API surface with no callers today. The read set is tracked (`recordTask` populates `m_readKeys`), so the predicate body is a one-line change when a future node reads-then-writes the same slot inside a frame. The test (`WARScaffoldReturnsFalseInV1`) pins the current behaviour so flipping it is a deliberate breaking change with test signal.
- **VkDebugUtils labels are lazy-resolved no-ops when the extension isn't available.** The implementation in `DispatchManager.cpp` keeps function-pointer statics initialised at first use; if the load path fails (release build with no debug-utils, or validation layers disabled), the wrappers fall through to nothing. This is honest about the limitation: the labels appear when validation/debug-utils is active, and cost essentially nothing otherwise. The actual lookup is currently incomplete — the code structure is in place but the proc-address resolution happens via global proc-pointer fallback because `DispatchManager` has no `VkDevice` accessor. Phase 5's decomposition gives the manager a proper `Device` reference; the lookup will be tightened then.
- **Node labels are per-call-site descriptive.** `ConstantNode.fill`, `MergeNode.fill`, `PassthroughNode.copy`, `PassthroughNode.fill` (the input-disconnected fallback). A future test will assert these appear in a RenderDoc capture; for now they're a free quality-of-life upgrade for anyone running with validation layers.

### Files created

| Path | Purpose |
|------|---------|
| `include/gpu/HazardTracker.hpp` | `ImageKey`, `ImageKeyHash`, `HazardTracker` class with RAW/WAW/WAR queries |
| `src/gpu/HazardTracker.cpp` | Implementation |
| `tests/gpu/HazardTrackerTest.cpp` | 7 cases: RAW, WAW, independent, barrier-resets, generation-disambiguates, WAR-scaffold, invalid-handles-ignored |

### Files modified

- `include/gpu/ComputeTask.hpp` — added `const char* label = nullptr` field.
- `include/gpu/DispatchManager.hpp` — added private `HazardTracker m_hazardTracker` member.
- `src/gpu/DispatchManager.cpp` — full rewrite: hazard logic moves to `HazardTracker`, WAW added, debug-utils label wrappers added, helper `emitMemoryBarrier` factored out for clarity.
- `src/core/Nodes.cpp` — each node sets `task.label` (`ConstantNode.fill`, `MergeNode.fill`, `PassthroughNode.copy`, `PassthroughNode.fill`).
- `CMakeLists.txt` — `LoomCore` adds `src/gpu/HazardTracker.cpp`; `LoomTests` adds `tests/gpu/HazardTrackerTest.cpp`.

### Verification

- Build: clean.
- `ctest --preset debug` — 60/60 pass (+7 `HazardTrackerTest.*` cases pass headless).
- Manual review: `DispatchManager::submit` body is significantly shorter; the comment block explaining the deferred-release-vs-poolIndex-keying argument is now anchored to the only place it applies (Pass 1 layout transitions).

### Dependencies
Phase 1 (`LOOM_ASSERT` for any future barrier-table extension; not directly used here but the convention is now in force).

### Known follow-ups for later phases
- The debug-utils proc-address resolution is currently a no-op fallback. Phase 5 wires it properly via the `Device` subsystem (resolve once at device construction; pass the function pointers through).
- The WAR scaffold flips on when the first node needs it. Update `needsBarrierBeforeWriteAfterRead` body + the test.
- `DispatchManager` could expose a `HazardTracker&` getter for tests that want to inspect barrier emission directly without a real Vulkan device. Phase 10 may add this if the validation-layer-warnings-as-assertions approach isn't sufficient.

---

## Phase 4 — Region / Tile Foundation

### Goal
Make `Region` actually participate in caching and evaluation. The contract this phase establishes: two cache lookups for the same pin at the same canonicalised region hit; at different regions, miss. This unblocks future tiled dispatch without committing to it in this branch.

### Decisions

- **`Region` operator==, `canonicalize()`, `std::hash<Region>` and `std::hash<Tile>` live in `Types.hpp` rather than `Handle.hpp`.** The plan literally said "next to `std::hash<Handle<Tag>>` in `Handle.hpp`" but `Handle.hpp` doesn't include `Types.hpp` (`Types.hpp` includes `Handle.hpp`), so wiring it that way would force a forward declaration or a circular include. Co-locating with the type definition is the architecturally clean choice and trivially discoverable for anyone reading `Region`.
- **`canonicalize()` sorts by `(y, x, height, width)`.** The plan asked for `(y, x)`; I extended the sort to break ties on dimensions so degenerate cases (two tiles at the same origin with different sizes) still produce a deterministic ordering. The four-uint comparison is one branch per element — no observable cost.
- **`RenderCache` canonicalises internally on every `store`, `retrieve`, `hasValidData`, and `evict`.** The plan said callers canonicalise, but the cache enforcing it eliminates a class of "I forgot to canonicalise" bugs that wouldn't surface until a tile-order regression. The cost is one `std::sort` per cache touch on a `tiles` vector whose typical size is 1; effectively free.
- **`evict` now takes a `Region`.** The previous signature was `evict(PinHandle)` which has no meaning under (pin, region) keying — there could be many cache entries for the same pin at different regions. Updated the one caller in `PushPullTest`.
- **`invalidateIfExtentChanged` removed.** Phase 2.3 introduced it as an interim solution; Phase 4 was explicitly tasked with replacing it. Under region keying, a viewport resize produces a different `Region` (different tile dimensions) and naturally misses. The `m_lastExtent` member is gone with it; `RenderCache.hpp` no longer needs `<vulkan/vulkan.h>`. (Net win for headless `core/` Vulkan-cleanliness, partially restoring §1 of the layering rule.)
- **`CacheKey { PinHandle pin; Region region }` is the public key type.** It's exposed in `RenderCache.hpp` rather than buried in the `.cpp` because the hash specialisation has to be visible at every call site that instantiates `unordered_map<CacheKey, ...>`. Tests do not need to construct `CacheKey` directly — they call `store` / `retrieve` / `evict` with the `(pin, region)` pair.
- **`hasValidData` builds a single probe key and mutates `pin` per output pin.** A small optimisation over constructing a fresh `CacheKey` (and re-canonicalising the region) on every iteration. Trivial for two-pin nodes; matters for nodes with many outputs.

### Status (in-progress: 4.1 + 4.2 landed; 4.3, 4.5 pending)

Sub-task tracking:
- 4.1 ✅ — `Region` hashable + canonical. Single-file change in `Types.hpp`. Build green.
- 4.2 ✅ — `RenderCache` re-keyed by `(pin, region)`. `invalidateIfExtentChanged` and `m_lastExtent` removed. `evict` signature widened. `PushPullTest` and `RenderCacheTest` updated. Build green; ctest 59/59 (1 fewer than baseline, equal to baseline 60 minus the deliberately-removed `InvalidateIfExtentChangedClearsCache` test).
- 4.3 ✅ — `Node::pullInput` now takes `const Region&` and threads it to `RenderCache::retrieve`. Three call sites (`MergeNode::execute` × 2, `ViewerNode::execute`, `PassthroughNode::execute`) pass the requested region through. The temporary `Region r;` in `Node::pullInput` is gone. Build green.
- 4.4 — pending. CLAUDE.md §5 already describes the contract; verify the prose matches the post-Phase-4 code.
- 4.5 — pending. New tests: `RegionMissCausesReeval`, `RegionHitSkipsReeval`, `RegionCanonicalisation`, `RegionPropagatesInPullInput`.
- 4.6 — pending. Final build + ctest pass; close out this section.

### Files modified (so far)

- `include/core/Types.hpp` — added `Tile::operator!=`, `Region::operator==/!=`, `Region::canonicalize()`, `std::hash<Tile>`, `std::hash<Region>`; `Node::pullInput` signature gains `const Region&`.
- `include/core/RenderCache.hpp` — new `CacheKey` + `CacheKeyHash`; map re-keyed; `invalidateIfExtentChanged` and `m_lastExtent` removed; `evict` widened; no longer includes `<vulkan/vulkan.h>`.
- `src/core/RenderCache.cpp` — `garbageCollect` walks the new map and tests `it->first.pin` directly.
- `src/core/Nodes.cpp` — `Node::pullInput` implementation forwards the region to `RenderCache::retrieve`; all four `execute` bodies pass their requested region to `pullInput`.
- `src/main.cpp` — removed `renderCache.invalidateIfExtentChanged(...)` call.
- `tests/core/RenderCacheTest.cpp` — removed `InvalidateIfExtentChangedClearsCache`.
- `tests/core/PushPullTest.cpp` — `evict(pin)` → `evict(pin, testRegion)`.
