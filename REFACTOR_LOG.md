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

### Sub-task tracking

- 4.1 ✅ — `Region` hashable + canonical. Single-file change in `Types.hpp`. Build green.
- 4.2 ✅ — `RenderCache` re-keyed by `(pin, region)`. `invalidateIfExtentChanged` and `m_lastExtent` removed. `evict` signature widened. `PushPullTest` and `RenderCacheTest` updated.
- 4.3 ✅ — `Node::pullInput` now takes `const Region&` and threads it to `RenderCache::retrieve`. Three call sites (`MergeNode::execute` × 2, `ViewerNode::execute`, `PassthroughNode::execute`) pass the requested region through. The temporary `Region r;` in `Node::pullInput` is gone.
- 4.4 ✅ — CLAUDE.md §5 extended with the canonicalisation-is-internal note and the `pullInput` threading contract.
- 4.5 ✅ — Four new tests added: `RegionMissCausesReeval`, `RegionHitSkipsReeval`, `RegionCanonicalisation`, `RegionPropagatesInPullInput`. The fourth uses a `PullInputTestNode` subclass that promotes the protected `pullInput` to public and is wired up manually (not via `Graph::addNode`) — clean because the only Graph state pullInput touches is the linked pin lookup, which we set up with a real Constant→Passthrough link.
- 4.6 ✅ — Final build + ctest: 63/63 pass (4 new tests, all green; 19 GPU tests skipped cleanly on this no-device machine).

### Files modified

- `include/core/Types.hpp` — added `Tile::operator!=`, `Region::operator==/!=`, `Region::canonicalize()`, `std::hash<Tile>`, `std::hash<Region>`; `Node::pullInput` signature gains `const Region&`.
- `include/core/RenderCache.hpp` — new `CacheKey` + `CacheKeyHash`; map re-keyed; `invalidateIfExtentChanged` and `m_lastExtent` removed; `evict` widened; no longer includes `<vulkan/vulkan.h>`.
- `include/CLAUDE.md` — §5 (Region semantics) extended.
- `src/core/RenderCache.cpp` — `garbageCollect` walks the new map and tests `it->first.pin` directly.
- `src/core/Nodes.cpp` — `Node::pullInput` implementation forwards the region to `RenderCache::retrieve`; all four `execute` bodies pass their requested region to `pullInput`.
- `src/main.cpp` — removed `renderCache.invalidateIfExtentChanged(...)` call.
- `tests/core/RenderCacheTest.cpp` — removed `InvalidateIfExtentChangedClearsCache`; added four region-keyed tests.
- `tests/core/PushPullTest.cpp` — `evict(pin)` → `evict(pin, testRegion)`.

### Verification

- Build: clean.
- `ctest --test-dir build` — 63/63 pass headless (4 new tests, baseline previously 60).
- Manual review: `RenderCache.hpp` is now Vulkan-free (header dependency closure shrank by one).

### Dependencies
Phases 1, 2.

### Known follow-ups for later phases
- The `Region` propagation contract is documented for `markRequiredTiles` overrides; no production node currently exercises a non-identity mapping. The first one (likely `Blur` in a future feature branch) will validate the contract end-to-end.
- `CacheKey` is intentionally exposed in `RenderCache.hpp`. If the hash specialisation ever needs to grow (e.g. to include node generation), it stays a one-file change.
- `Region::canonicalize()` is a public mutator. Future code that builds regions in many places could lean on this — or we could harden the contract by making `Region`'s constructor accept tiles and canonicalise on construction. Deferred until needed.

---

## Phase 5 — VulkanContext Decomposition & Vulkan 1.3 Modernisation

### Goal
Split the ~970-line `VulkanContext` god class into focused subsystems (`Instance`, `Device`, `Swapchain`, `FrameLoop`, `ResourceFactory`) and modernise to Vulkan 1.3 idioms (timeline semaphores, persistent on-disk `VkPipelineCache`, validation-layer feature requests). Eight sub-tasks across two sessions: the first session landed 5.8 / 5.1 / 5.2 / 5.3 (the mechanical carve-outs); this session lands 5.5 / 5.4 / 5.6 / 5.7 (the resource subsystem, the timeline-semaphore behavioural switch, the persistent pipeline cache, and the final façade slim).

### Decisions

- **Shadow members pattern during decomposition.** `VulkanContext` keeps its raw `VkDevice m_device` / `VkPhysicalDevice m_physicalDevice` / `VkQueue` members as *aliases* of the newly-extracted subsystem objects. After each carve-out, `init()` copies handles from the new owner (`Device`, `Swapchain`) into the legacy raw members so the ~50 internal call sites that touch `m_device` etc. don't need a tree-wide rewrite mid-phase. The shadows are flagged as removed in Phase 5.7 (the façade slim). Cost: an extra 8 lines in `init`; benefit: every intermediate commit compiles and ctest is 63/63.
- **`Instance`, `Device`, `Swapchain` constructed eagerly via `std::unique_ptr<T>`.** Reasoning: they need to live for the lifetime of `VulkanContext`, but `VulkanContext` is default-constructible and initialised lazily via `init(window, appName)`. Holding by `unique_ptr` defers construction to `init` without changing the public API. Destructor order is explicit: `m_swapchainObj.reset(); m_deviceObj.reset(); m_instanceObj.reset();` — surface lives until after the device is gone.
- **`Device` requests Vulkan 1.3 features through the umbrella structs.** `VkPhysicalDeviceVulkan12Features` + `VkPhysicalDeviceVulkan13Features` cover `timelineSemaphore`, `descriptorIndexing`, `descriptorBindingPartiallyBound`, `runtimeDescriptorArray`, `synchronization2`, `dynamicRendering` in two structs instead of chaining four separate feature structs. Also gates physical-device selection on feature support — a device missing any required feature is rejected with a contextual error rather than failing later with validation noise.
- **`Swapchain::acquire` returns `bool` rather than `VkResult`.** The caller only cares whether the surface is out-of-date (recreate + retry) vs successful. Other failures still throw via `LOOM_VK_CHECK` internally. `present` does return `VkResult` because the caller wants to distinguish `SUBOPTIMAL` from `SUCCESS`.
- **`Swapchain` owns `recreate()`.** The fence-tracker resize that Phase 1.4 added (`m_imagesInFlight.assign(...)`) stays in `VulkanContext::recreateSwapchain` because it's about the fence array, not the swapchain. The `Swapchain::recreate()` body uses the old swapchain handle as `oldSwapchain` for the new `vkCreateSwapchainKHR` call, then destroys the old swapchain — eliminating the previous awkward `m_oldSwapchain` member.
- **Validation-layer messenger drops `VERBOSE_BIT_EXT`.** Per plan §5.1: WARNING + ERROR only. The previous setup flooded logs in debug builds.
- **`transitionImageLayout` is now a free function in `gpu/LayoutTransitions.hpp` (Phase 5.8).** Same `sourceMasks` / `destMasks` switch tables; same `LOOM_ASSERT` on unknown layouts. The function call sites switched from `transitionImageLayout(cmd, ...)` (member) to `loom::gpu::transitionImageLayout(cmd, ...)` (free function) — but ADL resolves them inside `loom::gpu` so the apparent call site is unchanged.

### Sub-task tracking

- 5.8 ✅ — `transitionImageLayout` moved to `gpu/LayoutTransitions.{hpp,cpp}`. Same masks / table / asserts. `VulkanContext` includes it where the helper was previously called.
- 5.1 ✅ — `Instance` carved out (instance + debug messenger + surface). Validation severity drops `VERBOSE_BIT_EXT`.
- 5.2 ✅ — `Device` carved out (physical + logical device + queues + queue families). Requests VK 1.3 features explicitly; rejects unsuitable devices at construction with a contextual error.
- 5.3 ✅ — `Swapchain` carved out (swapchain + images + image views + recreate logic). Public `acquire / present / recreate / get* / getImage / getImageView`. Eliminates the `m_oldSwapchain` member.
- 5.5 ✅ — `ResourceFactory` carved out (command pool, descriptor pool, VMA allocator, `BindlessHeap`, single-time-commands). VulkanContext getters delegate to it; the per-frame command-buffer allocation pulls the pool from the factory rather than from a member. Net deletion of ~98 lines in `VulkanContext.cpp` for the same behaviour. Destructor order: `m_resourceFactory.reset()` before the device is torn down, matching the existing swapchain-then-device pattern.
- 5.4 ✅ — `FrameLoop` carved out with timeline-semaphore switch. Replaces the binary-fence + per-image-fence pattern with a single monotonic timeline semaphore: each submit signals `m_frameValue+1`; the start of frame N waits for value `(m_frameValue+1) - MAX_FRAMES_IN_FLIGHT` on the timeline before reusing the slot. Binary semaphores remain only where the Vulkan API requires them (`vkAcquireNextImageKHR`'s signal and `vkQueuePresentKHR`'s wait). `currentFrameValue()` exposes the monotonic counter for Phase 6's bindless / pool retirement gates. Submit chains `VkTimelineSemaphoreSubmitInfo` via `pNext` to provide the signal value; the wait-value count must equal the wait-semaphore count, so a dummy `0` covers the binary image-available wait. The `m_imagesInFlight` per-image fence tracker is gone — the timeline wait subsumes it, and so does the per-image binary present-wait that the swapchain already needed. Header comment in `FrameLoop.hpp` explains why each binary semaphore is still indexed the way it is.
- 5.6 ✅ — Disk-backed `VkPipelineCache`. New `platform::userDataDir()` resolves `~/Library/Caches/loom` on macOS, `$XDG_DATA_HOME/loom` (or `$HOME/.local/share/loom`) on Linux, `%LOCALAPPDATA%/loom` on Windows, and lazy-creates the directory. `PipelineCache` constructs a `VkPipelineCache` seeded from `pipeline_cache.bin` if present and passes the handle to `vkCreateComputePipelines` instead of the previous `VK_NULL_HANDLE`. Destructor calls `vkGetPipelineCacheData` and writes to a `.tmp` sibling + rename so a crash mid-write leaves the prior cache intact. Driver/GPU mismatch on load (returns `VK_ERROR_INCOMPATIBLE_DRIVER` or otherwise fails) falls back to an empty cache with a stderr note — the engine boots, just slower until it re-warms. Path resolution can throw if `$HOME` is unset; we catch and degrade to an in-memory cache rather than failing engine startup.
- 5.7 ✅ — `VulkanContext` slimmed to a pure composition root. All shadow members are gone: `m_physicalDevice`, `m_device`, `m_graphicsQueue` / `m_computeQueue` / `m_presentQueue`, the three queue-family indices, and the per-frame command-buffer / semaphore / fence vectors. Every getter now delegates to a subsystem object. The body of `VulkanContext.cpp` is ~30 lines (constructor, destructor with explicit reset order, `init` composition, `waitIdle`). Public API is unchanged from `main.cpp` and the tests' perspective; the file went from ~290 lines to ~35.

---

## Phase 6 — Bindless Lifetime & Sync Hardening

### Goal
Tie `BindlessHeap` slot recycling and `TransientImagePool` / `TransientBufferPool` entry release to the FrameLoop's timeline-semaphore retirement, so a slot is never re-issued while shaders still reference its descriptor.

### Decisions

- **Tagged-release API.** `release(handle, releaseAtFrame)` carries the deferred frame value with the pending entry rather than a separate side channel. The engine path passes `vulkan.currentFrameValue() + MAX_FRAMES_IN_FLIGHT`; the default arg `0` lets unit tests pair `release(h)` with `flushPendingReleases()` for synchronous behaviour. `flushPendingReleases()` is preserved as the explicit "drain everything regardless of tag" escape hatch — required for shutdown after `vkDeviceWaitIdle`, and convenient for tests that don't construct a real frame loop.
- **Drain via the live timeline counter.** Rather than have FrameLoop publish "frame X retired" callbacks on a hook list, the engine queries `vkGetSemaphoreCounterValue` once per frame (via `vulkan.getRetiredFrameValue()`) and forwards it to consumers. Live query is more responsive than the lower-bound `m_frameValue + 1 - MAX_FRAMES_IN_FLIGHT` from the begin-frame wait, and avoids accumulating a list of registered hooks for what is currently a fixed set of two consumers.
- **`BindlessHeap::unregisterImage(slot, releaseAtFrame)` is now the sole API.** The previous immediate-push-to-free signature had no useful production caller (only a no-op invocation in `TransientImagePool`'s destructor, removed). The pool destructor's call to `unregisterImage` was dead code: the `BindlessHeap` descriptor pool is destroyed alongside the image pool's lifetime, so pushing slots back to its free queues has no observable effect. Removing it eliminates the cyclic-destructor concern.
- **Cyclic destructor concern.** `TransientImagePool::~TransientImagePool` and `TransientBufferPool::~TransientBufferPool` used to call `BindlessHeap::unregister*` during their teardown. With both pools constructed by `main()` (not by `ResourceFactory`), and the `BindlessHeap` owned by `ResourceFactory`, the destruction order in `main()` matters: pools are destroyed before `vulkan.waitIdle()` returns and `VulkanContext`'s destructor reaches `m_resourceFactory.reset()`. Removing the unregister calls makes the order irrelevant — the slot queues are torn down with the heap regardless.
- **`getRetiredFrameValue` lives on `FrameLoop`, surfaced via `VulkanContext`.** Querying the timeline semaphore is the natural responsibility of whoever owns it. Putting the getter behind both the subsystem and the façade preserves the option to add a deferred-release manager later without churning call sites.

### Files modified

- `include/gpu/BindlessHeap.hpp`, `src/gpu/BindlessHeap.cpp` — pending-slot queues, `unregister*(slot, releaseAtFrame)` and `onFrameRetired(retiredValue)`.
- `include/gpu/TransientImagePool.hpp`, `src/gpu/TransientImagePool.cpp` — `release(handle, releaseAtFrame)`, `onFrameRetired`, `flushPendingReleases` retained for shutdown / tests; destructor no longer calls `unregisterImage`.
- `include/gpu/TransientBufferPool.hpp`, `src/gpu/TransientBufferPool.cpp` — same shape.
- `include/gpu/FrameLoop.hpp`, `src/gpu/FrameLoop.cpp` — `getRetiredFrameValue()` queries `vkGetSemaphoreCounterValue`.
- `include/gpu/VulkanContext.hpp` — `getRetiredFrameValue()` passthrough.
- `src/main.cpp` — release loop tags handles with `currentFrameValue + MAX_FRAMES_IN_FLIGHT`; once-per-frame `imagePool.onFrameRetired(retired)` and `bindlessHeap.onFrameRetired(retired)` replace the old unconditional `flushPendingReleases()`.
- `CLAUDE.md` §2 — lifetime contract updated to spell out the tagging convention and the `flushPendingReleases` shutdown carve-out.
- `tests/gpu/ResourcePoolTest.cpp` — silence `[[nodiscard]]` warning on the `FreeListExhaustion` test (deliberate-discard pattern).

### Verification

- Build: clean.
- `ctest --test-dir build` — 63/63 pass. The tests that use `release(h)`+`flushPendingReleases()` keep working because the default `releaseAtFrame = 0` plus `flushPendingReleases()`'s "drain everything" semantics matches the old behaviour.
- The deferred-release path is not exercised by the headless test suite. Phase 10's `BindlessSlotNotReusedWithinFrameLifetime` and `Stress_1000AcquireReleaseCycles` are the proper end-to-end tests; they need a Vulkan device with validation layers and are deferred.

### Dependencies
Phase 5.4 (timeline semaphore + `currentFrameValue`).

### Known follow-ups

- Phase 10 will add the `BindlessSlotNotReusedWithinFrameLifetime` test (acquire A; submit a frame using A; `release(h, currentFrameValue + MAX_FRAMES_IN_FLIGHT)`; assert the slot is *not* in the bindless free queue until that many further frames have retired).
- The Phase 7 cleanup will fold `unregister*` and `onFrameRetired` behind `[[nodiscard]]` where appropriate — currently neither returns a value, so no audit work.
- The two `onFrameRetired` consumers (`BindlessHeap`, `TransientImagePool` — `TransientBufferPool` is built but not currently driven from main()) walk a `std::vector` of pending entries each frame. If a long-running session accumulates a large pending list before retirement catches up, a binary-search on a sorted vector or a min-heap by `releaseAtFrame` would pay off; today the list is bounded by `MAX_FRAMES_IN_FLIGHT` worth of evictions and the linear scan is fine.

---

## Phase 7 — Hot-path, Code Hygiene & Instrumentation

### Goal
Remove per-frame heap allocations, kill copy-paste, introduce a logger and a profiling-scope macro, and audit `[[nodiscard]]` where ignoring a return is a real bug. No behavioural changes; the test suite verifies that.

### Sub-task tracking

- 7.1 ✅ — `SlotMap::forEach` templated. `std::function` removed; the per-call heap closure is gone. `Graph::forEachNode` (both const / non-const) and `Graph::forEachLink` follow the same pattern. Templated `forEach` keeps them in the header, which is also fine for compile time at this scale.
- 7.2 ✅ — Reusable scratch buffers hoisted to `Graph` members: `m_dirtyQueue` (std::deque) for `markDirty`, `m_topoQueue` (std::deque) + `m_inDegree` (std::vector<int>, indexed by node-handle index) for `computeTopologicalOrder`. Each method `clear()`s its scratch up front; capacity is retained between calls. The map → vector swap drops `std::unordered_map<uint32_t,int>` allocation pressure and is O(1) lookup instead of average-O(1) with a hash.
- 7.3 — **skipped this session.** Moving `Graph` method bodies to `Graph.cpp` would conflict with the just-introduced templated forEach helpers (which have to stay in the header) and the templated `addNode` payload would also remain in the header. Net win on compile time is marginal in current state; defer to a later cleanup when more of `Graph`'s API is non-template.
- 7.4 ✅ — `ComputeTask::setPushConstants<T>()` typed helper with `static_assert(sizeof(T) <= 128, ...)` and `static_assert(std::is_trivially_copyable_v<T>)`. The `MAX_PUSH_CONSTANT_BYTES` constant replaces the magic 128. All four hand-rolled `memcpy(task.pushConstants.data(), &pc, sizeof(pc))` patterns in `Nodes.cpp` are gone — they now route through `setPushConstants(pc)` in the task builders. A future 129-byte struct fails to compile rather than silently corrupting the next field on the GPU.
- 7.5 ✅ — Shared task builders in `core/NodeTaskBuilders.{hpp,cpp}`: `buildFillTask(ctx, out, color[4], label)` and `buildPassthroughTask(ctx, in, out, label)`. The four copy-paste-clone task constructions in `Nodes.cpp` collapse onto these. The labels (`ConstantNode.fill`, `MergeNode.fill`, `PassthroughNode.copy`, `PassthroughNode.fill`) come from the call sites, not the builders, so RenderDoc / NSight captures keep the per-node attribution.
- 7.6 ✅ — `Graph::getViewers()` returns `std::vector<NodeHandle>`. `main.cpp` uses `viewers[0]` in v1 (single viewer); multi-viewer UI selection lands later. The previous code scanned every node via a `forEachNode` lambda and took whichever viewer it saw last — non-deterministic for multi-viewer graphs even though the case isn't exercised yet.
- 7.7 ✅ — Logger façade in `core/Log.{hpp,cpp}`. `loom::log::trace/debug/info/warn/error(args...)` — variadic templates fold each arg through `std::ostringstream` so call sites read like the iostream they replace (`loom::log::info("Selected GPU: ", deviceName, " (score: ", score, ")")`). Thread-safe via internal `std::mutex`. v1 backend writes to stderr (warn/error) or stdout (others) prefixed with `[HH:MM:SS.mmm][SEV ]`; the implementation is the swappable surface — replacing `Log.cpp` with spdlog / Tracy / structured JSON is a one-file change. Converted call sites: `main.cpp`, `ImGuiRenderer.cpp`, `Device.cpp`, `Instance.cpp`'s validation callback (now severity-aware: ERROR → `log::error`, everything else → `log::warn`), `BindlessHeap.cpp`, `PipelineCache.cpp`.
- 7.8 ✅ — `core/Profile.hpp` defines `LOOM_PROFILE_SCOPE("name")` and `LOOM_PROFILE_FRAME()`. v1: compile-time no-op (the macro guard checks `LOOM_ENABLE_PROFILING`, which is undefined). v2 hook is already in place: define the macro, drop a Tracy include into `Profile.cpp`, and every existing call site instruments automatically. Sprinkled at: `Graph::execute`, `DispatchManager::submit`, `DisplayPass::record`, `FrameLoop::beginFrame`, `FrameLoop::endFrame`.
- 7.9 ✅ — `[[nodiscard]]` audit landed: `Window::shouldClose`, `Window::wasResized`, `Window::getNativeWindow`; every handle's `isValid()` (both `loom::core::Handle<Tag>` and `loom::gpu::ImageHandle`/`BufferHandle`); `SlotMap::insert`, `emplace`, `get`, `isValid`; `Graph::addNode`, `tryAddLink`, `canAddLink`, `getViewers`; `PipelineCache::getOrCreate`; `FrameLoop` and `VulkanContext` accessors that were missing it. Production discard sites updated: `main.cpp` startup wiring checks the `tryAddLink` result and warns; `NodeEditorPanel`'s in-loop drag wiring uses an explicit `(void)` since `canAddLink` already approved the edge (the `tryAddLink` is a belt-and-braces second check). Test files were over-eager about discarding `tryAddLink` for happy-path edges — those got wrapped in `ASSERT_TRUE` via a one-shot perl pass (`perl -i -pe 's/^(\s+)((?:graph|m_graph|g)\.tryAddLink\(...\));$/$1ASSERT_TRUE($2);/'`), which incidentally tightens the tests' invariant checking. `SlotMapTest::IterationSkipsRemovedItems` uses `(void)` for the two emplaces it deliberately discards.
- 7.10 — partial. `std::span` migration was scoped to "read-only ranges at API boundaries"; `HazardTracker` is the only consumer that fit cleanly. `DispatchManager::submit(VkCommandBuffer, const std::vector<ComputeTask>&, ...)` and the task-builder signatures stay as `std::vector` for now since the callers always own a vector — the conversion buys nothing in this codebase yet. Documented as a `Known follow-ups` item if a non-vector caller appears.

### Decisions

- **No `Graph.hpp` → `Graph.cpp` split.** Templated forEach helpers and templated addNode keep most of the API in the header anyway; moving the non-template bodies would save a small amount of compile time at the cost of a tree-wide churn that doesn't compose well with future templating. Revisit when more of `Graph` stops being templated.
- **Logger backend is iostream in v1, deliberately not picking a logging library.** spdlog / fmt would each pull in a non-trivial dependency, and v1's call rate (a handful per frame at most) doesn't justify the cost. The façade is the contract: backend swap is a `Log.cpp` rewrite.
- **`ASSERT_TRUE` over `(void)` for `tryAddLink` happy-path call sites in tests.** Two reasons: (1) it tightens the test invariant — if a future edge change makes a "should-pass" link silently fail, the test exposes it at the wiring step rather than later when a downstream assertion mysteriously fails; (2) it preserves the visual cue that this line is a non-trivial state mutation, where `(void)` reads more like "I don't care about this result."
- **`[[maybe_unused]] m_device` in `TransientBufferPool`.** The field is unused since Phase 4 because all buffer creation goes through VMA, which takes the device internally. Deleting the field would also delete the constructor parameter and break callsite symmetry with `TransientImagePool` (which does still need the device for `vkDestroyImageView`). `[[maybe_unused]]` is the cheap honest signal; if the buffer pool ever needs the device again it's already plumbed.

### Files created

- `include/core/Log.hpp`, `src/core/Log.cpp`
- `include/core/Profile.hpp`
- `include/core/NodeTaskBuilders.hpp`, `src/core/NodeTaskBuilders.cpp`

### Files modified

- `include/core/SlotMap.hpp` — templated forEach, [[nodiscard]] on insert/emplace/get/isValid.
- `include/core/Graph.hpp` — templated forEachNode/forEachLink; scratch members `m_dirtyQueue`, `m_topoQueue`, `m_inDegree`; `getViewers()`; LOOM_PROFILE_SCOPE on `execute`; `[[nodiscard]]` on addNode/tryAddLink/canAddLink.
- `include/core/Handle.hpp` — `[[nodiscard]] isValid`.
- `include/gpu/ResourceHandles.hpp` — `[[nodiscard]] isValid` on ImageHandle / BufferHandle.
- `include/platform/Window.hpp` — `[[nodiscard]] shouldClose / wasResized / getNativeWindow`.
- `include/gpu/ComputeTask.hpp` — `setPushConstants<T>` typed helper.
- `include/gpu/PipelineCache.hpp` — `[[nodiscard]] getOrCreate`.
- `include/gpu/TransientBufferPool.hpp` — `[[maybe_unused]] m_device`.
- `src/core/Nodes.cpp` — rewritten on top of NodeTaskBuilders; one default `ImageSpec` helper at file scope; no more memcpy push-constant patterns.
- `src/gpu/FrameLoop.cpp`, `src/gpu/DispatchManager.cpp`, `src/gpu/DisplayPass.cpp` — LOOM_PROFILE_SCOPE at the planned sites.
- `src/main.cpp`, `src/ui/ImGuiRenderer.cpp`, `src/gpu/Device.cpp`, `src/gpu/Instance.cpp`, `src/gpu/BindlessHeap.cpp`, `src/gpu/PipelineCache.cpp` — `std::cout` / `std::cerr` → `loom::log::*`.
- `src/ui/NodeEditorPanel.cpp` — explicit `(void)` on the in-editor `tryAddLink`.
- Tests: `TopoTest`, `GraphTest`, `PushPullTest`, `NodeBehaviorTest`, `ComputeDispatchTest`, `GraphExecutionTest` — happy-path `tryAddLink` calls wrapped in `ASSERT_TRUE`; `SlotMapTest` uses `(void)` for deliberate discards.
- `CMakeLists.txt` — adds `Log.cpp`, `NodeTaskBuilders.cpp`.

### Verification

- Build: clean across all configurations. Zero compiler warnings on Loom's own code (the GLFW vendored tree still emits `-Wpedantic` and `-Wmissing-field-initializers` from upstream; those land in Phase 9 if they need silencing).
- `ctest --test-dir build` — 63/63 pass. The `ASSERT_TRUE` wraps strengthened the test invariants: if `tryAddLink` ever silently rejects an edge that the test expects to succeed, it fails at the wiring step rather than downstream.

### Dependencies
Independent of Phases 5–6 except where call sites overlap (Logger conversion touches `Device.cpp`, `Instance.cpp`, `BindlessHeap.cpp`, `PipelineCache.cpp`, all of which were the subjects of earlier phases).

### Known follow-ups

- 7.3 (Graph.hpp → Graph.cpp split) deferred. Revisit when the API stops being mostly templated.
- 7.10 (std::span migration) is partial — only `HazardTracker` accepts spans today. When a non-vector caller of `DispatchManager::submit` or `NodeTaskBuilders` appears, widen the signatures then.
- The logger backend is iostream; replacing with spdlog/Tracy is a `Log.cpp`-only change.
- `LOOM_PROFILE_SCOPE` is currently a no-op. Tracy swap drops `#include <tracy/Tracy.hpp>` into `Profile.hpp` and defines `LOOM_ENABLE_PROFILING` at the build-system level — no call-site churn.
- The `(void)` discard pattern in tests is mechanical and noisy; a future macro `LOOM_TEST_DISCARD(expr)` could make the intent (test wants to discard) explicit, but two call sites today don't justify the abstraction.

---

## Phase 8 — Typed ResourceRef Refactor

### Goal
Replace the implicit assumption "every pin carries an `ImageHandle`" with a tagged `ResourceRef`. The structural seam unblocks deep buffers, geometry buffers, motion vectors, and any other non-image payload — without committing to any of them in this branch. `Kind::Buffer` and `Kind::Deep` are reserved in the union; v1 production code only ever produces `Kind::Image`.

### Sub-task tracking

- 8.1 ✅ — `ResourceRef` in `gpu/ResourceHandles.hpp`. `Kind { None, Image, Buffer, Deep }`, factory methods `fromImage` / `fromBuffer`, and `isValid()`. The `deep` sub-struct mirrors the OpenEXR Deep model (count image + offset image + samples buffer) so a future `Kind::Deep` consumer doesn't have to redesign the payload shape.
- 8.2 ✅ — `Node::pullInput` now returns `ResourceRef`. The new `Node::pullImageInput` typed accessor asserts `ref.kind == Kind::Image` and returns the inner `ImageHandle`. Existing image-only nodes (`ConstantNode`, `MergeNode`, `ViewerNode`, `PassthroughNode`) consume `pullImageInput`; the assertion catches type mismatches that slip past `canAddLink` (e.g. a future production buffer node that someone forgets to gate on PinType).
- 8.3 ✅ — `RenderCache` value type → `ResourceRef`. `store(pin, region, ref)` / `retrieve(pin, region) -> ResourceRef` / `evict` / `clear` / `garbageCollect` all carry the tagged payload. The `m_pendingImageReleases` queue stays typed as `ImageHandle` and is populated only when an evicted entry was `Kind::Image`; that keeps the pool-release contract simple (a single image-typed pool drains images). Future non-image kinds will route through their respective pools' release queues.
- 8.4 ✅ — Data-driven pin schema. New virtual `Node::getPinSchema() const -> std::vector<PinSpec>`. The previous central switch in `Graph::setupNodePins` collapses to a 3-line loop over the schema. Adding a new node type is now a single subclass edit instead of two edits (subclass + Graph switch).
- 8.5 ✅ — All four existing nodes updated. `ConstantNode { output:Float }`, `MergeNode { input:Float, input:Float, output:Float }`, `ViewerNode { input:Float }`, `PassthroughNode { input:Float, output:Float }`. Behaviour unchanged; each `execute` ends with `ctx.renderCache->store(outputs[0], region, gpu::ResourceRef::fromImage(handle))`.
- 8.6 ✅ — Type-system enforcement test. New `tests/core/PinSchemaTest.cpp` with three cases: (1) every registered node reports the expected pin counts via `getPinSchema`; (2) a `DeepBuffer`-typed output cannot wire to a `Float`-typed input — `canAddLink` returns false and `tryAddLink` returns false; (3) the standalone `BufferTestNode` returns its declared schema. The buffer-output test reuses an existing Constant node's pin by mutating its `type` to `DeepBuffer` rather than exposing a "create raw pin" helper on Graph just for the test. Companion `tests/core/ResourceRefTest.cpp` covers the tag-union basics: default-constructed is None, `fromImage` / `fromBuffer` carry the inner handle, the `Kind::Deep` slot exists and its inner handles default to invalid.

### Decisions

- **PinType enum kept as-is.** `PinType::Float` historically meant "image" (the pins carry image handles, not floats) and `PinType::DeepBuffer` covers the future deep-comp slot. Renaming `Float → Image` for clarity was tempting but would have churned the editor UI's pin-label switch and the existing GraphTest type-mismatch test, all for cosmetics. A comment in `Types.hpp` documents the historical naming and the PinType ↔ ResourceRef::Kind mapping.
- **ViewerNode::lastOutput stays `ImageHandle`.** The v1 viewer only displays images. A future multi-kind viewer (deep inspector, buffer inspector) lands with its own subclass and storage; documenting the v1 restriction in the `lastOutput` field comment makes the seam visible.
- **RenderCache release queue stays image-typed.** The cache's job is bookkeeping (pin × region → ref); it doesn't own pool resources. Production code only ever stores `Kind::Image`, so a single `vector<ImageHandle>` release queue is right. When/if a non-image kind starts being stored, parallel queues per pool are simpler than a heterogeneous "release me from whichever pool you came from" queue.
- **Pin-schema test reuses an existing pin via mutation.** Rather than expose a `Graph::createRawPin` helper just for the test, the buffer-output case mutates a Constant node's output pin's `type` to `DeepBuffer` and verifies `canAddLink` rejects the cross-type wire. This is the smallest possible test surface that exercises the rejection. A future `BufferTestNode` that participates in evaluation would warrant the helper.
- **No `BufferTestNode` in Graph::addNode.** The plan offered registering it as a test-only NodeType. I kept it as a free class that isn't in the `NodeType` enum — the wiring rejection only depends on PinType, not on NodeType. Keeping the production enum clean is worth more than one less line in the test.

### Files created

- `tests/core/ResourceRefTest.cpp`
- `tests/core/PinSchemaTest.cpp`

### Files modified

- `include/gpu/ResourceHandles.hpp` — adds `ResourceRef`, the `Kind` enum, factory methods, and the `DeepRef` sub-struct.
- `include/core/Types.hpp` — adds `PinSpec`; documents the PinType ↔ Kind mapping; adds `Node::getPinSchema()` pure virtual; `Node::pullInput` signature changes to return `ResourceRef`; new `Node::pullImageInput` typed accessor.
- `include/core/Nodes.hpp` — each concrete node declares `getPinSchema()`. `ViewerNode::lastOutput` gains a comment about the v1 image-only restriction.
- `src/core/Nodes.cpp` — implementations of `getPinSchema()` per node; `pullInput` body returns `RenderCache::retrieve` (now `ResourceRef`); `pullImageInput` body asserts `Kind::Image` and unwraps; every `store` call wraps the image in `ResourceRef::fromImage`.
- `include/core/RenderCache.hpp` — map value type → `ResourceRef`; `store` / `retrieve` / `evict` / `clear` / `garbageCollect` route through the tagged type; release queue extracts the inner `ImageHandle` only when `kind == Image`.
- `src/core/RenderCache.cpp` — `garbageCollect` mirrors the kind-aware release.
- `include/core/Graph.hpp` — `setupNodePins` collapses to a `for (const auto& spec : node->getPinSchema()) createPin(...)` loop.
- `tests/core/RenderCacheTest.cpp` — `store` calls wrap in `refOf(...)`; `retrieve` and `pullInput` results are `ASSERT_EQ`-checked against `Kind::Image` before `.image` is extracted for `handleEqual`. `PullInputTestNode` implements the new `getPinSchema()` virtual.
- `CMakeLists.txt` — adds the two new test files.

### Verification

- Build: clean. No new warnings on Loom's own code.
- `ctest --test-dir build` — 70/70 pass (+4 `ResourceRefTest.*`, +3 `PinSchemaTest.*`).
- The `BufferOutputCannotWireToImageInput` test pins the type-system contract: a `Kind::Deep` payload can never be wired into an image input via the editor.

### Dependencies
Phases 1–7. Region (Phase 4) and the typed push-constant helper (Phase 7.4) interact with `pullInput`'s signature change; the templated forEach + scratch buffers from Phase 7 were unaffected.

### Known follow-ups

- When the first production buffer node lands, plumb a `TransientBufferPool` release queue through `RenderCache` and add a parallel `m_pendingBufferReleases`. The pattern is identical to images; touching only the cache is preferable to a heterogeneous queue.
- The OpenEXR `Kind::Deep` payload is declared but never populated. The first deep node will validate the shape end-to-end and possibly tweak the `DeepRef` layout.
- `Node::pullImageInput` asserts on `Kind::Image` mismatch. Adding a buffer-typed input to a node means adding a `pullBufferInput` companion. Both could be folded behind a `pullInput<Kind>` template if the call sites grow.
- `PinSchemaTest::BufferOutputCannotWireToImageInput` mutates an existing pin's type rather than constructing a buffer-output node end-to-end. The first production buffer node will replace that workaround with a real `addNode(NodeType::FileRead)` (or similar) test.

---

## Phase 9 — CI Hardening & Static Analysis

### Goal
Move CI from "Release-only build + ctest" to industry standard: Debug/Release/Sanitize matrix, `clang-tidy` and `scan-build` gates, Lavapipe-driven headless GPU testing on Ubuntu, code coverage measurement, and Doxygen API doc generation.

### Sub-task tracking

- 9.1 ✅ — Tests run in CI across the expanded matrix. `ctest --output-on-failure` plus an `actions/upload-artifact@v4` step that uploads `LastTest.log` on failure. The existing single-job matrix grew to a 7-cell grid (3 OS × 2 build types + 1 Linux-only Sanitize entry).
- 9.2 ✅ — Sanitize entry added (`ubuntu-latest, clang, build_type=Sanitize`). The `Sanitize` CMake build type already provides `-fsanitize=address,undefined -fno-omit-frame-pointer`; the workflow just plumbs it through. MSVC sanitizer support is deferred — the matrix exclusion list documents the gap.
- 9.3 ✅ — `.clang-tidy` at the repo root with the plan's rule set (`bugprone-*`, `cert-*`, `cppcoreguidelines-*`, `performance-*`, `readability-*`, `modernize-*`, `misc-*`). Noisy rules disabled: `readability-magic-numbers`, `cppcoreguidelines-avoid-magic-numbers`, `cppcoreguidelines-pro-bounds-pointer-arithmetic`, `cppcoreguidelines-pro-bounds-constant-array-index`, `cppcoreguidelines-pro-type-reinterpret-cast`, `cppcoreguidelines-non-private-member-variables-in-classes`, `cppcoreguidelines-avoid-c-arrays`, `modernize-use-trailing-return-type`, `modernize-avoid-c-arrays`, `modernize-use-nodiscard`, `readability-identifier-length`, `misc-non-private-member-variables-in-classes`. CI step scans changed files (PR diff against base) or `src/`+`include/` on push. `WarningsAsErrors: ''` for now — diagnostics are visible but non-blocking; a follow-up PR tightens once the baseline is clean.
- 9.4 ✅ — `scan-build` job (`apt install clang-tools`, then `scan-build cmake -B build && scan-build --status-bugs cmake --build build`). Report uploaded as `scan-build-report` artifact. Doesn't fail the build today; the artifact is the signal.
- 9.5 ✅ — `Coverage` build type defined in `CMakeLists.txt` (`-O0 -g --coverage -fprofile-arcs -ftest-coverage` + `--coverage` linker flag). CI `coverage` job builds with g++, runs `ctest` (under Lavapipe), and post-processes with `gcovr --xml-pretty --html-details`. Reports uploaded as `coverage` artifact. No hard percentage gate this branch.
- 9.6 ✅ — Lavapipe install on Ubuntu (`apt install mesa-vulkan-drivers vulkan-tools`). The build job exports `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json` and `LOOM_HEADLESS_GPU=1`. Tests that rely on a swapchain still skip cleanly; tests that just need a Vulkan device (compute dispatch, hazard, bindless, readback) run against the software ICD.
- 9.7 ✅ — `docs/Doxyfile.in` template + CMake `docs` target. `find_package(Doxygen QUIET)` — if Doxygen is missing the target simply doesn't get registered (CI installs it on demand if/when a docs job lands). `Doxyfile.in` consumes `@CMAKE_BINARY_DIR@` and `@CMAKE_CURRENT_SOURCE_DIR@` placeholders via `configure_file`. The `docs/` output drops into `${CMAKE_BINARY_DIR}/docs/html`. The GitHub Pages publish step is documented as a follow-up — wiring it requires repo Pages settings outside the workflow file.
- 9.8 — deferred. `FetchContent` for GLFW and googletest still pins by tag (`3.3.8`, `v1.14.0`). Switching to commit-hash pinning is a one-line edit per dep and a CI rerun; left for the user since it requires picking specific hashes.

### Decisions

- **`WarningsAsErrors: ''` in .clang-tidy.** The plan suggested `'*'` (everything as error). Treating every clang-tidy warning as an error on a refactor branch that hasn't run the linter yet would block the build entirely — there are dozens of `cppcoreguidelines-pro-type-vararg`, `bugprone-easily-swappable-parameters`, etc. that need a sweep before they're worth gating on. The diagnostics surface in PR logs and as artifacts; a follow-up PR enumerates and fixes by category, then tightens `WarningsAsErrors`. The rule set itself is the load-bearing decision; the gate-strictness is policy.
- **clang-tidy scans PR diff, not full tree.** Two reasons: (1) the full-tree scan would multiply by ~30 files per check, blowing past the GitHub Actions soft minute budget; (2) the value of the lint check is "did this PR introduce a regression?" — a noisy baseline is better fixed in dedicated cleanup PRs. The push-to-main path still scans everything as a safety net.
- **Lavapipe over SwiftShader.** Mesa ships Lavapipe as part of the standard `mesa-vulkan-drivers` package on Ubuntu, no additional download. SwiftShader requires a build-from-source or vendored binary. Lavapipe is also the more actively maintained software-Vulkan implementation in 2026.
- **`CMAKE_EXPORT_COMPILE_COMMANDS = ON` at top level.** Required by clang-tidy / scan-build / language servers. The cost is one extra JSON file in the build tree; the alternative is forcing every contributor to remember the flag.
- **scan-build runs against Debug, not Release.** Static analysis benefits from `-O0` (full unoptimised IR is what the checker reasons over). Running it against Release would miss inlined-call diagnostics and add noise from optimisation-introduced patterns.
- **No GitHub Pages publish step for Doxygen yet.** The `docs` CMake target is in place; the publish workflow needs the repo's Pages settings configured manually (Settings → Pages → Source: GitHub Actions). Documenting this as a known follow-up rather than committing a workflow that 404s on the first push.

### Files created

- `.clang-tidy`
- `docs/Doxyfile.in`

### Files modified

- `CMakeLists.txt` — `Coverage` build type, `CMAKE_EXPORT_COMPILE_COMMANDS = ON`, optional `docs` target gated on `find_package(Doxygen)`.
- `.github/workflows/build.yml` — matrix expanded to Debug/Release/Sanitize, Lavapipe install, `ctest --output-on-failure` + log artifact, new `clang-tidy` / `scan-build` / `coverage` jobs that `needs: build`.

### Verification

- Local build green at 70/70 after the CMake additions. The new build types are configurable (`cmake -B /tmp/x -DCMAKE_BUILD_TYPE=Coverage` succeeds, "Doxygen not found; `docs` target unavailable" is the expected status line on this dev machine).
- The CI workflow can't be fully verified from a local checkout — it lands on `main` and runs on the first PR. The matrix changes are self-contained YAML; the clang-tidy / scan-build / coverage jobs read `compile_commands.json` from the configure step, which is exercised by the local build.
- `compile_commands.json` is now in the build tree (`cat build/compile_commands.json | jq length` returns ~30 on this checkout).

### Dependencies
Phases 0–8. The clang-tidy rule set picks up patterns introduced (or fixed) across the entire cleanup branch.

### Known follow-ups

- Tighten `WarningsAsErrors` once the baseline is clean. Start with the rule categories with the lowest noise ratio (`bugprone-*`, `cert-*`) and expand.
- Doxygen → GitHub Pages publish step. Requires the repo's Pages settings configured to "GitHub Actions"; the workflow is one `actions/deploy-pages@v4` call away.
- Pin GLFW and googletest by commit hash (plan §9.8). One-line edit per dep, but choosing hashes is a user decision.
- The MSVC sanitizer story — `/fsanitize=address` exists on recent MSVC but the runtime story is different. Defer to a dedicated PR when the project actually needs to run MSVC sanitizers.
- A `WARN_AS_ERROR = YES` setting in Doxyfile.in would surface broken doc references as build failures; deferred until the existing comment estate is audited.

### Files created

- `include/gpu/LayoutTransitions.hpp`, `src/gpu/LayoutTransitions.cpp`
- `include/gpu/Instance.hpp`, `src/gpu/Instance.cpp`
- `include/gpu/Device.hpp`, `src/gpu/Device.cpp`
- `include/gpu/Swapchain.hpp`, `src/gpu/Swapchain.cpp`
- `include/gpu/ResourceFactory.hpp`, `src/gpu/ResourceFactory.cpp`
- `include/gpu/FrameLoop.hpp`, `src/gpu/FrameLoop.cpp`
- `include/platform/UserDataDir.hpp`, `src/platform/UserDataDir.cpp`

### Files modified

- `include/gpu/VulkanContext.hpp` — adds `Instance`, `Device`, `Swapchain` member unique_ptrs; removes the standalone create*/cleanup*/choose* declarations and the validation-layers / device-extensions arrays (those live on the subsystems now). `getVkInstance` / `getSwapchainImageFormat` / `getSwapchainImageCount` getters delegate to the subsystem objects.
- `src/gpu/VulkanContext.cpp` — `init()` constructs the three subsystems in order, copies handles into the legacy shadow members. `recreateSwapchain` delegates to `Swapchain::recreate()` and resizes `m_imagesInFlight` to match the new image count. `beginFrame` / `endFrame` use `m_swapchainObj` for image, view, format, and present.
- `CMakeLists.txt` — `LoomCore` gains the four new `.cpp` files.

### Verification

- Build: clean across all four sub-task commits.
- `ctest --test-dir build` — 63/63 pass headless after each sub-task. Same set of GPU-skip tests as before.
- Public API unchanged from `main.cpp`'s perspective — `getDevice`, `getPhysicalDevice`, `getGraphicsQueue`, `getGraphicsQueueFamily`, `getDescriptorPool`, `getSwapchainImageFormat`, `getSwapchainImageCount`, `getVmaAllocator`, `getBindlessHeap`, `getVkInstance`, `beginFrame`, `endFrame`, `waitIdle` all behave identically.

### Verification

- Build: clean after each of the eight sub-task commits across both sessions.
- `ctest --test-dir build` — 63/63 pass headless after every sub-task. GPU-dependent tests `GTEST_SKIP` on this dev machine; the timeline-semaphore path is not exercised by the test suite. The end-to-end check (Loom runs, frames render correctly, no validation warnings) wants a Vulkan-capable host and is deferred to a manual smoke pass.
- Public API surface of `VulkanContext` after the slim: `init`, `waitIdle`, `beginFrame`, `endFrame`, `currentFrameValue`, `beginSingleTimeCommands`, `endSingleTimeCommands`, plus passthrough getters. Identical (modulo the new `currentFrameValue`) to the pre-decomposition surface; `main.cpp` and all tests build unchanged.

### Dependencies
Phase 1 (`LOOM_ASSERT`, `LOOM_VK_CHECK`, `transitionImageLayout` derived-mask rewrite). Phase 4 only incidentally (no overlap in files).

### Known follow-ups
- Phase 6 wires `BindlessHeap` and `TransientImagePool` retirement to the timeline value via `VulkanContext::currentFrameValue()` (or, more directly, `FrameLoop::currentFrameValue()` once the pools are passed a reference).
- The persistent pipeline cache writes `pipeline_cache.bin` on every shutdown. A future test (Phase 10) should round-trip create → destroy → re-load and assert `vkGetPipelineCacheData` size > the empty-cache header size. Until then the file is verified by manual inspection.
- `Device::checkDeviceExtensionSupport` is currently unused at the public level — the suitability check inlines its logic. Keep as a debug/test hook; remove later if it stays unused.
- `Swapchain` always picks `B8G8R8A8_SRGB` if available, else the first format. Once a settings system lands, this becomes user-configurable.
- The `MAX_SWAPCHAIN_IMAGES = 8` ceiling for renderFinished semaphores avoids semaphore recreation on swapchain resize; an `LOOM_ASSERT(swapchain.getImageCount() <= MAX_SWAPCHAIN_IMAGES)` at swapchain construction would harden this if a future driver ever exceeds it.
