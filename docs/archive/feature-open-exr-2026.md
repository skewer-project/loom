# Loom — Deep EXR / NVS Feature Branch Log (Phase A)

This file is the per-change dev archive for the `feature/open-exr` branch. It
captures **why** decisions were made, **what** changed at each sub-task, and
**how** to verify. Parallel to `refactor-cleanup-2026.md` (which logged the
11-phase cleanup that produced the professional baseline this work builds on)
and `build-out-phases-1-6.md` (the original implementation log).

The plan being executed lives at `~/.claude/plans/draft-a-plan-roadmap-for-jazzy-flask.md`
(private — not committed to the repo). Phase A is the foundational layer that
unblocks the deep-EXR viewer (Phase B), animation + compositing (Phase C), and
novel-view synthesis (Phase D).

---

## Baseline (before any Phase A changes)

- Branch: `feature/open-exr`, forked from `main` at `98e107c` (the tail of the
  11-phase cleanup).
- Tests: 73/73 passing locally (52 ran, 21 GPU-bound tests `GTEST_SKIP`-ed
  cleanly on this no-device machine).
- Loom is fully standalone — no sibling-repo coupling. Dependencies arrive
  exclusively via `FetchContent` (GLFW, GoogleTest) or are vendored under
  `external/` (ImGui, imgui-node-editor, VMA).
- Carry-forward invariants in force throughout Phase A: bindless heap, timeline
  semaphores, region-keyed `RenderCache`, `HazardTracker` RAW+WAW (WAR
  scaffolded), persistent `VkPipelineCache`, `LOOM_ASSERT` / `LOOM_VK_CHECK` /
  `loom::log::*`, headless `core/` cannot include Vulkan headers.

---

## Phase A.0 — OpenEXR integration + load spike

### Goal

Add OpenEXR + Imath as transitive `FetchContent` dependencies so Loom can read
and write deep EXR files standalone from a fresh checkout. Prove the integration
with a throwaway command-line spike that prints channel-aware metadata
(width / height, channel names + types, total samples, average samples per
pixel, depth range). Commit a deterministic deep EXR fixture under
`tests/data/` that Phase A.1+ unit tests will consume.

### Decisions

- **OpenEXR via `FetchContent`, pinned to `v3.2.4`. Imath pinned to `v3.1.12`.**
  Both libraries are first-party Academy Software Foundation projects, share a
  release cadence, and have stable CMake export targets (`OpenEXR::OpenEXR`,
  `Imath::Imath`). Pinning to exact tags makes the build reproducible across
  contributor machines; bumping the tag is a one-line change. The plan
  explicitly anticipated a vendored fallback under `external/openexr/` if
  `FetchContent` proved fragile — on this machine the first-configure took
  ~6 seconds, so we stay with `FetchContent`. Switch is a future change.
- **OpenEXR's tools / examples / Python / install / tests are all disabled.**
  Each is a `set(OPENEXR_BUILD_X OFF CACHE BOOL "" FORCE)` before
  `FetchContent_MakeAvailable`. Loom only needs the `OpenEXR::OpenEXR` library
  target; everything else is configure-time blast radius for no benefit.
  Same posture for Imath (`IMATH_INSTALL OFF`, `BUILD_TESTING OFF`).
- **Tools live outside `LoomCore`.** `tools/exr_spike.cpp` and
  `tools/make_deep_fixture.cpp` are explicitly throwaway — they link OpenEXR
  directly and do not depend on engine code. Reasoning: the engine-side wrapper
  (`IDeepReader` in Phase B.1) carries the long-term API. The spike's only job
  is to confirm OpenEXR Deep parses cleanly inside Loom's build environment;
  the fixture-maker's only job is to produce the committed `.exr` so the file
  is reproducible from source. Both are exec targets with no headers; neither
  is linked from anywhere else.
- **Fixture is committed, not generated at test time.** A 16×16 deep EXR with
  255 samples weighs ~4 KB on disk — well below the 100 KB threshold where
  Git LFS would start being a consideration. Generating the file at test time
  would couple OpenEXR's writer into the test binary; committing the bytes
  lets future tests consume the fixture without re-linking OpenEXR writers.
  Regenerate-on-demand stays available via the `make_deep_fixture` target.
- **Fixture channel set is the v1 spec from the plan.** `Z` + `ZBack`
  (`FLOAT`), `R` / `G` / `B` / `A` (`HALF`). This is the deep-EXR core that
  every producer (Skewer and any future tool) is contractually required to
  emit per `docs/CONVENTIONS.md` §19. NVS-extension fixtures (`world_pos.*` /
  `normal.*` / `albedo.*` channels) land alongside Phase D.0.
- **Two-pass deep read in `exr_spike`.** OpenEXR's `DeepScanLineInputFile`
  requires `setFrameBuffer` before each `readPixelSampleCounts` /
  `readPixels` pair. Re-setting the framebuffer between passes drops the
  file's internal sample-count cache, so the second pass must call
  `readPixelSampleCounts` again before `readPixels`. Documented inline; this
  trap will reappear when the Phase B.1 reader is written.
- **`LOOM_FIXTURE_DIR` resolves at compile time to the source-tree fixture
  path.** This keeps the spike runnable with zero arguments from any working
  directory and removes the temptation to chase the build-output directory
  with relative paths.

### Files created

| Path | Purpose |
|------|---------|
| `tools/exr_spike.cpp` | Throwaway deep-EXR inspector (Phase A.0 only) |
| `tools/make_deep_fixture.cpp` | Throwaway fixture generator (Phase A.0 only) |
| `tests/data/deep_smoke.exr` | 16×16 deep EXR fixture, six v1-spec channels, 255 samples |
| `docs/archive/feature-open-exr-2026.md` | This file |

### Files modified

- `CMakeLists.txt` — `FetchContent` declarations for `Imath` and `OpenEXR`
  with tools/examples/install disabled; two new `add_executable` targets
  (`exr_spike`, `make_deep_fixture`) linked against `OpenEXR::OpenEXR` /
  `Imath::Imath`; `LOOM_FIXTURE_DIR` define carries the source-tree fixture
  path into each tool.

### Verification

- Configure: `cmake --preset debug` — clean (`Configuring done (5.9s)`); first
  time on this machine, OpenEXR + Imath sub-CMake configures end-to-end with
  no warnings.
- Build: `cmake --build build/debug -j4` — clean. OpenEXR builds as a static
  library (`libOpenEXR-3_2_d.a`); the one duplicate-library linker warning
  (`Imath-3_1_d.a` referenced twice) is upstream noise from OpenEXR's CMake
  re-listing Imath as an explicit dependency. Not a Loom-side bug.
- Fixture regeneration: `./build/debug/bin/make_deep_fixture` →
  `wrote tests/data/deep_smoke.exr (16x16, 255 samples)`.
- Spike: `./build/debug/bin/exr_spike` →
  ```
  file: tests/data/deep_smoke.exr
    width  = 16
    height = 16
    channels:
      A : half
      B : half
      G : half
      R : half
      Z : float
      ZBack : float
    total samples = 255
    avg samples/px = 0.996
    Z range = [1, 3.5]
  ```
  Z range matches the fixture's design (`Z` runs 1.0..3.0, `ZBack` =
  `Z + 0.5`, so the joint range maxes at 3.5).
- Tests: `ctest --preset debug` — 73/73 pass (no regressions from the
  OpenEXR pull-in).

### Coordinate convention

The fixture is generated in the right-handed, Y-up, meters convention
documented in `docs/CONVENTIONS.md` §19 (added in A.6). No coordinate-system
attribute is written into the EXR header; loaders default to the convention
unless an explicit `loom/coordSystem` string attribute overrides it. Any
producer emitting deep EXRs from a left-handed or Z-up engine attaches that
attribute, and Loom applies the transform at load time. This contract is part
of the v1 channel-naming spec.

### Dependencies

None within Phase A. Unblocks A.1 (the `DeepLayout` will consume what the
spike inspects), A.4 (the staging upload path will be exercised by the deep
fixture).

### Known follow-ups for later sub-phases

- The two-pass read pattern in `exr_spike.cpp` will become a shared helper
  inside the Phase B.1 `IDeepReader` implementation. The spike's body is
  intentionally inlined so the trap is visible to anyone reading it; the
  engine wrapper hides it behind a single `readFrame` call.
- The fixture currently only carries the v1 channel set. The NVS-extension
  variant (with `world_pos.*` / `normal.*` / `albedo.*` channels) is a
  separate fixture, added alongside Phase D.0 (`tests/data/nvs_cornell_box.exr`).
- The duplicate-library linker warning is upstream and benign; if it ever
  starts triggering CI noise we can patch our `target_link_libraries(exr_spike
  …)` line to explicitly list `OpenEXR::OpenEXR` only and let CMake resolve
  Imath transitively.
- `OpenEXR_BUILD_TESTING` is OFF, but the umbrella `BUILD_TESTING` variable
  also gates Imath's own tests. If a Loom contributor sets `BUILD_TESTING=ON`
  via a parent project, Imath's tests will be re-enabled. Acceptable for
  v1 — the Loom build always sets it OFF in the relevant scope.

---

## Phase A.1 — `core::DeepLayout` + `ResourceRef::DeepRef` extension

### Goal

Introduce the interned, hashable, ordered channel-list abstraction that every
downstream Phase A / B / D component will key on. Extend `ResourceRef::DeepRef`
with a non-owning `const DeepLayout*` so pin payloads carry their channel
schema alongside the GPU resources.

### Decisions

- **`DeepChannel { name, type, components }`.** Plan-of-record; matches the
  shape called out in the roadmap. `type` is one of `Float16` / `Float32` /
  `UInt32` (Phase A.0's spike already established that the three OpenEXR
  pixel types cover the whole reachable surface area). `components` is the
  "vector channel" coalescer — `world_pos` is 1 channel with `components=3`
  even though OpenEXR's deep API hands it back as three scalar channels
  (`world_pos.x|y|z`). Coalescing is the loader's responsibility (Phase
  B.1); the layout structure is agnostic.
- **Packing is tight, no inter-channel padding.** Documented in the header.
  V1 makes no alignment guarantees beyond natural alignment of each scalar
  element. The shader-side SSBO load tolerates 2-byte alignment for 4-byte
  values on every desktop GPU we target. If a future tile-streaming
  evaluator needs 16-byte stride alignment we can introduce automatic
  padding then — at that point `DeepLayout` becomes responsible for
  inserting the pad channels, but the wire format from `DeepLayoutRegistry`
  stays content-addressable.
- **Process-wide content-addressed registry, single-threaded.** The plan
  called for "process-wide `DeepLayoutRegistry`: hash → canonical pointer.
  Layouts compare by pointer equality." Implemented as a static bucketed
  map (`hash → vector<unique_ptr<DeepLayout>>`) so identical-content
  layouts collapse to one allocation and pointer equality is always sound.
  CONVENTIONS §13 says the engine is single-threaded and warns against
  speculative locking; the registry follows that. The Phase C.1 worker
  thread parses EXR data off the main thread but hands the channel list
  back to the main thread for interning — no concurrent registry access.
- **Hash includes channel order.** `boost::hash_combine` pattern with the
  golden-ratio constant. Different orderings of the same channel set yield
  different layouts (pinned by `DifferentChannelOrderInternsDistinctly`).
  This matters because byte offsets depend on order: a writer that emits
  channels in a different sequence produces a layout that consumers must
  treat as distinct.
- **Bucket fallback on hash collision.** The registry walks the bucket and
  compares channels element-wise on a hash match. Pathological hash
  collisions still yield correct pointer-equality semantics, at the cost
  of an O(b) walk where b is bucket size. At realistic scales (a handful
  of distinct layouts per session) this never matters.
- **`DEBUG_clearLayoutRegistry()` for tests.** The registry is static and
  persists across `TEST` invocations in the same process; the Phase A.1
  tests need a clean slate per case to assert interning counts. The
  `DEBUG_` prefix is the existing project convention for diagnostic-only
  accessors (matches `TransientImagePool::DEBUG_getFreeSlotCount`).
- **`ResourceRef::DeepRef::layout` is a non-owning, nullable pointer.**
  The plan called for the layout pointer to live alongside `countImage` /
  `offsetImage` / `samples`. Nullable because a default-constructed
  `DeepRef` exists as a placeholder pin payload before evaluation runs;
  production-path consumers must check before dereferencing. Pointer
  equality between two refs' `layout` fields means they share the same
  channel schema (and therefore the same per-channel byte offsets).
- **Forward-declare `core::DeepLayout` in `ResourceHandles.hpp`.** The
  header is included from both `core/` and `gpu/`; pulling in
  `core/DeepLayout.hpp` here would invert the dependency direction.
  Forward declaration keeps the include graph clean — consumers that
  actually walk the layout include `core/DeepLayout.hpp` directly.

### Files created

| Path | Purpose |
|------|---------|
| `include/core/DeepLayout.hpp` | `DeepChannel`, `ChannelType`, `DeepLayout`, `getDeepLayout`, `DEBUG_*` |
| `src/core/DeepLayout.cpp` | Implementation + process-wide registry |
| `tests/core/DeepLayoutTest.cpp` | 7 cases covering offsets, stride, interning, hash, unknown-channel tolerance, empty layout |

### Files modified

- `include/gpu/ResourceHandles.hpp` — `DeepRef` gains `const core::DeepLayout*
  layout = nullptr`. Forward-declares `core::DeepLayout`. Adds
  `ResourceRef::fromDeep(DeepRef)` for symmetry with `fromImage` / `fromBuffer`.
- `tests/core/ResourceRefTest.cpp` — `DeepSlotReservedButZeroByDefault` extended
  to assert `layout == nullptr` by default. New `FromDeepCarriesPayload` case.
- `CMakeLists.txt` — `LoomCore` adds `src/core/DeepLayout.cpp`; `LoomTests`
  adds `tests/core/DeepLayoutTest.cpp`.

### Verification

- Build: clean.
- `ctest --preset debug` — 81/81 pass (+7 `DeepLayoutFixture.*` cases, +1
  `ResourceRefTest.FromDeepCarriesPayload`). Baseline was 73; net +8.
- Manual review: `ResourceHandles.hpp` no longer pulls any `core/` content
  in the absence of `DeepLayout` access (forward declaration only).

### Dependencies

A.0 (channel-type vocabulary established in the spike).

### Known follow-ups for later sub-phases

- Phase A.4's `StagingArena::uploadDeepImage` is the first non-test caller
  of `getDeepLayout`. It assembles the candidate channel list from the
  parsed EXR header and hands it to the registry; the returned pointer
  becomes `DeepRef::layout`.
- Phase B.1's `IDeepReader` populates the layout's `components` field via
  channel-name pattern matching (`world_pos.x` / `.y` / `.z` coalesce into
  one `world_pos` entry with `components=3`). The pattern matcher is a
  small helper inside the reader — `DeepLayout` itself does not know about
  the dot-separated convention.
- If we ever need cross-thread access to the registry, gate it on a single
  `std::mutex` inside `Registry`. Cost is one branch per `getDeepLayout`
  call — negligible.

---

## Phase A.2 — `core::Camera` + `EvaluationContext` threading

### Goal

Add the right-handed Y-up perspective camera that Phase B.5's `PointCloudPass`
and Phase D.2's splat shader consume, and thread it through
`EvaluationContext` so nodes can reach it during evaluation. Also wire the
scene-time `frame` index that Phase C's animation playback needs.

### Decisions

- **glm via FetchContent, pinned to 1.0.1.** The roadmap calls for
  `glm::vec3` in `Param` and the camera class is half-a-dozen lines without
  it. Pulling glm in is a one-line `FetchContent_Declare` — header-only,
  template-only, zero-cost for translation units that don't include it.
  `LoomCore`'s public link list now carries `glm::glm` so headers in
  `include/core/` and `include/gpu/` can refer to glm types without
  per-target boilerplate.
- **RH/Y-up/meters, Vulkan clip-space.** Documented in `Camera.hpp`'s class
  comment and pinned in §19. The choice matches Vulkan / glTF / Blender;
  Loom converts at the loader boundary for files declaring a different
  convention.
- **Y-flip lives in the projection, not in the viewport.** Vulkan's clip
  space has +Y down; negating row 1, column 1 of the projection matrix
  pre-flips world +Y so it lands at clip -Y. The alternative — a negative-
  height `VkViewport` — has historically caused inconsistent
  winding-order semantics across drivers (front-face determination flips
  silently). The projection-side flip is portable and explicit, and is
  pinned by `CameraTest.ProjectionFlipsYForVulkanClipSpace`.
- **`perspectiveRH_ZO` for the projection.** Vulkan NDC.z is in [0, 1]
  (Zero-One), matching `glm::perspectiveRH_ZO`. The default GLM behaviour
  is OpenGL's [-1, 1]; using the `_ZO` variant avoids a manual
  remap. Pinned by `CameraTest.ProjectionMatrixMapsViewportZTo01`.
- **Dirty flag is `mutable`; rebuild is lazy.** Callers read matrices
  through `viewMatrix()` / `projectionMatrix()` const-accessors, which
  recompute on first read after any setter. Production code paths upload
  the matrices into a uniform buffer once per frame; this design lets the
  orbit controller in Phase B.6 mutate the camera state many times within
  a frame (mouse drag, scroll zoom) without each mutation rebuilding the
  matrix — only the final read does.
- **`viewProj()` is not separately cached.** One mat4 multiply is below
  profiling noise; an extra cache slot would add a maintenance burden and
  another dirty-flag invariant to keep straight.
- **`EvaluationContext::camera` is nullable.** Headless tests and the
  current production graph don't need a camera; only the deep-pointcloud
  passes (Phase B.5+) do. Defaulting to null keeps the existing 73-test
  baseline untouched.
- **`EvaluationContext::frame` is a `uint64_t` scene-time index, distinct
  from FrameLoop's GPU frame value.** Documented inline. Scene-time
  monotonically increments per UI frame for playback purposes; GPU frame
  value advances per submit and is used by the timeline-semaphore
  retirement logic. Keeping them separate avoids the temptation to gate
  scene-state changes on a GPU-side counter.

### Files created

| Path | Purpose |
|------|---------|
| `include/core/Camera.hpp` | RH/Y-up perspective camera with lazy-rebuild view/proj |
| `src/core/Camera.cpp` | Implementation; uses glm's `lookAtRH` + `perspectiveRH_ZO` |
| `tests/core/CameraTest.cpp` | 8 cases: dirty-flag transitions, view-matrix correctness, view-origin-maps-to-zero, Vulkan Z range, Y flip, view*proj identity, aspect-only invalidation |

### Files modified

- `include/core/EvaluationContext.hpp` — forward-declares `core::Camera`;
  adds `const Camera* camera = nullptr` and `uint64_t frame = 0`. New
  members carry inline rationale.
- `CMakeLists.txt` — `glm` declared via `FetchContent` (tag `1.0.1`);
  `LoomCore` adds `glm::glm` to its public link interface and
  `src/core/Camera.cpp` to its sources; `LoomTests` adds
  `tests/core/CameraTest.cpp`.

### Verification

- Build: clean.
- `ctest --preset debug` — 89/89 pass (+8 `CameraTest.*` cases). Baseline
  was 81; net +8.
- Manual review: no existing test or production code referenced
  `EvaluationContext::camera` or `::frame`, so adding the fields is a pure
  extension. Defaults are zero/null, so the 73 pre-A.0 tests remain
  unchanged.

### Dependencies

A.1 in the sense that the next caller of the camera (Phase B.5's
`PointCloudPass`) reads per-sample data through `DeepLayout`. No code-level
dependency between A.1 and A.2 — they could have landed in either order.

### Known follow-ups for later sub-phases

- The orbit controller in Phase B.6 mutates `position` / `target` via
  mouse-drag math. The camera's lazy dirty flag is the right shape for
  that traffic pattern.
- Phase B.5's `PointCloudPass` uploads `viewProj()` into a uniform buffer
  once per frame at the start of the dispatch chain. The matrix flows
  through `EvaluationContext::camera`, not through a global.
- A future `OrthoCamera` could derive from a shared `Projection` base, but
  v1 has no need.

---

## Phase A.3 — Generic `Param` tag-union + JSON serialise

### Goal

Land the editable per-node parameter system that Phase B.2's
`DeepEXRReadNode` (path + frame_index params), Phase C.2's timeline
scrubber, Phase C.4's `TransformNode` (translate/rotate/scale), and the
eventual knob-animation system in Phase E all consume. JSON serialise via
the vendored `crude_json` so per-session crash recovery is free.

### Decisions

- **`Param::Value = std::variant<float, int, bool, glm::vec3, std::string>`.**
  Plan-of-record. The five alternatives cover every parameter shape v1
  through v2 ship needs. Adding a new alternative (e.g. `glm::mat4` for a
  transform handle, `std::filesystem::path` if we ever need it distinct
  from `std::string`) is one variant entry + one `if constexpr` branch in
  `toJson` / `fromJson` / `renderParamWidget`.
- **`ParamRange` is unconditionally serialised when meaningful.** The
  range stores `min` / `max` / `step` / `hasBounds`. `hasBounds=false`
  means "no enforced clamp" — the UI falls back to a free-form input. The
  encoder writes `range` when either `hasBounds` or `step != 0`; readers
  tolerate the absence and reconstruct defaults. This keeps the JSON
  small for the common no-bounds case without losing round-trip identity.
- **`Node::buildParams()` is invoked from `Graph::addNode`, not from the
  `Node` constructor.** Calling a virtual function from a constructor
  dispatches to the base, not the subclass. The plan said "invoked from
  constructor"; this is a documented C++ gotcha and the right place is
  one line into `addNode` (right after `setupNodePins`). The Node
  constructor is now const-correct re: vtable setup.
- **`Node::setParam(index, value)` is the canonical mutation entry.** It
  flips `isDirty` and is what both the panel and the tests use. The
  `Param` itself is value-typed and does not back-reference the node, so
  direct `.value() = ...` mutation does *not* flip dirty — the test
  `SetParamFlipsNodeDirty` pins this contract.
- **`NodeEditorPanel` widgets route through `Node::setParam`.** A single
  source of truth for "did this param change → mark dirty + cascade".
  The panel additionally calls `Graph::markDirty(handle)` after a widget
  reports change so downstream nodes flip dirty too (the cascade is
  what triggers re-evaluation in the next frame's `Graph::execute`).
- **Each toy node migrates to the new system.** `ConstantNode` declares
  `color: vec3 = (1, 0, 0)` (replaces the hardcoded red fill).
  `MergeNode` declares `mergeColor: vec3 = (1, 0, 1)` (replaces the
  hardcoded magenta). `ViewerNode` and `PassthroughNode` declare empty
  param vectors but still override `buildParams()` so the
  paramsFromJson / paramsToJson contract is uniform per node. This is
  the minimal-but-real migration the plan calls for.
- **Widget-id namespacing.** ImGui scopes its internal IDs by string;
  two nodes with the same-named param ("color" twice) would collide on
  the canvas. The panel suffixes the widget label with
  `_{nodeIndex}_{nodeGeneration}` so handles disambiguate by node
  identity, not by widget order.
- **`paramsFromJson` matches by name, not by index.** A forward-tolerant
  load: a future build that drops or reorders a param leaves the
  existing values in place rather than zeroing the whole vector. Matches
  the same posture as the channel-naming spec (§19) — newer files load
  in older builds; older files load in newer builds.

### Files created

| Path | Purpose |
|------|---------|
| `include/core/Param.hpp` | `Param`, `ParamRange`, value-variant + JSON round-trip API |
| `src/core/Param.cpp` | Implementation; `valueToJson`, `fromJson` with malformed-input tolerance |
| `tests/core/ParamTest.cpp` | 10 cases: per-type round trip, range round trip, dirty-flag flip, buildParams populated, paramsFromJson, malformed-input default |

### Files modified

- `include/core/Types.hpp` — `Node` gains `std::vector<Param> params`,
  `virtual buildParams()`, `setParam(i, v)`, `paramsToJson()`,
  `paramsFromJson(j)`. Forward-includes `core/Param.hpp`.
- `include/core/Nodes.hpp` — every concrete node declares
  `buildParams() override`.
- `src/core/Nodes.cpp` — implementations: `ConstantNode` and `MergeNode`
  declare a `vec3` param and use it in `execute`; `ViewerNode` and
  `PassthroughNode` provide empty overrides for shape parity. New
  `Node::paramsToJson` / `paramsFromJson` implementations.
- `include/core/Graph.hpp` — `addNode` calls `buildParams()` after
  `setupNodePins`. Inline comment explains why not in the constructor.
- `src/ui/NodeEditorPanel.cpp` — new `renderParamWidget(Node&, size_t)`
  helper. Widget renderers cover the five variant alternatives; each
  routes through `Node::setParam` and the panel calls
  `Graph::markDirty(handle)` to cascade.
- `CMakeLists.txt` — `LoomCore` adds `src/core/Param.cpp`; `LoomTests`
  adds `tests/core/ParamTest.cpp`.

### Verification

- Build: clean.
- `ctest --preset debug` — 99/99 pass (+10 `ParamTest.*` cases). Baseline
  was 89; net +10.
- Manual review:
  - `ConstantNode` now consumes its `color` param (visible as a tinted
    fill in the viewport if you wire it up and run the GUI).
  - Render-cache hits stay valid: changing the color via `setParam`
    flips `isDirty` so the next frame re-evaluates, but a steady-state
    no-edit frame still serves from cache. Pinned indirectly by
    `PushPullTest::DirtyPropagation` (which still passes).
  - `crude_json::value` is owned by the `imgui_node_editor` library
    target; `LoomCore` already links it for the editor itself, so
    nothing new pulls in.

### Dependencies

A.2 in the sense that the `glm::vec3` variant alternative needs glm. No
other inter-A dependencies — A.3 could have landed before A.2.

### Known follow-ups for later sub-phases

- The graph-level JSON serialiser (load / save of the full DAG, viewer
  state, camera) lands in Phase E. `Node::paramsToJson` is already in
  the shape that future serialiser will pull from.
- The widget-rendering branch is currently one big `if constexpr` chain
  inside a `std::visit`. If we add many more types (or if more nodes
  need bespoke widget styles), this would benefit from a registry of
  widget renderers keyed off the variant tag. Premature today.
- The `Param::Value` variant could be extended with `glm::vec2`,
  `glm::vec4`, `glm::quat`, etc. when concrete callers need them.

---
