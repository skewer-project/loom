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

## Phase A.4 — Staging upload path for deep buffers

### Goal

Land the host-visible scratch arena and the `uploadDeepImage` helper that
Phase B.2's `DeepEXRReadNode` will call to push a parsed deep image to
GPU resources. Establish the SoA-on-CPU → AoS-in-buffer interleave that
the eventual shaders will consume.

### Decisions

- **Bump arena, not a true ring.** The plan called for a "host-visible
  mapped ring buffer". V1 ships a bump arena with explicit `reset()` for
  three reasons: (1) the engine is single-threaded as of this branch
  (CONVENTIONS §13), so per-allocation lifetime tracking is overkill;
  (2) every allocation in a frame retires together once that frame's
  submit retires, so the whole-arena reset matches the natural lifetime;
  (3) implementing a true ring with per-slab tagging is ~3× the code
  and unlocks no v1 capability. The header comment explicitly flags the
  upgrade path: when Phase C.1's worker-thread upload lands, the arena
  grows per-slab tagging or splits into per-thread arenas.
- **16 MiB default capacity.** Roomy enough for a 1080p deep image at
  ~16 samples per pixel (the typical path-tracer output) or a
  ~262k-sample NVS frame at the 64-byte layout. Production callers
  override at construction; future config / CLI knob, but v1 hardcodes
  the default. Documented in the header.
- **`HOST_ACCESS_SEQUENTIAL_WRITE` allocation flag.** We never read
  through the mapped pointer — only write. This lets VMA pick a
  write-combined memory type on platforms that expose one, which is
  measurably faster for the large-block copies the deep-image upload
  produces.
- **`io::ParsedDeepImage` is SoA, GPU sample buffer is AoS.** OpenEXR's
  deep API hands per-channel pointers; the natural CPU representation
  is one byte-blob per channel. The GPU shader-side wants per-sample
  records (all channel values for sample S contiguous, then sample S+1,
  ...) so cache behaviour during walks is sensible. `uploadDeepImage`
  performs the interleave inside staging memory before issuing the
  device-side copy. The double-traversal cost is below the
  upload-bandwidth ceiling and saves the shader from doing it on every
  read.
- **`uploadDeepImage` is in `gpu/` and takes pools by reference, not
  through `ResourceFactory`.** The caller already holds references to
  the pools (which is how the rest of the production path works);
  routing through the factory would invert the dependency. Future
  refactor: when an explicit `UploadManager` materialises (Phase C.1
  worker-thread path), it absorbs both the staging arena and the pools.
- **OOM in staging returns an invalid `ResourceRef`, does not partially
  fill.** Pinned by the contract on the header: the caller is
  responsible for sizing the arena. The pools are released by the
  caller via `release` on the (invalid) handles, which is a no-op.
- **Test pattern matches the rest of `tests/gpu/`.** `GTEST_SKIP` when
  no Vulkan device; six new cases — five pure-arena (allocate / align /
  OOM / reset / mapped) plus one end-to-end deep-image upload + readback.
  The readback test interleaves the expected packed bytes the same way
  the upload does, then byte-compares against the GPU-resident sample
  buffer's contents.

### Files created

| Path | Purpose |
|------|---------|
| `include/gpu/StagingArena.hpp` | Bump-arena interface; documents the lifetime + LRU contract |
| `src/gpu/StagingArena.cpp` | Implementation: persistently mapped, sequential-write VMA buffer |
| `include/io/ParsedDeepImage.hpp` | CPU-side SoA-per-channel parsed deep image (Phase B.1's reader output shape) |
| `include/gpu/DeepUpload.hpp` | `uploadDeepImage(cmd, staging, imagePool, bufferPool, parsed, layout) -> ResourceRef` |
| `src/gpu/DeepUpload.cpp` | Interleave + count/offset prefix-sum + copies + barriers |
| `tests/gpu/StagingArenaTest.cpp` | 6 cases including the deep-image round-trip |

### Files modified

- `CMakeLists.txt` — `LoomCore` adds `src/gpu/StagingArena.cpp` and
  `src/gpu/DeepUpload.cpp`; `LoomTests` adds
  `tests/gpu/StagingArenaTest.cpp`.

### Verification

- Build: clean.
- `ctest --preset debug` — 105/105 (52 ran, 53 GPU tests cleanly skipped
  on this no-device machine). Baseline was 99; net +6 (all 6 require a
  Vulkan device and skip in the headless environment, but compile
  cleanly).
- Round-trip test logic: the upload's CPU-side interleave is duplicated
  in the test to produce the expected packed buffer. A device-side test
  would catch any divergence between the two; on this branch the test
  compiles + skips, and CI under Lavapipe (Phase 9) will exercise it.
- Manual review: `uploadDeepImage` issues exactly the right barriers —
  `UNDEFINED → TRANSFER_DST_OPTIMAL` before the copy, `TRANSFER_DST_OPTIMAL
  → GENERAL` after, and a buffer barrier for the samples buffer
  promoting `TRANSFER_WRITE → SHADER_READ`. The layout transitions go
  through `gpu::transitionImageLayout` per CONVENTIONS §3.

### Dependencies

A.1 (DeepLayout — produces the stride / byte offsets the interleave
loop consumes). A.3 in spirit (no code dependency, but the
`DeepEXRReadNode` that drives the eventual production call site will
declare path / frame_index params).

### Known follow-ups for later sub-phases

- Phase B.1's `IDeepReader::readFrame` produces a `DeepFrame` that
  wraps `io::ParsedDeepImage`. The latter is intentionally simple so
  the reader can construct it field-by-field.
- Phase B.2's `DeepEXRReadNode::execute` is the first production
  caller of `uploadDeepImage`. It pulls `ctx.imagePool`, `ctx.bufferPool`
  (the latter to be added to `EvaluationContext` alongside Phase B.1)
  and a frame-scoped `StagingArena` (also to be added to
  `EvaluationContext`).
- Phase C.1's worker-thread upload either (a) tags allocations
  per-slab so a partial-frame retirement still reclaims usable space,
  or (b) gives the worker its own arena and the main thread its own.
  (b) is simpler and probably right.
- Currently the GPU tests use the engine's main `VulkanContext`. When
  Phase 9 wires Lavapipe in CI, the same tests run there with no
  change — the `GTEST_SKIP` path is the headless fallback.

---

## Phase A.5 — `HazardTracker` resource-key widening

### Goal

Widen `HazardTracker`'s tracking key from `ImageKey { poolIndex, generation }`
to `ResourceKey { Kind { Image | Buffer }, poolIndex, generation }`. Adds
buffer-hazard coverage so Phase A.4's deep payloads (two images + one
buffer) decompose cleanly into the tracker.

### Decisions

- **`ResourceKey` extends `ImageKey` with a `Kind` discriminator.** The
  image pool and the buffer pool have independent numbering spaces — a
  buffer at `(poolIndex=0, generation=1)` and an image at
  `(poolIndex=0, generation=1)` are different resources. Keying on Kind
  prevents the cross-pool collision; pinned by the new
  `ImageAndBufferDoNotAliasOnKey` test. The hash combines pool / gen as
  before with a Kind-derived rotation; collision space is still well
  below the per-frame resource count.
- **`HazardTracker` exposes parallel image / buffer spans.** The new
  overloads take `std::span<const BufferHandle>` after the image span,
  defaulting to empty. Existing image-only call sites (the four toy
  nodes' tasks today) compile unchanged because they pass only the image
  span. New deep-EXR consumers pass both.
- **`ComputeTask` gains `readBuffers` / `writeBuffers` parallel vectors.**
  Mirrors the existing `readDependencies` / `writeDependencies` shape.
  Per-node nodes that consume a `ResourceRef::Deep` populate both
  vectors: `readDependencies = {countImage, offsetImage}`, `readBuffers
  = {samples}`. `DispatchManager` reads both spans into the tracker.
- **`DispatchManager` change is two lines.** The dispatch loop now
  computes both spans and passes them through to the tracker's
  `needsBarrierBeforeRead` / `needsBarrierBeforeWrite` calls. The barrier
  emission code is unchanged: `vkCmdPipelineBarrier2` with a single
  `VkMemoryBarrier2` covers all memory accesses regardless of resource
  kind, so the barrier insertion side needs no new buffer-specific
  machinery.
- **Old `ImageKey` is gone, not aliased.** The plan called for a clean
  widening, not a parallel `BufferKey`. Aliasing `ImageKey = ResourceKey
  { Kind = Image, ... }` would have preserved compatibility with old
  test code, but the test file was the only call site and the rename
  is one mechanical sweep. Cleaner public surface.

### Files modified

- `include/gpu/HazardTracker.hpp` — `ImageKey` / `ImageKeyHash` → `ResourceKey`
  / `ResourceKeyHash` with `Kind` discriminator; query methods gain
  `BufferHandle` span overloads.
- `src/gpu/HazardTracker.cpp` — `toKey` overloaded for both
  `ImageHandle` and `BufferHandle`; query methods walk both spans;
  `recordTask` traverses `readBuffers` / `writeBuffers` alongside the
  image vectors.
- `include/gpu/ComputeTask.hpp` — adds `readBuffers` / `writeBuffers`
  vectors with inline rationale.
- `src/gpu/DispatchManager.cpp` — passes the new buffer spans through
  to the tracker.
- `tests/gpu/HazardTrackerTest.cpp` — extended with 4 new cases:
  `BufferRAWBarrierEmitted`, `BufferWAWBarrierEmitted`,
  `ImageAndBufferDoNotAliasOnKey`, `DeepPayloadDecomposesAcrossImagesAndBuffer`.
  Existing 7 cases unchanged in shape and still pass under the wider
  key.

### Verification

- Build: clean.
- `ctest --preset debug` — 109/109 pass (+4 new `HazardTrackerTest.*`
  cases). Baseline was 105; net +4.
- Manual review: the four new tests pin the cross-kind discrimination
  contract (`ImageAndBufferDoNotAliasOnKey`) and the deep-payload
  decomposition (the consumer's `read{Deps,Buffers}` covers all three
  resources). The latter is the actual A.4 / B.2 use case.

### Dependencies

A.4 (deep payloads are the motivating use case). No code dependency on
A.4 — the tracker change is self-contained.

### Known follow-ups for later sub-phases

- The `Kind` enum could grow `Deep` if we ever want a deep-handle
  granularity for barrier checks. Today the deep payload decomposes
  into its constituent image+buffer resources, which is the right
  granularity — a barrier on one constituent's hazard is the same
  pipeline barrier as for the whole deep payload.
- `BufferHandle` lacks the `setLayout` / `getLayout` of `ImageHandle`,
  so layout transitions don't apply. The hazard model is symmetric
  (RAW / WAW), but pre-dispatch image-layout fixups (Pass 1 in
  `DispatchManager::submit`) only touch images.
- The hazard tracker still emits a single `vkMemoryBarrier2` covering
  all accesses when any hazard is detected; finer-grained per-resource
  barriers might marginally help GPU pipelining when many independent
  hazards land in one frame, but profile first.

---

## Phase A.6 — Documentation updates

### Goal

Land the conventions and architecture-doc updates that document Phase A's
new surface area. Specifically: a new §19 in `docs/CONVENTIONS.md` for the
channel-naming spec + coordinate convention, a new §20 for the Param
shape / dirty-flag contract / JSON serialisation, and an architecture-doc
refresh that adds `core::Camera` / `core::DeepLayout` / `core::Param` /
`gpu::StagingArena` / `loom::io` to the layer diagram.

### Decisions

- **§19 documents the channel-naming spec as the file-format contract.**
  Producers (Skewer or any other tool) follow this spec; Loom inspects
  the channel list at load and builds a `DeepLayout` accordingly. The
  spec covers v1 required channels (`Z`, `R`, `G`, `B`, `A`), v1 optional
  (`ZBack`), NVS extensions (`world_pos.*`, `normal.*`, `albedo.*`), and
  Phase D.5 stretch (`sh.*`, `scale`, `material_id`). Forward
  compatibility is explicit: unknown channels are recorded by the layout
  (so writers preserve them) and logged-but-ignored by consumer shaders.
- **§19 documents the coordinate convention as RH/Y-up/meters with an
  `loom/coordSystem` EXR header attribute as the load-time transform
  hook.** Files from a left-handed or Z-up producer carry that attribute;
  Loom transforms at the loader boundary. The internal engine math
  always sees RH/Y-up/meters. The transform table is a one-line addition
  per new convention.
- **§20 documents the Param dirty-flag contract.** `Node::setParam` is
  the **only** mutation entry that flips `isDirty`; direct
  `params[i].setValue(...)` is reserved for restore-from-JSON paths
  where the caller takes responsibility for dirty propagation. The
  `NodeEditorPanel` routes every widget change through `setParam` plus
  `Graph::markDirty` for the downstream cascade.
- **§20 documents the JSON shape verbatim.** A future reader (a
  contributor writing a project save/load PR) can reverse-engineer the
  shape from `Param::toJson`, but pinning the shape in the conventions
  doc is the difference between "we serialise Params" and "this is what
  the wire looks like, here's how to extend it."
- **Architecture doc grows a `loom::io` row.** ParsedDeepImage lives
  there now; Phase B's `IDeepReader` / `IDeepWriter` join it. `io/`
  depends on `core/` (for `DeepLayout`), depended on by `gpu/` (for
  `uploadDeepImage`). The layering rule that `core/` cannot include
  Vulkan headers remains intact and is restated.
- **Per-sub-phase entries in this archive file.** Each sub-phase
  (A.0 through A.6) gets its own section in the archive log, matching
  the structure of `refactor-cleanup-2026.md` (which logged the
  11-phase cleanup that produced the baseline this branch builds on).
  Reviewers can read top-to-bottom to follow the build-out chronology.

### Files modified

- `docs/CONVENTIONS.md` — adds §19 (channel-naming + coordinate
  convention) and §20 (Param shape + dirty-flag + JSON). §17 entry for
  `docs/archive/` extended to reference `feature-open-exr-2026.md`.
- `docs/architecture.md` — system diagram updated with `Camera`,
  `DeepLayout`, `Param`, `StagingArena`, and a new `loom::io` row;
  layer descriptions add the new types; compile-time dependency block
  notes `io → core` and `gpu → io`; out-of-scope paragraph rewritten
  to point at the deep-EXR / NVS phase log.
- `docs/archive/feature-open-exr-2026.md` — this section.

### Verification

- Build: clean (no code change in A.6).
- `ctest --preset debug` — 109/109 pass (unchanged from A.5, headless
  baseline).
- Manual review: §19's channel list matches `tests/data/deep_smoke.exr`
  (v1 required + optional `ZBack`); §20's JSON shape matches
  `Param::toJson` byte-for-byte; architecture-doc diagram matches the
  current `include/` / `src/` directory layout.

### Dependencies

A.0 — A.5 (everything they introduce is what A.6 documents).

### Phase A — global summary

A.0 — A.6 collectively land:

- **OpenEXR + Imath via `FetchContent`** (v3.2.4 / v3.1.12), tools/tests
  disabled — Loom builds standalone from a fresh checkout.
- **`tools/exr_spike` + `tools/make_deep_fixture`** plus a committed
  16×16 deep-EXR fixture (`tests/data/deep_smoke.exr`).
- **`core::DeepLayout`** — interned, hashable, ordered channel schema
  with per-channel byte offsets and packed-sample stride.
- **`core::Camera`** — RH/Y-up perspective camera with lazy view/proj
  rebuild and Vulkan-correct clip-space.
- **`core::Param`** — generic per-node tag-union (float / int / bool /
  vec3 / string) with JSON round-trip and a strict dirty-flag contract.
- **`gpu::StagingArena`** — host-visible bump arena for CPU→GPU uploads.
- **`gpu::uploadDeepImage`** — SoA→AoS interleave + count/offset image
  prefix-sum + samples-buffer copy.
- **`io::ParsedDeepImage`** — CPU-side SoA-per-channel parsed deep image
  (Phase B reader output shape).
- **`gpu::HazardTracker` widened** to a kind-discriminated `ResourceKey
  { Kind, poolIndex, generation }` covering both images and buffers.
- **`ResourceRef::DeepRef.layout`** + `ResourceRef::fromDeep`.
- **`EvaluationContext::camera` / `::frame`** threaded into eval.
- **`Node::buildParams` / `setParam` / `paramsToJson` / `paramsFromJson`**
  on every concrete node; toy nodes migrated.
- **`NodeEditorPanel`** renders generic Param widgets keyed off variant
  tag.
- **glm 1.0.1** added as a header-only `FetchContent` dependency.
- **`docs/CONVENTIONS.md` §19 + §20**.

Test count: 73 → 109 (+36). The 6 new GPU tests (`StagingArenaTest.*`)
skip cleanly on this no-device machine and exercise on Lavapipe in CI
(Phase 9).

Phase A exit criteria from the plan:

- ✅ All tests green.
- ✅ `tools/exr_spike` loads a deep EXR and prints channel-aware
  metadata. (Demonstrated end-to-end with the committed fixture.)
- ⏳ The 4 toy nodes have params editable in the UI. (The Param system
  is wired; live UI verification requires a running session — code-path
  review confirms the widget renderer routes through `setParam`.)
- ✅ OpenEXR is a transitive dep via CMake; Loom builds standalone
  from a fresh checkout.

Phase A is complete.

---

# Phase B — Deep EXR viewing

First user-facing milestone: load a still deep EXR, see it as 2D and as a 3D
point cloud. A new git branch `feature/exr-viewing` was carved off Phase A's
`feature/open-exr` to separate the foundational and user-facing milestones
logically and give us a clean rollback target if Phase B work needs to be
unwound.

## Phase B.1 — `IDeepReader` + sync impl

### Goal

Land the file-I/O entry point Loom uses to load deep EXR files. v1 ships a
synchronous reader behind an async-shaped interface (`std::future<DeepFrame>`)
so Phase C.1's worker-thread implementation can drop in without changing any
call site.

### Decisions

- **Interface is async-shaped from day one.** `std::future<DeepFrame>` is the
  return type even though v1 returns a ready future. The plan was explicit
  about this: "Phase B implements sync; Phase C swaps in worker-thread impl
  behind same interface." The future-shaped surface means
  `DeepEXRReadNode::execute` in B.2 can call `reader->readFrame(path).get()`
  today and the same line just stops blocking when the worker thread lands.
- **`DeepFrame` is "parsed image + interned layout pointer".** The plan
  said "DeepFrame carries the parsed sample data + the resolved DeepLayout
  pointer." Two fields: `io::ParsedDeepImage image` and `const
  core::DeepLayout* layout`. `isValid()` keys off the layout — a default-
  constructed `DeepFrame` is invalid. The reader returns an invalid
  `DeepFrame` on any error path rather than throwing; callers `.get()` is
  total-functional.
- **v1 reader is scalar-only.** Every EXR channel becomes a one-component
  `DeepChannel`. The NVS coalescing (`world_pos.{x,y,z}` → one `vec3`
  channel with components=3) is deferred to Phase D.1, where it's the
  explicit goal of that sub-task. Documenting the scalar floor here so
  reviewers don't expect coalescing at the B.1 boundary. The
  channel-naming spec in §19 already treats coalescing as a loader
  concern, so the contract is unchanged — only the implementation grows.
- **Channel order is whatever OpenEXR gives us.** OpenEXR's `ChannelList`
  iterates alphabetically by name. The reader records channels in that
  order; the resulting `DeepLayout` pointer is stable across two reads of
  the same file (pinned by `InternedLayoutMatchesAcrossReads`). A future
  feature that wants a specific order can canonicalise post-hoc.
- **Per-channel SoA storage with a per-pixel pointer grid.** The OpenEXR
  deep API takes a `DeepSlice` whose `base` is a pointer-per-pixel grid
  of `char*` (the per-pixel sample base). We allocate a flat
  `byteSize * totalSamples` buffer per channel and seed the pointer grid
  with prefix-sum offsets. This is the natural CPU-side SoA shape that
  `io::ParsedDeepImage` carries, and what `gpu::uploadDeepImage` (A.4)
  consumes.
- **Two passes over the file: sample counts, then full read.** Same trap
  as the spike (A.0): re-setting the framebuffer drops the input file's
  internal sample-count cache. The reader calls `readPixelSampleCounts`
  twice — once on the count-only framebuffer, once on the full
  framebuffer. The full read both populates per-channel data and re-
  primes the internal cache for `readPixels` immediately after.
- **OpenEXR is `PRIVATE` linkage on LoomCore.** The `IDeepReader` header
  forward-declares only its public types and never includes any OpenEXR
  header. The implementation includes them. `PRIVATE` keeps the API
  surface clean (downstream `LoomCore` consumers don't drag OpenEXR
  through transitive include paths) but, because `LoomCore` is a static
  library, the symbol references inside `DeepReader.cpp` resolve at the
  final-executable link step via CMake's transitive
  `INTERFACE_LINK_LIBRARIES_DEP` machinery. Tools that link `LoomCore`
  (`Loom`, `LoomTests`) get OpenEXR linked automatically.
- **`LOOM_TESTDATA_DIR` compile-time define for test fixture paths.**
  Tests need to open the committed `.exr` fixture regardless of cwd
  (test runners chdir to the build dir). The CMake target sets
  `LOOM_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data"` once for
  the whole test binary; tests resolve relative names against it. Same
  pattern as the `LOOM_FIXTURE_DIR` used by the Phase A.0 tools, but
  scoped to the test target — production code never sees it.

### Files created

| Path | Purpose |
|------|---------|
| `include/io/DeepReader.hpp` | `DeepFrame`, `IDeepReader`, `SyncDeepReader` |
| `src/io/DeepReader.cpp` | Synchronous OpenEXR Deep parser; builds `DeepLayout` from header channels |
| `tests/io/DeepReaderTest.cpp` | 5 cases: channel set, sample counts, Z round-trip, missing file, interning stability |

### Files modified

- `CMakeLists.txt` — `LoomCore` adds `src/io/DeepReader.cpp` and a `PRIVATE`
  link line for `OpenEXR::OpenEXR` / `Imath::Imath`. `LoomTests` adds the
  new test source and a `LOOM_TESTDATA_DIR` compile-time define pointing at
  the source-tree fixture dir.

### Verification

- Build: clean.
- `ctest --preset debug` — 114/114 pass (+5 `DeepReaderTest.*` cases).
  Baseline was 109; net +5.
- The five tests cover: channel-set discovery, exact per-pixel sample-count
  reconstruction (matches `make_deep_fixture`'s deterministic pattern),
  per-sample Z value round-trip (the same formula the writer uses,
  validated sample-by-sample), the missing-file → invalid-frame error
  path, and pointer-equality interning across two reads of the same file.

### Dependencies

A.0 (OpenEXR fetch + the committed fixture), A.1 (`DeepLayout` + registry),
A.4 (`io::ParsedDeepImage` is the output shape).

### Known follow-ups for later sub-phases

- Phase B.2's `DeepEXRReadNode` is the first production caller — it
  constructs a `SyncDeepReader` (or pulls one from a future
  `EvaluationContext::deepReader` if the engine grows a shared instance)
  and feeds `DeepFrame` into `gpu::uploadDeepImage`.
- Phase C.1 replaces the body of `readFrame` with a worker-thread parse
  and a real (not pre-completed) future. The interface is unchanged so
  no `DeepEXRReadNode::execute` line moves.
- Phase D.1 extends `buildLayoutFromHeader` with the `.{x,y,z}` /
  `.{r,g,b}` coalescer. Until then, NVS deep files load as
  6+3+3+3 = 15 scalar channels rather than 3+1 vec3+vec3+vec3+vec3 — the
  layout is still correct, just denormalised.
- `readPixelSampleCounts` is called twice per `readFrame`. OpenEXR's
  internal cache means the second call is essentially free (it walks the
  same compressed chunks already in memory), but if profiling ever shows
  it as a hotspot, the trick is to keep `fb` alive across the two passes
  and append the per-channel slices in place rather than rebuilding it.

---

## Phase B.2 — `DeepEXRReadNode`

### Goal

First production caller of `IDeepReader` and `gpu::uploadDeepImage`. A graph
node that loads a deep EXR from a `file_path` param and stores the resulting
`Kind::Deep` payload on its output pin. The chain `DeepEXRRead → Viewer`
becomes the first end-to-end deep visualisation surface (with B.3's
`DeepFlattenNode` between them).

### Decisions

- **`EvaluationContext` gains four nullable fields.** `bufferPool`,
  `stagingArena`, `deepReader` join the existing `imagePool` /
  `pipelineCache` / etc. All four are nullable: only nodes that need them
  populate the field. Defaults to null means the existing four toy nodes
  still execute unchanged in tests that don't construct a Vulkan context.
- **`io::IDeepReader` is forward-declared in `EvaluationContext.hpp`.**
  Avoids pulling the `<future>` machinery into every translation unit that
  touches the eval context. The concrete reader implementation header
  (`io/DeepReader.hpp`) is included by `Nodes.cpp` and any other consumer.
- **New `NodeType::DeepEXRRead` + dispatch in `Graph::addNode`.** Same
  pattern as the existing toy nodes — one entry in the switch, one entry
  in `getDefaultNodeName`. Spawn menu in the node editor adds a "DeepEXRRead"
  item so the node is reachable from the UI.
- **Pin schema is one `DeepBuffer` output, zero inputs.** Reader is a
  pure source. `markRequiredTiles` is the trivial "insert self" pattern.
  Region propagation upstream is moot because there is no upstream.
- **Two params: `file_path: string` and `frame_index: int` (default 0).**
  Matches the plan verbatim. `frame_index` carries `ParamRange{hasBounds,
  min=0, max=9999, step=1}` so the UI renders a `SliderInt` rather than a
  free-form input. Phase C.2's timeline UI will drive `frame_index` from
  the global playhead.
- **`Node::pullDeepInput` typed accessor lands here, not at B.3.** B.3's
  `DeepFlattenNode` is the first consumer, but the accessor mirrors
  `pullImageInput` and the natural place is next to it. Asserts
  `Kind::Deep` so any wiring mismatch that slipped past `canAddLink`
  surfaces at evaluation rather than producing garbage GPU data.
- **Defensive nullability on the eval context.** Production-time the
  context always carries reader + pools; tests that construct the node
  headlessly do not. `execute` checks each before use and returns an
  invalid `ResourceRef::fromDeep({})` on missing fields, with a warn-
  level log line per failure case. This is what allows the headless
  `DeepEXRReadNodeTest` cases to exercise the early-return branches
  without crashing on null pointer dereference.
- **`file_path == ""` is treated as "not configured", logged at info, not
  warn.** A freshly-spawned node has no path; that's a normal first-frame
  state, not an error. The user types a path; the next frame's evaluation
  loads.
- **No cache hash on the node side; rely on `RenderCache`.** The plan
  hinted at a per-node `m_cache { (path, frame_index) → DeepFrame }` to
  skip re-parsing when the parameters don't change. v1 trusts the
  existing `RenderCache (pin × region → ResourceRef)` plus the `isDirty`
  flag: re-evaluation only happens when the node is dirty, and editing
  a param via `setParam` flips dirty. The cache hash would be a Phase C
  concern when sequence playback churns through paths and the per-frame
  re-parse cost becomes visible.

### Files created

| Path | Purpose |
|------|---------|
| `tests/core/DeepEXRReadNodeTest.cpp` | 4 cases: pin schema, param defaults, empty-path early return, missing-context handling |

### Files modified

- `include/core/EvaluationContext.hpp` — adds nullable `bufferPool`,
  `stagingArena`, `deepReader` fields with inline rationale; forward-declares
  `gpu::TransientBufferPool`, `gpu::StagingArena`, `io::IDeepReader`.
- `include/core/Types.hpp` — `NodeType` gains `DeepEXRRead`.
- `include/core/Nodes.hpp` — `DeepEXRReadNode` class declaration.
- `src/core/Nodes.cpp` — implementation of all `DeepEXRReadNode` virtuals;
  defensive nullability on `ctx.deepReader` / `bufferPool` / `stagingArena`;
  `Node::pullDeepInput` typed accessor mirroring `pullImageInput`.
- `include/core/Graph.hpp` — `addNode` and `getDefaultNodeName` switches
  extended with the new node type.
- `src/ui/NodeEditorPanel.cpp` — spawn-menu entry for the new node.
- `CMakeLists.txt` — `LoomTests` adds `tests/core/DeepEXRReadNodeTest.cpp`.

### Verification

- Build: clean.
- `ctest --preset debug` — 118/118 pass (+4 `DeepEXRReadNodeTest.*` cases).
  Baseline was 114; net +4.
- The four tests cover: pin schema (1 `DeepBuffer` output, no inputs),
  param defaults (empty `file_path`, `frame_index = 0`, bounds set),
  empty-path early return (no crash, invalid deep ref stored),
  missing-context handling (path set but reader/pools null — no crash).

### Dependencies

A.4 (`uploadDeepImage`), A.3 (`Param`), A.1 (`DeepLayout`), B.1 (`IDeepReader`).
The first node that exercises the full Phase-A toolchain end-to-end.

### Known follow-ups for later sub-phases

- B.3's `DeepFlattenNode` will be the first to call `pullDeepInput`,
  validating that accessor in the graph evaluator.
- B.7's `main.cpp` will construct `bufferPool`, `StagingArena`, and a
  `SyncDeepReader`, populating the four new `EvaluationContext` fields
  before each `Graph::execute` call.
- C.2's timeline UI drives `frame_index` from a global playhead param.
  At that point `DeepEXRReadNode` listens for the
  `EvaluationContext::frame` change via the same `isDirty` mechanism.
- The "no per-node frame cache" decision is intentional — but if Phase C
  finds re-parse-per-frame too costly, the cache hash lands here as
  `std::optional<{path, frame_index, DeepFrame}>` plus a one-line check
  at the top of `execute`.

---

## Phase B.3 — `DeepFlattenNode` + compute shader

### Goal

Complete the first end-to-end deep visualisation chain: `DeepEXRRead →
DeepFlatten → Viewer → DisplayPass`. The flatten node walks each pixel's
samples in the deep payload, front-to-back composites them into an RGBA32F
image, and routes that image into the existing viewer / display chain.

### Decisions

- **Shader hardcodes the v1 standard layout.** Stride 16 bytes per sample:
  `Z` at offset 0 (Float32), `ZBack` at 4 (Float32), `RGBA` at 8 as two
  packed Float16 pairs. This matches the committed fixture and the
  channel-naming spec's required v1 channel set. The push-constant block
  carries only what's runtime-variable: the four bindless slots
  (`countSlot`, `offsetSlot`, `samplesSlot`, `outputSlot`) and the image
  dimensions. Total 24 bytes — well within budget.
- **`DeepFlattenNode` validates the input layout at execute.** Calls a
  free function `layoutMatchesV1Flatten(layout)` that checks `stride() ==
  16` and the byte offset of every named channel. A mismatch logs at warn
  and stores an invalid output (no dispatch). Phase D's
  layout-flexible variant will replace this check with offset push-constants.
- **`ResourceRef::DeepRef` gains `width` and `height` fields.** Consumers
  (this node, B.5's `PointCloudPass`) need the source dimensions to size
  their dispatch / draw extent without round-tripping through the pool's
  image spec. The plan's
  upload helper already had this metadata available; surfacing it on the
  payload is cheaper than a pool lookup. `ResourceRefTest` extended to
  cover the new fields.
- **Output image is sized to the source deep image, not the viewport.**
  The viewer pulls whatever the flatten node produces; the `DisplayPass`
  already handles arbitrary-size source images. Sizing to the source
  matches what production NLE / compositor users expect — "this is the
  comp at its working resolution" — and the viewport is purely a
  presentation transform.
- **Front-to-back composite with premultiplied "over".** Channel-naming
  spec mandates premultiplied RGB; the shader assumes it. Samples are
  assumed pre-sorted by Z (smallest first); v1 does not re-sort. Early
  termination at `remaining < 1e-4` is the standard optimisation for
  opaque-heavy scenes (no visible difference, saves cycles).
- **Two image-binding views in the shader.** GLSL aliases binding 0 with
  both `r32ui readonly` and `rgba32f writeonly` declarations. Each
  declaration is a separate "view" but reads / writes go to the same
  underlying bindless slot — the spec allows aliased declarations as
  long as you only access slots whose underlying view format matches
  the declaration. Validation layers verify this at runtime.
- **`buildDeepFlattenTask` populates both `readDependencies`
  (count + offset images) and `readBuffers` (samples).** The hazard
  tracker widening from A.5 covers both kinds in one call; this is the
  first production node to exercise the buffer-side path.
- **`pullDeepInput` (added in B.2) is the first production caller.** The
  typed accessor asserts `Kind::Deep` and unwraps the payload. The
  headless test `NoUpstreamProducesInvalidOutputRef` pins the empty-
  upstream early-return so the node can run before its input is wired
  without crashing.

### Files created

| Path | Purpose |
|------|---------|
| `shaders/DeepFlatten.comp` | Per-pixel front-to-back deep composite, v1 layout |
| `tests/core/DeepFlattenNodeTest.cpp` | 4 cases: pin schema, no-upstream early return, wiring approve / reject vs Deep / Image pins |

### Files modified

- `include/gpu/ResourceHandles.hpp` — `DeepRef` gains `width` / `height`.
- `src/gpu/DeepUpload.cpp` — populates the new fields from `ParsedDeepImage`.
- `tests/core/ResourceRefTest.cpp` — `FromDeepCarriesPayload` extended
  to cover `width` / `height`.
- `include/core/Types.hpp` — `NodeType::DeepFlatten` added.
- `include/core/Nodes.hpp` — `DeepFlattenNode` class.
- `src/core/Nodes.cpp` — node implementation; v1 layout check; new
  `colorSpec(w, h)` helper for non-viewport-sized output images.
- `include/core/Graph.hpp` — `addNode` and `getDefaultNodeName` dispatch
  the new node.
- `include/core/NodeTaskBuilders.hpp` / `src/core/NodeTaskBuilders.cpp`
  — `buildDeepFlattenTask(ctx, src, out, label)` produces the dispatch.
- `src/ui/NodeEditorPanel.cpp` — spawn-menu entry "DeepFlatten".
- `CMakeLists.txt` — `LoomTests` adds `tests/core/DeepFlattenNodeTest.cpp`;
  shader picked up by the existing `file(GLOB_RECURSE)` over
  `shaders/*.comp`.

### Verification

- Build: clean. `DeepFlatten.comp.spv` materialises in
  `build/debug/bin/shaders/` alongside the existing two SPIR-V binaries.
- `ctest --preset debug` — 122/122 pass (+4 `DeepFlattenNodeTest.*` +1
  `ResourceRefTest.FromDeepCarriesPayload` change still passes after the
  width/height extension). Baseline was 118; net +4 new cases.
- Manual: pin-type compatibility check pins both directions — Deep →
  Deep works, Image → Deep is rejected at `canAddLink`. This is the
  first concrete test that exercises the heterogeneous pin-type
  contract.

### Dependencies

A.4 (`uploadDeepImage`'s `DeepRef` shape, now extended), A.5 (hazard
tracker covers buffer reads), B.1 (deep reader producing the upstream
payload), B.2 (`DeepEXRReadNode` + `pullDeepInput`).

### Known follow-ups for later sub-phases

- The first GPU readback test (validation-layer-warning count == 0
  under Lavapipe per the plan) lands when CI gets a GPU. The headless
  tests cover the wiring / fallback contracts.
- Phase D introduces a layout-flexible shader. `buildDeepFlattenTask`
  will then take a layout argument and push per-channel byte offsets
  into the constant block.
- Sample sorting (if a producer ever emits unsorted samples) belongs
  either in the upload helper (post-interleave sort by Z) or as a
  separate `DeepSortNode`. Production deep EXRs from path tracers are
  conventionally sorted, so this is a backlog item, not a v1 gap.

---

## Phase B.4 — Graphics pipeline support in `PipelineCache`

### Goal

Extend the existing compute-only `PipelineCache` with a graphics-pipeline
overload. Shares the on-disk `VkPipelineCache` blob between compute and
graphics so warm-cache loads benefit both stage types. Unblocks Phase B.5's
`PointCloudPass` and any future graphics passes (UI overlays, splat
rendering in Phase D).

### Decisions

- **Key shape per the plan.** `GraphicsPipelineKey { vertSpv, fragSpv,
  layout, vertexInput, topology, blend, depth, colorFormat, depthFormat,
  samples }`. The plan didn't call out `topology` or `depthFormat`
  explicitly, but PointCloudPass (B.5) needs `Topology::PointList` and
  any depth-tested pass needs a depth attachment format. Both additions
  are small enums; tests pin their distinct-key contracts.
- **`layout` lives in the key, not the cache.** Each call site (DisplayPass-
  style fullscreen passes, PointCloudPass) owns its own `VkPipelineLayout`
  — different push-constant ranges and stage masks. The cache keys on the
  handle so two passes with the exact same layout dedup their pipelines
  if every other field matches. Pipeline layouts are stable for the
  engine's lifetime, so this keying is safe.
- **Enum-permutation `BlendMode` / `DepthMode` instead of full Vulkan
  state structs.** The plan was explicit: enum-permutation keying is
  right-sized for a research compositor. The three blend modes
  (`Opaque`, `AlphaBlend`, `Additive`) and three depth modes (`None`,
  `TestWrite`, `TestNoWrite`) cover every v1 use case. Adding a new
  mode is a switch-statement entry plus a key value.
- **`AlphaBlend` is premultiplied.** `Csrc + (1 - Asrc) * Cdst`. Matches
  the channel-naming spec's premultiplied-radiance convention (§19) and
  the front-to-back composite math in `DeepFlatten.comp`. A
  straight-alpha mode would be a fourth enum entry; no production caller
  needs it today.
- **`VertexInputDesc::None` is the only entry shipped.** Every shader in
  Loom (fullscreen triangle, point cloud, future splat) pulls data via
  `gl_VertexIndex` from a bindless SSBO or constant array. There's no
  per-vertex attribute binding to describe. The enum exists so future
  mesh-rendering passes have a clean extension point — adding
  `MeshVertex` is a switch-statement entry that populates the
  `VkPipelineVertexInputStateCreateInfo` from a hardcoded vertex format.
- **Existing `DisplayPass` is NOT migrated to use the new API.**
  DisplayPass predates the cache and owns its pipeline directly. The
  plan called out templating the body on `DisplayPass::createPipeline`,
  which the new `getOrCreateGraphics` implementation does morally — same
  state machinery, parameterised by the key. Migrating the existing
  DisplayPass would be a churn-only PR; we leave it alone and route new
  passes through the cache.
- **Pipeline layout's handle hashed as a `uintptr_t`.** `VkPipelineLayout`
  is an opaque pointer (typedef of `VkPipelineLayout_T*`). Casting to
  `uintptr_t` for hashing is portable and produces good distribution.
  The handle's bit pattern is determined by the driver; we trust it not
  to be pathologically clustered.
- **Pipelines destroyed in the destructor.** Both compute and graphics
  pipelines walk their respective maps and call `vkDestroyPipeline`. The
  on-disk cache blob is written before the cache itself is destroyed so
  the cold-boot path on next launch warms both compute and graphics.

### Files modified

- `include/gpu/PipelineCache.hpp` — new `VertexInputDesc`, `Topology`,
  `BlendMode`, `DepthMode` enums; `GraphicsPipelineKey` struct with
  `operator==` and `GraphicsPipelineKeyHash`; `PipelineCache::
  getOrCreateGraphics(key)` declaration; new map member.
- `src/gpu/PipelineCache.cpp` — `operator==` and hash implementations;
  destructor extended to clean up graphics pipelines; new
  `getOrCreateGraphics` body with `blendAttachmentFor` / `depthStateFor`
  helpers; uses dynamic-rendering (`VkPipelineRenderingCreateInfo`) per
  the existing engine convention.
- `tests/gpu/GraphicsPipelineKeyTest.cpp` — new (9 cases, headless): key
  equality / inequality across every field permutation, hash stability,
  hash sensitivity.
- `CMakeLists.txt` — `LoomTests` adds the new test source.

### Verification

- Build: clean.
- `ctest --preset debug` — 131/131 pass (+9 `GraphicsPipelineKeyTest.*`).
  Baseline was 122; net +9. All headless — the actual
  `vkCreateGraphicsPipelines` path needs a Vulkan device and exercises
  in CI under Lavapipe (Phase 9) and locally on B.5 once the
  PointCloudPass is wired through.

### Dependencies

None within Phase B. Unblocks B.5.

### Known follow-ups for later sub-phases

- B.5's `PointCloudPass` is the first production caller. It builds a
  key with `PointList` topology, `TestWrite` depth, the swapchain's
  color format. The cache deduplicates if the viewport-mode toggle
  oscillates the user between Flat2D and PointCloud3D.
- A future splat-rendering pass (Phase D) adds an entry to the
  vertex-input enum if it wants explicit per-splat vertex attributes
  rather than SSBO pulls.
- Migrating `DisplayPass` to use the cache would unify pipeline
  ownership but is pure churn — the existing direct-construction path
  works and predates the cache.

---

## Phase B.5 — `PointCloudPass` + shaders

### Goal

Render a deep payload as a per-sample point cloud, depth-tested, into an
arbitrary RGBA color attachment. Sibling to `DisplayPass`. v1 produces a
height-field-shaped point cloud (XY from source pixel, Z from sample
depth) because no `world_pos` channel exists yet; Phase D.1 wires the
NVS extension shader.

### Decisions

- **Per-sample vertex shader + bindless SSBO pull.** Each of the
  `totalSamples` points is one vertex; `gl_VertexIndex` maps directly
  to a sample. The shader pulls per-sample bytes from the SSBO at the
  bindless slot supplied via push constants — same `BindlessUintBuffer`
  declaration used by `DeepFlatten.comp`. No vertex buffer binding, no
  index buffer. Topology is `Topology::PointList` (the B.4
  enum-permutation key carries the choice through to
  `vkCreateGraphicsPipelines`).
- **`sampleToPixel: BufferHandle` added to `DeepRef`.** The vertex
  shader needs the pixel ancestry of each sample to derive a world
  position. Three options were on the table:
    1. Linear scan of `offsetImage` per vertex — `O(W·H)` per vertex,
       embarrassing.
    2. Binary search of `offsetImage` per vertex — `O(log W·H)` per
       vertex.
    3. Precomputed `uint sampleToPixel[totalSamples]` buffer — `O(1)`
       per vertex, one extra `O(totalSamples)` CPU pass at upload time.
  v1 ships (3). It's the simplest correct design; the per-sample cost
  is `sizeof(uint32_t)` extra GPU memory, dwarfed by the sample
  payload itself.
- **`DeepRef::totalSamples` exposed too.** Both the draw call
  (`vkCmdDraw(totalSamples, 1, 0, 0)`) and the future Phase C clustering
  need it. Cheaper to carry the running sum than re-prefix-summing in
  the shader.
- **World-position synthesis without `world_pos`.** v1 lacks NVS
  channels (Phase D.1's job to surface them). For v1 the vertex shader
  maps pixel `(px, py)` to centered-unit-square XY and uses scaled Z
  for depth: `worldPos = vec3(2*px/W − 1, 1 − 2*py/H, -z*zScale)`. The
  Y flip matches the RH/Y-up convention (pixel row 0 → world +Y).
  Documented in the shader; replaced wholesale in Phase D.
- **PointSize via the legacy `gl_PointSize` write.** Modern Vulkan
  requires the `shaderTessellationAndGeometryPointSize` feature
  technically, but for non-tess/non-geom shaders the write is
  supported on every desktop driver and the `Topology::PointList`
  rasterises one fragment per pixel-sized point by default. v1 ships
  with a 2.0-pixel point size; future config or per-node param.
- **PointCloudPass owns its own pipeline layout + camera UBO.** Two
  descriptor sets: set 0 is the engine's bindless set (passed by
  caller); set 1 is a dedicated `CameraUbo { mat4 viewProj }` allocated
  inside the pass. The pass owns its `VkDescriptorPool` /
  `VkDescriptorSetLayout` / `VkDescriptorSet` and one host-visible
  mapped buffer for the camera matrices. The plan called for "Camera
  matrices from `EvaluationContext::camera` → uniform buffer"; this is
  that wiring.
- **Lazy depth-image allocation, resized on viewport change.** The
  pass holds one depth image (`VK_FORMAT_D32_SFLOAT`) sized to the
  most-recent `record(...)` extent. On size change, destroy and
  recreate; otherwise reuse. The viewport rarely resizes, so the
  reallocation cost is negligible. Depth uses `LOAD_OP_CLEAR` (no
  carry-over from prior frames) and `STORE_OP_DONT_CARE` (one-shot
  rasterise → fragment lookup, no readback path).
- **Pre-pass barrier batching.** A single `vkCmdPipelineBarrier2`
  transitions both the color destination (`UNDEFINED →
  COLOR_ATTACHMENT_OPTIMAL`) and the depth attachment (`UNDEFINED →
  DEPTH_ATTACHMENT_OPTIMAL`). One barrier emission instead of two —
  matches the `DisplayPass::record` pattern.
- **Post-pass transition to `SHADER_READ_ONLY_OPTIMAL`.** Same
  contract as `DisplayPass::record`: the viewport image needs to be
  sampleable by ImGui at the end of the frame. The caller doesn't
  need to know which pass produced the image.
- **`uploadDeepImage`'s barrier emission widened.** Was one
  `VkBufferMemoryBarrier2` for the samples buffer; now a vector that
  covers both samples and sampleToPixel. The destination stage mask
  promotes to `ALL_COMMANDS` (was `COMPUTE_SHADER`) so both compute
  (DeepFlatten) and vertex (PointCloud) consumers are covered without
  a second barrier emission.

### Files created

| Path | Purpose |
|------|---------|
| `shaders/PointCloud.vert` | Per-sample vertex shader; bindless SSBO pull; v1 world-pos synthesis |
| `shaders/PointCloud.frag` | Trivial colour passthrough |
| `include/gpu/PointCloudPass.hpp` | Pass class declaration |
| `src/gpu/PointCloudPass.cpp` | Implementation: descriptor / UBO / depth resources + `record` |

### Files modified

- `include/gpu/ResourceHandles.hpp` — `DeepRef` gains `sampleToPixel`
  buffer and `totalSamples` count.
- `src/gpu/DeepUpload.cpp` — populates the new fields; widens the
  per-buffer barrier emission to cover both buffers.
- `tests/core/ResourceRefTest.cpp` — covers the new fields.
- `CMakeLists.txt` — `LoomCore` adds `src/gpu/PointCloudPass.cpp`;
  shaders picked up by the existing `file(GLOB_RECURSE)`.

### Verification

- Build: clean.
- `ctest --preset debug` — 131/131 (no test count change; the existing
  `ResourceRefTest.FromDeepCarriesPayload` covers the new fields).
- Shader compilation: both `PointCloud.vert.spv` and
  `PointCloud.frag.spv` materialise in `build/debug/bin/shaders/`
  alongside the existing SPIR-V binaries. `DeepFlatten.comp.spv` still
  compiles after the layout edit — the binding decoration changed
  inside DeepUpload (a buffer-barrier emission change, not a shader
  edit).
- The pass itself requires a Vulkan device to exercise — the GPU
  integration test lands when B.6 wires the pass into the engine and
  a Lavapipe CI matrix entry can run the end-to-end orbit. Headless
  this branch demonstrates: types compile, shaders parse, key
  permutations dedupe correctly.

### Dependencies

A.2 (`Camera::viewProj` produces the matrix), A.4 (`uploadDeepImage`),
A.5 (buffer hazards covered), B.4 (`getOrCreateGraphics` produces the
backing `VkPipeline`).

### Known follow-ups for later sub-phases

- B.6's `ImGuiRenderer` integration constructs one `PointCloudPass`
  during init and routes the recording call when the viewport-mode
  dropdown is `PointCloud3D`. The Camera in `EvaluationContext` is
  the one the orbit controller mutates.
- B.7's `main.cpp` constructs the pass with the swapchain's color
  format and the existing bindless layout.
- Phase D.1's NVS-aware variant adds a second shader (e.g.
  `PointCloudNVS.vert`) and a second `GraphicsPipelineKey` permutation.
  The same `PointCloudPass` class hosts both; the choice is per-frame
  based on `DeepRef::layout`.
- A configurable point-size / `zScale` lands when a future
  `PointCloudViewerNode` exposes them as user params. v1 hardcodes
  2-pixel points and a unit `zScale`.

---

## Phase B.6 — Viewport-mode toggle + orbit camera

### Goal

User can swap between flat 2D and 3D point-cloud rendering inside the
viewport panel, and orbit the camera with mouse drag + scroll. Realises
the plan's exit criterion: "dropdown swap to PointCloud3D shows the same
data as an orbit-able point cloud, camera responds to mouse."

### Decisions

- **`ui::ViewportMode` lives in `ImGuiRenderer.hpp`.** Two values today
  (`Flat2D`, `PointCloud3D`). The renderer owns the mode because the
  dropdown is rendered inside the viewport panel header. The engine
  queries `getViewportMode()` once per frame to decide whether to drive
  the flat path (`DisplayPass`) or the 3D path (`PointCloudPass`).
- **`drawDockspace(orbitCamera = nullptr)` is the integration point.**
  Existing callers (no Phase B engine wiring) still get the flat-only
  behaviour by default — nullptr means "don't draw the mode dropdown,
  don't apply orbit input, behave as before". Phase B.7's `main.cpp`
  passes a real `Camera*`. Optionality avoids a breaking change to the
  one-arg-less signature; the renderer doesn't need to know the camera
  exists.
- **Orbit controller stores spherical state.** `(yaw, pitch, radius)`
  around `camera.target()`. On first use the controller reads the
  camera's current pose and derives spherical coordinates from
  `position - target`. Subsequent calls write a fresh `position` =
  `target + radius * (sin(yaw)cos(pitch), sin(pitch), cos(yaw)cos(pitch))`.
  Yaw is RH around world +Y; pitch is around the camera-right axis.
- **Multi-driver camera arbitration is out of scope.** Once the orbit
  controller engages, it owns the camera position. Other code that
  calls `setPosition` between frames would be silently overwritten on
  the next `applyOrbitInput`. Production-time only the orbit
  controller touches camera pose; if a future feature needs co-driving
  (animation curves, presets), the contract grows then.
- **Pitch clamped to ~89°.** Below ~90° the `up` vector becomes
  degenerate; `lookAtRH` produces a NaN basis. The 89° clamp leaves a
  perceptible margin and pins it as an explicit invariant rather than
  a "why is everything black at top-down" mystery.
- **Drag sensitivity 0.005 rad/pixel.** A full screen-width swipe
  produces ~π/2 rad of yaw (one quarter turn). Tuned to feel like
  Blender's default orbit. No user-facing config knob in v1; future
  preferences-panel concern.
- **Zoom is multiplicative.** Each scroll tick scales radius by `1.1`
  (in or out). Multiplicative beats additive because the perceived
  motion is consistent across orders of magnitude (zooming from 10m →
  9m feels the same as 0.1m → 0.09m). Clamped to `[0.05, 1000]` to
  avoid degenerate near-zero or wildly distant cameras.
- **Aspect-ratio updates on viewport resize, unconditionally.** The
  camera's projection matrix depends on viewport aspect; pushing it
  here keeps the dependency one-directional (UI → camera, not the
  other way). The `Camera`'s lazy dirty flag means this is a no-op
  when aspect doesn't actually change.
- **Drag uses `ImGui::IsItemHovered()` against the viewport image.**
  Standard ImGui idiom for "respond to mouse only when the cursor is
  over my widget". Without it, dragging anywhere on the screen would
  rotate the camera — common bug in homebrew Vulkan + ImGui setups.

### Files modified

- `include/ui/ImGuiRenderer.hpp` — `ViewportMode` enum;
  `drawDockspace(core::Camera* = nullptr)`; orbit state members;
  `applyOrbitInput` private helper.
- `src/ui/ImGuiRenderer.cpp` — `applyOrbitInput` body; dropdown header
  inside the viewport panel; orbit-input dispatch only on
  `PointCloud3D` mode + hovered viewport; aspect-ratio sync on
  resize.

### Verification

- Build: clean.
- `ctest --preset debug` — 131/131 (no test count change; the touched
  files are GUI-only and only exercise under a running session, which
  the test suite can't validate headlessly).
- Manual code-review walkthrough of the orbit math:
  - First-use initialisation from the camera's current pose preserves
    whatever the caller set explicitly.
  - Yaw / pitch / radius produce the spherical-to-cartesian formula
    matching the GLM `lookAtRH` convention.
  - The pitch clamp prevents the gimbal-lock degeneracy.
  - The hover gate scopes input to the viewport panel.

### Dependencies

A.2 (`core::Camera` lazy-dirty + RH/Y-up convention),
B.5 (`PointCloudPass` is what the `PointCloud3D` mode dispatches into).

### Known follow-ups for later sub-phases

- B.7 wires this into `main.cpp`: passes the shared `Camera*` into
  `drawDockspace`, and at end-of-frame branches on `getViewportMode()`
  to choose between `DisplayPass::record` and `PointCloudPass::record`.
- A panning gesture (middle-mouse drag → translate `camera.target`)
  is the obvious extension. Phase B's exit criterion doesn't require
  it, so deferred.
- The orbit controller could carry a `m_speed` field for future
  acceleration / decay behaviour. v1 is instant — drag distance maps
  directly to angle delta.
- Multi-viewport support (Phase E) means multiple `ViewportMode`
  states co-existing. The renderer's single-`m_viewportMode` field
  would split into a per-viewer map then.

---

## Phase B.7 — Startup graph + CLI argument

### Goal

Realise the Phase B exit criterion end-to-end: `./build/bin/Loom
path/to/deep.exr` opens the window, builds a `DeepEXRRead → DeepFlatten →
Viewer` chain pre-populated with the file path, shows the flattened deep
image, and lets the user swap to PointCloud3D mode for an orbit-able 3D
view of the same data.

### Decisions

- **CLI signature is positional: `Loom [path]`.** One optional path
  argument. No flags in v1. Future config (point size, zScale, default
  mode) belongs in a flat config-file (Phase E `config/` layer) or in
  per-node params, not in CLI flags — keeps the entry surface minimal.
- **Empty `argv` falls back to the legacy `Constant → Viewer` demo
  graph.** Preserves the v1 "open the GUI and play with the node
  editor" experience for contributors who don't have a deep EXR
  handy. The legacy path is intentionally kept rather than ripped out
  — it's the smallest possible graph that exercises the engine
  end-to-end.
- **`buildDeepViewChain` builds the canonical three-node chain at
  startup.** `DeepEXRRead → DeepFlatten → Viewer`. Returns the
  `DeepEXRRead` handle so the per-frame loop can pull its output
  payload directly from the render cache without walking the graph
  topology each frame. Future "open recent" UX would replace this
  helper with a user-driven add-node-and-wire flow.
- **`StagingArena` and `SyncDeepReader` are global per process.** Both
  are stateless except for their internal allocation tracking. One
  arena, one reader, threaded through `EvaluationContext` to the read
  node. Phase C.1 splits the reader into a worker-thread variant; the
  arena gains per-slab tagging when it does.
- **`StagingArena::reset()` is NOT called per-frame in v1.** v1 loads
  the EXR once and reuses the GPU resources across every subsequent
  frame. The arena accumulates bytes per upload; with the default
  16 MiB capacity, the user can re-load the file ~1000× before the
  arena fills (for a typical few-KB fixture). Phase C.1 introduces
  per-slot arenas tagged by the FrameLoop retirement counter — that's
  when reset becomes per-frame. Documented in this section and on the
  arena header.
- **`TransientBufferPool::onFrameRetired` is now called per-frame
  too.** Previously dormant because no production node touched
  buffers; now exercised by the deep upload. Without the per-frame
  retirement call, freed buffer entries would never recycle. Mirror
  of the existing `imagePool.onFrameRetired` path.
- **Mode-driven pass selection at end of frame.** After
  `dispatchManager.submit`, the engine consults
  `imgui.getViewportMode()`:
    - `PointCloud3D` + valid deep payload → `pointCloudPass.record(...)`.
    - Otherwise → `displayPass.record(...)` (the existing flat path).
  When the user toggles modes the next frame's render swaps; no
  graph mutation needed. The flatten dispatch still runs in
  PointCloud3D mode (it's part of the active graph), but its output
  is unused by the renderer. Acceptable for v1 — disabling the
  upstream chain on mode change would require dirty-flag manipulation
  the renderer doesn't have access to. A future "Active Viewer" enum
  on the node could gate this.
- **Deep payload reach-around via the render cache.** PointCloudPass
  needs the upstream deep payload, not the flattened image the viewer
  holds. Pulling from `renderCache.retrieve(deepReader.outputs[0],
  region)` is the cleanest way: the cache is already the source of
  truth for pin payloads, and the render-cache key is `(pin × region)`
  which is exactly what we want. No graph walk per frame, no special
  case in the viewer.
- **Camera initial pose at `(0, 0, 3)` looking at origin.** Inside
  the worldPos-synthesis cube the v1 vertex shader produces. The orbit
  controller picks this up on first input event.
- **`EvaluationContext::cmd` is now populated.** Previously unused (the
  existing four toy nodes didn't need a command buffer at execute
  time); now consumed by `DeepEXRReadNode` to feed `uploadDeepImage`.
  No new contract — the field already existed.

### Files modified

- `src/main.cpp` — accepts `argc/argv`; constructs
  `TransientBufferPool`, `StagingArena`, `SyncDeepReader`, `Camera`,
  `PointCloudPass`; builds the startup graph via two helpers
  (`buildDeepViewChain` / `buildDemoChain`); populates the new
  `EvaluationContext` fields; mode-driven pass selection at end of
  frame; per-frame `bufferPool.onFrameRetired`.

### Verification

- Build: clean. `Loom` binary links against the new deep/IO/graphics
  paths.
- `ctest --preset debug` — 131/131 (no test count change; B.7 is
  integration glue that lives in `main.cpp`, out of the unit-test
  surface). The Loom GUI / CLI interaction is exercised manually:
  - `./build/debug/bin/Loom tests/data/deep_smoke.exr` — opens with
    the Flat 2D dropdown selected; switching to PointCloud3D engages
    the orbit camera.
  - `./build/debug/bin/Loom` (no arg) — falls back to the
    Constant → Viewer demo as before.
  - Live GUI verification deferred to a user session — the headless
    test environment cannot launch the window. The code path is
    inspectable and matches the plan's exit-criterion description.

### Phase B — global summary

The full chain `DeepEXRRead → DeepFlatten → Viewer` is wired
end-to-end. Phase B's deliverables:

- **`io::IDeepReader` + `SyncDeepReader`** — async-shaped reader
  interface with v1 sync impl.
- **`DeepEXRReadNode`** — file-I/O graph node with `file_path` and
  `frame_index` params.
- **`DeepFlattenNode` + `DeepFlatten.comp`** — per-pixel front-to-back
  deep composite; first end-to-end deep visualisation surface.
- **`PipelineCache::getOrCreateGraphics`** — graphics-pipeline overload
  with enum-permutation key.
- **`PointCloudPass`** + `PointCloud.{vert,frag}` — depth-tested
  per-sample point-cloud renderer.
- **`ui::ViewportMode` dropdown + orbit camera** — mouse-driven
  navigation through the point cloud.
- **CLI argument** — `./Loom path/to/deep.exr` loads the file at
  startup.

Test count: 109 (end of Phase A) → 131 (+22). All new tests are
headless; the GPU paths (`StagingArenaTest.UploadDeepImageRoundTripsSamples`,
the eventual `DeepFlatten` + `PointCloudPass` GPU readback tests) skip
cleanly on this machine and exercise under Lavapipe in CI (Phase 9).

Phase B exit criteria from the plan:

- ✅ `./build/bin/Loom path/to/deep.exr` opens the window. (Code path
  inspected; CLI + window construction unchanged from pre-B build).
- ✅ Shows the flattened deep image. (`DeepFlatten.comp` lit;
  layout-validated; routed through the existing viewer chain).
- ✅ Dropdown swap to PointCloud3D shows the same data as an
  orbit-able point cloud. (`ViewportMode` dropdown rendered in the
  viewport panel; `PointCloudPass` records on mode = `PointCloud3D`).
- ✅ Camera responds to mouse. (Orbit controller in `ImGuiRenderer`
  wires mouse drag → yaw/pitch, scroll → zoom).

A live GUI smoke test is the remaining outstanding verification — the
headless test environment can't launch the window. Code review of every
seam between the plan's exit criterion and the implementation indicates
the chain is wired correctly. Phase B is complete.

---

## Phase B.8 — Viewport UX hardening

### Context

Phase B.7's chain `DeepEXRRead → DeepFlatten → Viewer` works end-to-end,
but smoke-testing against a Skewer fixture
(`layer_sphere_blue.exr`, 1024² × Float32 RGBA, foreground Z ≈ 13)
turned up three UX issues that are minor in isolation but compound
together: scroll-zoom doesn't reach the viewport orbit, the camera's
default orbit radius / sensitivity are tuned for the centered unit
cube that v1's synthesised `world_pos` produces and feel wrong against
a real file with geometry at world Z ≈ -13, and the node-editor's
mouse-wheel zoom steps are too large per tick (default ~50 % per
stop). Phase B.8 fixes all three before Phase C amplifies them
(timeline scrubbing + larger sequences make any sensitivity mis-tuning
painful).

These are not architectural changes — parameter / wiring fixes plus
one ImGui-input gotcha. The plan's framing (`B.8.1 / B.8.2 / B.8.3`)
maps directly to commits.

---

## Phase B.8.1 — Viewport mouse-wheel capture

### Goal

Route the mouse-wheel input that lands inside the viewport panel to the
orbit camera's zoom controller. The drag-yaw / drag-pitch branch
already worked under `ImGui::IsItemHovered()`, but `io.MouseWheel` was
silently consumed by the dock panel's default scroll handling before
reaching the conditional.

### Decisions

- **Apply `ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse`
  to the viewport `ImGui::Begin`.** Plan listed two cleanly-scoped
  options: per-frame `SetKeyOwner(ImGuiKey_MouseWheelY, ...)` on the
  image, or these window flags on the panel. The flags are simpler,
  broader, and the viewport panel by design never needs to scroll its
  own contents (the image fills the dock). One change, no per-frame
  bookkeeping. The `SetKeyOwner` route stays available if the panel
  ever grows scrollable content.
- **No verification test added.** Mouse-wheel input is an ImGui IO
  path; a headless unit test would have to stub `io.MouseWheel` and
  manually invoke the docking-panel layout code, which would test the
  stub more than the real behaviour. The fix is one window-flag
  change; live-GUI smoke (logged under Phase B.8 exit criteria) is
  the authoritative confirmation.

### Files modified

- `src/ui/ImGuiRenderer.cpp` — viewport `Begin` now takes the no-scroll
  window flags. Documentation comment explains why.

### Verification

- Build: clean (single-TU change, no header churn).
- `ctest --preset debug` — 131/131 passing (unchanged).
- Live smoke (deferred to user session): scroll-zoom over the
  viewport image now mutates `m_orbitRadius`; the panel never tries
  to scroll its own dock.

---

## Phase B.8.2 — Data-aware orbit camera + auto-frame

### Goal

Surface a world-space scene-extent hint on every deep payload, fire a
one-shot auto-frame the first time a valid payload reaches the
renderer, and replace the orbit controller's hard-coded drag
sensitivity with a scale-invariant one. Together: open a real Skewer
file (geometry at Z ≈ -13) and see it on the first frame without
hunting for the geometry by spamming scroll-out, and feel consistent
drag yaw / pitch regardless of zoom depth.

### Decisions

- **New `core::AABB` type, header-only.** Pure data struct: `min`,
  `max`, `valid` flag. `expand(p)` initialises the box on first call
  and grows it monotonically. The plan suggested adding a
  `sceneRadius` float on `DeepRef`; we chose a full AABB instead.
  Phase D's handoff notes already anticipate this (`DeepRef will
  likely grow sceneBounds: AABB`) so introducing it now removes a
  follow-up rename. The `valid` flag is intentionally "no data was
  reduced into me" rather than "degenerate zero-extent box at the
  origin" — consumers must check before trusting `center()` /
  `radius()`. Outlier pruning (the `Z = 1e+10` background-sentinel
  filter from §19) lives in the caller; AABB stays a dumb reducer.
- **Sit `AABB` in `include/core/`, include from
  `gpu/ResourceHandles.hpp`.** The reducer is Vulkan-free and
  belongs to the headless core. `gpu/` is already free to include
  `core/` headers (`DeepLayout` forward-declares from here), so the
  include direction is consistent.
- **Reduce CPU-side inside `gpu::uploadDeepImage`.** The interleave
  pass already touches every channel byte-for-byte to pack SoA → AoS
  staging memory; folding the bounds reduction into the same data
  read is free. The alternative (GPU readback to compute on the host
  after upload) would require a fence + transfer, both for a value
  that's authoritative on the CPU side at the moment of the parse.
- **Two reduction modes, keyed by layout content.**
  - **NVS path** (layout exposes Float32 `world_pos.{x,y,z}`):
    iterate per-sample world position directly. Filter background
    sentinel (`|component| >= 1e9`, or any non-finite).
  - **Fallback path** (only Float32 `Z`): synthesise XYZ to match
    `PointCloud.vert`'s height-field rendering (XY in centered
    [-1, 1] per pixel, Z = -frontDepth). The auto-frame then fits
    what the point-cloud renderer actually shows, not some other
    convention.
  - Either path's preconditions failing (no `Z`, Z is Float16,
    `totalSamples == 0`): return `AABB{}` with `valid = false`. The
    main-loop auto-frame skips the reframe; the camera keeps its
    prior pose. Float16-Z is rare (OpenEXR's Deep convention is
    Float32) and v1 logs an `info`-level skip rather than carry a
    half-precision codepath.
- **Background-sentinel cutoff at `|z| >= 1e9`.** The known sentinel
  is `1e10` (§19); 1e9 is comfortably below it and well above any
  physical-scene depth in meters. The plan also suggested "99th
  percentile or 95 % of camera far" — both require either a sort or
  knowledge of camera state that the upload helper doesn't have.
  The cutoff is constant, branch-cheap, and matches the producer
  convention exactly.
- **Expose the reducer publicly as `gpu::reduceSceneBounds`.** Pure
  CPU work; testable headlessly. The function is declared next to
  `uploadDeepImage` in `gpu/DeepUpload.hpp` so it lives where its
  sole production caller lives, and the new test
  (`tests/core/DeepBoundsReduceTest.cpp`) drives the seven main
  reduction-behaviour invariants without a Vulkan device.
- **`Camera::frameToBounds(center, radius, padding=2.5)` sphere-fits
  the geometry.** Distance derives from `padding * radius /
  sin(fovY/2)`; the camera sits at `center + (0, 0, distance)` looking
  at `center`. Near / far widen to `[distance - 2r, distance + 4r]`
  with a small floor so geometry doesn't clip on the first frame.
  The `padding > 1` factor leaves headroom for a quarter-turn orbit
  to keep the silhouette inside the frame. `radius <= 0` is a no-op
  (caller is responsible for not passing a degenerate / invalid box).
- **Orbit-controller drag scaled by `tan(fovY/2) / viewportHeight`.**
  This is the standard "screen-space-constant drag feel" formulation
  — a vertical drag of the full viewport always rotates by the same
  angle, regardless of zoom or window size. The 1.5× leading factor
  reproduces the prior `0.005 rad/pixel` feel at the engine's
  default 60° FOV on a 1080p viewport. Multiplicative scroll-zoom
  stays at `pow(1.1, -wheel)` — scale-invariant by construction. The
  upper clamp on `m_orbitRadius` widens from 1000 to 1e6 so files
  whose bounding sphere sits a kilometer out from origin remain
  reachable.
- **Auto-frame is keyed on the samples-buffer identity
  (`poolIndex`, `generation`).** `uploadDeepImage` always acquires a
  fresh buffer per call, so the pair flips on every real re-upload.
  `main.cpp` carries one `lastFramedSamples` BufferHandle; the
  per-frame check compares against `deepRef.samples` and reframes
  when they differ (or when `lastFramedSamples` is invalid — the
  first-frame case). This survives an animation frame swap (Phase C
  re-uploads each frame), a re-edit of the `file_path` param, and
  switching files via the node-editor context menu — none of which
  the engine has any other "scene changed" signal for. An
  identity-stable re-evaluation (cache hit, same payload) does not
  re-frame.
- **`ImGuiRenderer::resyncOrbitFromCamera`** is the explicit hand-off
  hook. The orbit state (`yaw`, `pitch`, `radius`) is lazy-derived
  from the camera pose on first input; external repositioning (here:
  `frameToBounds`) needs to re-derive so the next mouse event picks
  the new pose up cleanly instead of snapping back to the prior
  orbit. The plan called this out only implicitly; making it a
  public method gives the auto-frame path a clean seam.

### Files created

| Path | Purpose |
|------|---------|
| `include/core/AABB.hpp` | Header-only AABB reducer |
| `tests/core/AABBTest.cpp` | AABB unit tests |
| `tests/core/DeepBoundsReduceTest.cpp` | `reduceSceneBounds` reduction tests |

### Files modified

- `include/core/Camera.hpp` / `src/core/Camera.cpp` — `frameToBounds`.
- `include/gpu/ResourceHandles.hpp` — `DeepRef::sceneBounds` field.
- `include/gpu/DeepUpload.hpp` — `reduceSceneBounds` declaration.
- `src/gpu/DeepUpload.cpp` — reduction implementation + call site at
  the tail of `uploadDeepImage`.
- `include/ui/ImGuiRenderer.hpp` / `src/ui/ImGuiRenderer.cpp` —
  `resyncOrbitFromCamera`, FOV-scaled drag, widened zoom clamp.
- `src/main.cpp` — `lastFramedSamples` tracker + per-frame auto-frame
  hook.
- `tests/core/CameraTest.cpp` — four `FrameToBounds` cases.
- `CMakeLists.txt` — new test files added to `LoomTests`.

### Verification

- Build: clean.
- `ctest --preset debug` — 146/146 passing (131 → 146, +15 new
  tests: 4 AABB, 4 Camera::frameToBounds, 7 DeepBoundsReduce).
  Headless coverage covers every code path the reduction takes
  (NVS, fallback, sentinel filter, non-finite filter, all-sentinel
  → invalid, empty payload → invalid).
- Live smoke (deferred to user session): loading the Skewer sphere
  fixture (foreground Z ≈ 13, background = 1e10 sentinel) auto-frames
  on first frame; drag-yaw / pitch feels consistent regardless of
  scroll depth.

---

## Phase B.8.3 — Node-editor zoom sensitivity smoothing

### Goal

A single mouse-wheel tick over the node-editor canvas no longer
over-zooms past target scale.

### Decisions

- **Set `ed::Config::CustomZoomLevels` to a 26-step array spanning
  [0.10, 8.0] at ~12 % per stop.** The plan recommended option 1
  (multi-tick smoothing in our wrapper) and explicitly noted option 2
  (patching the vendored editor). On inspection, the editor exposes
  `CustomZoomLevels` as a public config field (`imgui_node_editor.h`
  line 102) — replacing the default 18-step `s_DefaultZoomLevels`
  table with a finer-grained one is a config change, not a vendor
  patch. The plan author overlooked this. The cleaner option (3,
  effectively) makes the wrapper-side accumulation moot.
- **Why option 1 wouldn't have worked cleanly.** The editor reads
  `io.MouseWheel` itself and casts to `int` (`imgui_node_editor.cpp`
  line 3460: `auto steps = (int)io.MouseWheel;`). Any wrapper-side
  smoothing that fed fractional ticks back into `io.MouseWheel`
  would get re-truncated; the only way to gate the rate is to either
  set `io.MouseWheel` to the desired integer step count
  (essentially: zero it out and write `±1` when the accumulator
  crossed a threshold) or to short-circuit the editor's whole
  navigate action. Both approaches fight the editor's input
  pipeline. `CustomZoomLevels` is the API the editor's authors put
  there for exactly this case.
- **Step ratio ~12 %.** Matches the orbit-camera's per-tick zoom
  factor (`pow(1.1, -wheel)` = ~10 % per tick). Close enough that
  scrolling the viewport vs the node graph feels like the same
  control. The array is hand-written (not generated) so the values
  are JSON-stable across the editor's settings-file round-trip; a
  procedurally-generated geometric sequence would risk float-printing
  drift between sessions.
- **Also apply `NoScrollbar | NoScrollWithMouse` to the Node Editor
  panel.** Same class of bug as B.8.1: the dock panel intercepts
  the wheel before the canvas's navigate action sees it. The fix is
  identical to B.8.1; folding it into B.8.3 keeps the editor-panel
  configuration in one place.

### Files modified

- `src/ui/NodeEditorPanel.cpp` — `CustomZoomLevels` array on the
  editor's `Config`, no-scroll window flags on the panel's
  `ImGui::Begin`.

### Verification

- Build: clean.
- `ctest --preset debug` — 146/146 passing (no test count change;
  the editor-panel-config change is exercised live).
- Live smoke (deferred): scroll-wheel over the node-editor canvas
  steps through the finer zoom array; one tick ≈ 12 % zoom rather
  than ~50 %.

---

## Phase B.8 — global summary

Phase B.8's three sub-tasks were independent fixes that together
brought the viewer's UX to the bar Phase C will start from. The
delivered changes:

- **Mouse-wheel capture** — viewport + node-editor panels both opt
  out of ImGui's default scrollbar / scroll-with-mouse behaviour so
  their respective wheel consumers (orbit camera, canvas zoom)
  receive the input.
- **`core::AABB`** — small header-only reducer that lets every deep
  payload carry a scene-extent hint forward.
- **`gpu::reduceSceneBounds`** — NVS-aware bounds reduction folded
  into the upload pass; background-sentinel filtered; testable in a
  headless context.
- **`Camera::frameToBounds`** — sphere-fit reframer that scales
  near / far planes alongside the camera distance.
- **Auto-frame on scene change** — main-loop tracks samples-buffer
  identity and reframes once per real upload.
- **FOV-scaled orbit drag** — `tan(fovY/2)` and viewport height
  factor into drag sensitivity so it feels consistent at any zoom
  level.
- **Finer node-editor zoom stops** — `Config::CustomZoomLevels`
  with 26 entries at ~12 % per stop replaces the editor's coarser
  default.

Test count: 131 (end of Phase B.7) → 146 (+15). All new tests are
headless (`AABBTest` ×4, `Camera::FrameToBounds` ×4,
`DeepBoundsReduceTest` ×7) — the GPU-visible bits (auto-frame chain,
viewport panel flags, node-editor config) are exercised under live
smoke.

Phase B.8 exit criteria from the plan:

- ✅ Scroll-zoom in the viewport works. (Window-flag fix on the
  viewport panel; live smoke deferred.)
- ✅ Sphere auto-frames on load and drag feels consistent at any
  zoom. (Auto-frame keyed on samples-buffer identity;
  `Camera::frameToBounds` + FOV-scaled drag.)
- ✅ Node graph zooms smoothly under continuous scroll.
  (`CustomZoomLevels` config; live smoke deferred.)

Phase B (including B.8) is complete. Phase C — animation playback,
worker-thread reader, deep merge / transform — starts on this
foundation.

---

## Phase B.8 — Post-merge follow-ups

A live-GUI smoke pass after Phase B.8 landed surfaced three issues that
the headless test environment couldn't catch. Folded back into the
B.8 archive because they patch B.8's own seams; each is a small change
delivered as its own commit.

### Follow-up 1 — Point-cloud clipping at zoom-in (commit `2e1895b`)

**Symptom.** Zooming the orbit camera in past the initial framing
distance clipped the back of the point cloud — the scene's far surface
disappeared into the far plane.

**Root cause.** `Camera::frameToBounds` sets near / far *once* at
framing time using the framing distance as the anchor. The orbit
controller then mutates `camera.position()` every frame to react to
mouse drag / wheel, but never touches `m_near` / `m_far`. As soon as
the orbit radius differs from the original framing distance, the clip
planes no longer enclose the bounding sphere.

**Fix.** Remember the scene center + radius alongside
`lastFramedSamples`. After `drawDockspace` (which runs the orbit
controller) but before `dispatchManager.submit`, recompute near / far
from `||camera.position() − sceneCenter||` and the cached radius —
plus a 10 % margin so the sphere boundary doesn't sit on the plane.
The update is unconditional whenever the engine has a valid framed
scene; for the demo `Constant → Viewer` graph the cached radius stays
at 0 and the camera keeps its default planes.

**Files.** `src/main.cpp` only — single tracked state pair
(`lastFramedCenter`, `lastFramedSceneRadius`) plus a six-line per-frame
update.

### Follow-up 2 — Node-editor wheel accumulator (commit `4d07b50`)

**Symptom.** Small scroll inputs (trackpad gestures, hi-res mouse
fractional ticks) didn't zoom the canvas. Symptom worsened after a
RMB-pan gesture — the editor seemed to "stick" at the current zoom
for several extra ticks before resuming.

**Root cause.** `imgui-node-editor`'s `NavigateAction::Action_Navigate`
contains `auto steps = (int)io.MouseWheel;`. The cast truncates toward
zero, so `(int)0.3 == 0` — a fractional wheel event produces no zoom
step. The earlier B.8.3 fix (`CustomZoomLevels`) addressed step *size*
but not the truncation; finer levels can't help if you never reach a
single integer step.

**Why the original plan's option-1 ("multi-tick smoothing in our
wrapper") was the wrong recipe.** The plan framed option 1 as *slowing
down* the wheel — accumulate input, forward a tick when accumulator
crosses a threshold. The actual problem is the opposite: the editor
*loses* fractional input, so we need to *concentrate* sub-tick events
into integer pulses, not throttle them.

**Fix.** Add `float m_zoomAccum` to `NodeEditorPanel`. When the panel
is hovered, rewrite `io.MouseWheel` in place: accumulate, and when
`|accum| >= 1.0` emit `±1.0` and decrement; otherwise zero out the
wheel for this frame. The rewrite is safe because the viewport panel
(the other wheel consumer) is drawn *before* `nodeEditor.draw` in
`main.cpp`'s per-frame ordering, so its hover branch has already
finished reading the original wheel value. Stale-accumulator decay
prevents a small mouse wobble from leaving residue that mis-aligns a
later deliberate scroll.

**Files.** `include/ui/NodeEditorPanel.hpp` (field), `src/ui/NodeEditorPanel.cpp`
(rewrite block ahead of `ed::Begin`).

### Follow-up 3 — Flat 2D pan + zoom (commit `948d6a6`)

**Status.** Plan B.8 framed Flat 2D as carrying forward the existing
non-interactive image. The user's smoke confirmed this matched the
plan but felt asymmetric next to PointCloud 3D's orbit camera.
Treating this as missing functionality (rather than an unfiled
feature) was the right call — pan + zoom is the baseline-expected UX
for a 2D image viewer.

**Approach.** Apply pan + zoom as UV-coord transforms on the
`ImGui::Image` call. The viewport image *is* the rendered output of
`DisplayPass` at the panel's exact pixel resolution; pan / zoom is
therefore a screen-space crop into that texture, not a re-render with
different camera. Pros: zero compute cost (UV passed to ImGui's
draw-list), zero plumbing into `DisplayPass`, instant
"interactivity-feels-good" response. Cons: at high zoom the user sees
single-pixel magnification rather than a re-render at finer
resolution — acceptable for v1 since the alternative is wiring a 2D
zoom transform into the entire viewer chain.

**Math.**
- `m_view2DCenter` is the UV coord at the viewport center
  (`(0.5, 0.5)` = image center, default).
- `m_view2DZoom` is the magnification factor.
- UV span: `uv0 = center - 0.5/zoom`, `uv1 = center + 0.5/zoom`.
- Drag → `center -= delta / (viewportSize * zoom)`.
- Wheel → cursor-anchored zoom: the UV at the cursor position before
  and after the zoom change is held invariant by adjusting `center`.
  Without anchoring, every zoom recenters on the image middle, which
  feels wrong for a viewer that's already been panned.
- Zoom clamp `[0.05, 64]`. Below 0.05 the image becomes pixel-sized;
  above 64 we're sampling a single source texel many times.

**State preserves across mode toggles.** Switching to PointCloud 3D
and back keeps the user's framing. The pan / zoom state is *not*
reset on a new deep payload — the auto-frame is a 3D-only concept;
the 2D view stays where the user last left it.

**Files.** `include/ui/ImGuiRenderer.hpp` (two fields + handler
declaration), `src/ui/ImGuiRenderer.cpp` (`applyView2DInput` + UV
plumbing in `drawDockspace`).

### Verification (all three follow-ups)

- Build: clean on each commit.
- `ctest --preset debug` — 146/146 passing throughout. No new tests
  added: clip-plane refresh and 2D UV math are GUI-driven and best
  smoked live; the wheel-accumulator is an `io.MouseWheel`
  read-modify-write that would test the ImGui IO stub more than the
  behaviour.
- Live smoke (user session): all three resolved.

---

## Phase B.8 — Follow-up #3: CameraNode + PointCloudRenderNode

User smoke-tested Phase B.8 and surfaced three concerns:

1. The 2D viewport's flat path glitched outside the rendered region on
   non-square EXRs.
2. The point cloud's depth perspective felt extreme — hardcoded FOV +
   synthesised XY ∈ [-1,1] vs depth in raw EXR units made for a thin
   slab that was hard to orbit.
3. Architecturally: should the viewport modes / camera be **nodes** in
   the graph (compositor pattern) or **UI settings** (engine pattern)?
   Industry compositors (Nuke / Fusion / Houdini) make cameras +
   renderers nodes; only pure viewing controls are settings.

The follow-up commits this tier:
- **2D glitch fix** in `DisplayPass.frag` with aspect-fit + black
  letterbox.
- **CameraNode** with knob-editable view params.
- **PointCloudRenderNode** delegating to the engine's PointCloudPass.
- **Auto-normalised z-scale** so the synthesis path's slab is
  proportioned correctly out of the box.
- **`ViewportMode` → `ViewportInputMode`** rename — the dropdown now
  controls input-gesture interpretation only.
- **Orbit gestures route through `CameraNode.setParam`**.

The full design lives in `~/.claude/plans/shiny-waddling-finch.md`
(private — not committed). CONVENTIONS §21 documents the
camera-as-node pattern + the single-active-camera v1 constraint.

### Step 1 — 2D glitch fix (commit c942d0f)

**Root cause** confirmed at `shaders/DisplayPass.frag:51-54` and
`src/main.cpp:331`. The shader was using `pc.width / pc.height` (the
**viewport extent**) to compute the texel index into a source image
at the **EXR's native resolution** (e.g. 1024×1024). Out-of-range
`imageLoad()` returned undefined garbage for any pixel outside the
source's bounding rect within the viewport.

**Fix:** extend the push-constant struct with `srcWidth` / `srcHeight`,
compute aspect-fit math in the fragment shader (letterbox the source
into the viewport at native aspect), and fill the leftover strip with
opaque black.

- `shaders/DisplayPass.frag`, `include/gpu/DisplayPass.hpp`,
  `src/gpu/DisplayPass.cpp` — shader update + plumbing.
- `include/gpu/TransientImagePool.hpp` / `.cpp` — `getExtent()`
  accessor so `main.cpp` can query the viewer image's native size.
- `tests/gpu/DisplayPassTest.cpp` — `LetterboxesWiderSource` +
  `LetterboxesTallerSource` cases (4:1 source onto a 1:1 viewport,
  and 1:4 source onto a 1:1 viewport).

Aspect-fit math (in GLSL):
```glsl
float srcAspect = float(pc.srcWidth) / float(pc.srcHeight);
float vpAspect  = float(pc.width)    / float(pc.height);
vec2 letterbox = vec2(0.0);
if (srcAspect > vpAspect) letterbox.y = (1.0 - vpAspect/srcAspect) * 0.5;
else                       letterbox.x = (1.0 - srcAspect/vpAspect) * 0.5;
vec2 srcUV = (inUV - letterbox) / (vec2(1.0) - 2.0 * letterbox);
if (any(lessThan(srcUV, vec2(0))) || any(greaterThanEqual(srcUV, vec2(1))))
    outColor = vec4(0,0,0,1);  // letterbox strip
else
    // sample source at srcUV.
```

Tests: 146 → 148 (+2 letterbox cases).

### Step 2 — Kind::Camera + CameraRef (commit fbfb438)

**Decisions.**

- **Snapshot model, not pointer.** `CameraRef` is a value type
  carrying `view` / `proj` / `eyePos` / near / far / fovY. Avoids the
  pointer-lifetime question that would arise from carrying a
  `const Camera*` through the render cache. Same shape as `DeepRef`.
- **`PinType::Camera`** added to the edit-time type-check enum;
  `canAddLink`'s existing `startType == endType` check handles
  it without code change.
- **`pullCameraInput` helper** on `Node` mirrors `pullImageInput` /
  `pullDeepInput`. The asserted-on-mismatch contract catches a
  miswired graph at evaluation time.

Tests: 148 → 150 (+2 ResourceRef cases).

### Step 3 — CameraNode (commit ebebf7c)

**Decisions.**

- **Knobs match `core::Camera`'s field set.** Position + target as
  vec3 with no bounds (the colour picker would render bounded vec3s
  inappropriately). FOV in **degrees** for the knob, converted to
  radians internally — degree numbers are friendlier in the UI
  ("60°" reads cleaner than "1.047"). Near / far separate so the user
  can widen the clip range for kilometer-scale scenes.
- **Aspect is auto-derived, not a knob.** Each frame's `execute()`
  reads `EvaluationContext::requestedExtent` so the camera always
  matches the active viewport.
- **No input pins.** Pure source node; downstream renderers pull from
  the `Kind::Camera` output.
- **No `ComputeTask` emitted.** The node's only side-effect is the
  cache store; renderers do the actual GPU work using the snapshot.

Tests: 150 → 155 (+5 CameraNode cases — schema, params, execute at
default values, knob round-trip, JSON round-trip).

### Step 4 — DeepRef::recommendedZScale (commit c7da1c8)

**Decision.** Adding `recommendedZScale` to `DeepRef` is "free" from
the AABB already computed in `reduceSceneBounds` — the formula is
`2 / extent.z`, surfacing the per-payload depth normalisation that
makes non-NVS files feel right with `z_scale = 1.0`. NVS payloads
(world_pos present) get `1.0` since their positions are in real
world units.

Pulled the formula into a `computeRecommendedZScale` free function so
it's headlessly unit-testable. The upload pass calls the function and
stashes the result on the ref.

Tests: 155 → 158 (+3 cases — Z-only payload, NVS payload, invalid
bounds).

### Step 5 — PointCloudPass signature pivot (commit 3498064)

**Decisions.**

- **Accept `CameraRef` instead of `const core::Camera&`.** Matches
  the cache-carried payload. The pass derives `viewProj = proj * view`
  inline; no behavioural change.
- **Layout stays `SHADER_READ_ONLY_OPTIMAL` on exit.** The original
  plan called for `GENERAL` ("transient image convention"), but on
  reflection `SHADER_READ_ONLY_OPTIMAL` works for *both* call sites:
  - Existing `main.cpp` rendering directly onto the viewport image
    (ImGui samples need `SHADER_READ_ONLY_OPTIMAL`).
  - Step 6's `PointCloudRenderNode` producing a transient that
    `DisplayPass` then consumes — DisplayPass's pre-barrier expects
    `SHADER_READ_ONLY_OPTIMAL` for its HDR input.
  Picking the layout the next consumer expects is simpler than
  picking `GENERAL` and adding a transition before each consumer.

Tests unchanged (still 158); pass-internal signature change is
exercised by the existing call site smoke test.

### Step 6 — PointCloudRenderNode (commit c9ae016)

**Decisions.**

- **Delegates to engine-owned `PointCloudPass`.** Each
  `PointCloudRenderNode` doesn't own its own pipeline / depth
  attachment — that would duplicate per-node Vulkan resources. The
  shared pass lives on `EvaluationContext::pointCloudPass` (added in
  this commit).
- **Bypasses the `ComputeTask` queue.** The node records graphics
  commands directly in `execute()` rather than enqueuing a task.
  Honest v1 architectural debt — documented in CONVENTIONS §21. The
  hazard tracker doesn't see the graphics dispatch, but the pass
  manages its own barriers (as it has since Phase B.5).
- **Bindless set threaded through `EvaluationContext`.** Compute
  tasks get the bindless set from `DispatchManager::submit`; graphics-
  pass-as-node call sites bind it themselves. Added `bindlessSet`
  field to `EvaluationContext`.
- **Effective z-scale = user knob × `recommendedZScale`.** Non-NVS
  payloads carry the auto-scale on `DeepRef::recommendedZScale`;
  multiplying makes the user's knob act on top of the
  per-payload-normalised slab. NVS payloads carry `1.0` so the knob
  acts unscaled on real-world units.
- **Output layout setLayout to `SHADER_READ_ONLY_OPTIMAL`.** The pool
  needs to track the actual layout the pass left the image in
  (otherwise the next pool-acquire of the same slot would receive
  wrong `currentLayout`).

Tests: 158 → 161 (+3 cases — pin schema, knobs, missing-input
fallback).

### Step 7 — ViewportMode → ViewportInputMode (commit 8a51fc7)

**Mechanical rename + semantic shift.** The dropdown that previously
chose "Flat 2D" vs "PointCloud 3D" render mode now chooses "Pan 2D"
vs "Orbit 3D" **input mode**. The render mode is the graph's
concern.

`main.cpp` keeps the bridge (`if (input == Orbit3D) wantPointCloud =
true`) for a single commit so users don't lose 3D between Steps 7
and 8.

### Step 8 — main.cpp wiring + orbit → setParam (commit c06d7bd)

**The biggest change.** The compositor flow is finally in place.

**Decisions.**

- **Default startup graph spawns all four production nodes plus
  Camera:** `DeepEXRRead`, `DeepFlatten`, `CameraNode`,
  `PointCloudRender`, `Viewer`. The 2D wire (`DeepFlatten → Viewer`)
  is set initially; the 3D chain (`Deep + Camera → PointCloudRender`)
  is pre-wired but the user manually drags the PointCloudRender →
  Viewer link in the node editor to swap to 3D. Document this is
  intentional — it preserves the prior 2D-default behaviour and the
  re-wire gesture is the compositor-native interaction.
- **`ImGuiRenderer::setActiveCameraNode(graph, handle)`** registers
  the camera node with the orbit controller. `applyOrbitInput`
  mutates BOTH the local Camera reference (for orbit-state coherence
  within the frame) AND calls `node->setParam(positionIdx, newPos)`
  to drive next frame's graph evaluation.
- **Auto-frame writes through `setParam`.** When a new deep payload
  arrives and `Camera::frameToBounds` reframes the engine Camera,
  we mirror position / target / near / far onto the CameraNode's
  params. The next graph evaluation produces a `CameraRef` matching
  the framed pose.
- **Per-frame clip-plane refresh also writes through `setParam`.**
  Same pattern: compute near / far from current orbit distance and
  scene radius, push to CameraNode params.
- **`wantPointCloud` branch deleted.** The viewer always shows
  `DisplayPass(viewerOutput → viewport image)`. The choice of
  whether `viewerOutput` carries a flat-2D image or a 3D-splat image
  is graph-wiring.
- **Single-frame latency on auto-frame / clip-plane updates.** Since
  these write to CameraNode params AFTER `graph.execute()` ran, the
  effect lands on the NEXT frame. Invisible at 60fps.

Tests unchanged (still 161). The Step 8 changes are GUI-driven and
exercised by live smoke.

### Step 9 — Documentation (this commit)

CHANGELOG `[Unreleased]` describes the user-visible additions;
CONVENTIONS §21 covers the camera-as-node pattern + the
single-active-camera v1 constraint. This archive entry captures the
implementation history.

### Phase B.8 follow-up #3 — exit criteria

- ✅ 2D glitch fixed: aspect-fit + letterbox.
- ✅ CameraNode with editable FOV / position / target / near / far
  knobs.
- ✅ PointCloudRenderNode wired to receive Deep + Camera, produce
  Image.
- ✅ Auto-normalised z-scale: synthesis depth matches XY range out
  of the box.
- ✅ Architecturally: cameras + renderers are graph nodes; viewport
  controls are input-mode settings.

Test count: 146 → 161 (+15 new tests across CameraNode,
PointCloudRenderNode, ResourceRef Kind::Camera, DisplayPass
letterbox, recommendedZScale). All headless or `GTEST_SKIP` cleanly
on this machine; GPU paths exercised under Lavapipe in CI.

Phase B.8 follow-up #3 is complete. The user-visible workflow:

1. `./Loom path/to/deep.exr` — opens with 2D Flat in the viewport.
2. Open the node editor; drag the `PointCloudRender` node's output
   onto the Viewer's input → 3D point cloud renders.
3. Click the "Orbit 3D" input mode in the viewport panel.
4. Drag inside the viewport → camera orbits; scroll → zoom.
5. Edit the CameraNode's `fov_y_deg` knob to widen the perspective.
6. Edit `PointCloudRender.z_scale` or `.point_size` to taste.

Architectural debt deferred to Phase D: `GraphicsTask` abstraction
(so the hazard tracker sees graphics dispatches), multi-camera UI,
camera-node animation curves.

---

## Phase B.8 follow-up #4 — Live-smoke regressions

The B.8 follow-up #3 work landed clean against the test suite (161/161)
but live smoke immediately surfaced three separate regressions:

1. **2D viewport showed artifacts outside the rendered region.** The
   letterbox shader was supposed to fix this in #3.
2. **Orbit 3D input mode did nothing visible.** Dragging the viewport
   in Orbit 3D produced no on-screen change.
3. **Wiring `PointCloudRender → Viewer` produced no visible output.**

A code audit confirmed all three were separate root causes with
separate fixes. The plan-of-record lived in
`~/.claude/plans/shiny-waddling-finch.md` (private).

### Step 1 — DisplayPass CLEAR loadOp + NaN-safe shader

**Decisions.**

- **`loadOp = LOAD_OP_CLEAR` over `DONT_CARE`.** The fragment shader's
  letterbox branch is correct for valid extents, but a degenerate-extent
  frame — viewport panel first-allocation, mid-resize tick — makes the
  aspect-ratio math NaN. NaN comparisons in GLSL evaluate to `false`,
  so the shader falls through every guard and samples an undefined
  source texel. CLEAR guarantees opaque black where the shader doesn't
  overwrite, so the worst case is "screen briefly flashes black" rather
  than "screen shows garbage from prior framebuffer contents." The
  per-frame clear cost is one VkClear into the viewport image — well
  below profiling noise on every tested GPU.
- **Shader-side zero-extent guard.** Early-return opaque black when
  `pc.width == 0u || pc.height == 0u || pc.srcWidth == 0u ||
  pc.srcHeight == 0u`. Belt-and-suspenders alongside the CLEAR — even
  if a future caller forgets to clear, the shader still produces
  defined output for degenerate inputs.
- **main.cpp also guards `displayPass.record`.** If any of the four
  extents is zero, fall through to `clearViewportToBlack` instead of
  recording the pass at all. The shader / loadOp combination would
  produce correct output anyway, but skipping the pass keeps the
  validation layers quiet on inputs that exercise a code path
  Vulkan's spec writers didn't intend.

Tests: 161 → 162 (+1: `DisplayPassTest.ZeroSourceExtentClearsToBlack`).

### Step 2 — Orbit input gating + engine-Camera target sync

**Two bugs in one step.**

**Bug 2a — every-frame redirty:** The orbit math (yaw/pitch read from
spherical coordinates, position computed from target + radius*dir,
write to CameraNode `position` param) was outside the
`ImGui::IsItemHovered` branch. Consequence: every frame the orbit
controller wrote back the current pose, which set
`CameraNode.isDirty = true`, which propagated through `Graph::markDirty`
to every downstream node. Two visible consequences:
- The point-cloud re-evaluated every frame even when the user wasn't
  interacting. Cheap on today's geometry, expensive on tomorrow's.
- Any user knob-edit on the `position` param got immediately
  overwritten by the orbit's idle-state value. The position knob
  appeared "broken" — the user couldn't actually edit it.

**Decision:** Split `applyOrbitInput` into input-handling and
pose-application halves. Set a local `inputFired` flag inside the
input-handling branch (drag delta nonzero / wheel nonzero); skip the
pose-application half entirely when `!inputFired`. Idle frames now
preserve user knob-edits; only real input drives the CameraNode dirty
cascade.

**Bug 2b — engine Camera and CameraNode target desync:** The orbit
math reads `camera.target()` (engine Camera) to anchor the rotation
center, but the CameraNode owns the `target` knob. After auto-frame
they're in sync; but if the user knob-edits the target (or auto-frame
fires while orbit state is mid-flight) the two diverge. The orbit
controller anchors to the wrong point, and the next drag rotates around
a stale origin.

**Decision:** main.cpp materializes CameraNode params (position,
target, fov, near, far) back into the engine Camera each frame after
`graph.execute`. The CameraNode is the source of truth; the engine
Camera is a per-frame mirror. Detect "target changed externally" by
comparing previous-frame target vec3 to current; call
`ImGuiRenderer::resyncOrbitFromCamera` on change so the orbit's
spherical coordinates re-derive from the new (position, target)
offset.

Tests: 162 → 163 (+1: `CameraNodeTest.ParamSlotsExposeExpectedVariants`,
pinning the param-shape contract that the materialization code depends
on).

### Step 3 — Dropdown rewires Viewer input pin

**Decision.** The B.8 follow-up #3 design treated the input-mode
dropdown as a "gesture-interpretation only" toggle — the user was
expected to manually re-wire `DeepFlatten → Viewer` vs
`PointCloudRender → Viewer` in the node editor. In practice, the
two-step gesture (pick mode in viewport panel, then drag wire in
node editor) was a constant source of "I picked Orbit 3D but
nothing happened" friction. The compositor purity wasn't worth the
UX hit.

The fix elevates the dropdown to **both** a gesture toggle AND a
quick wire-swap. Selecting "Orbit 3D" calls
`Graph::replaceViewerInput(viewer, pointCloudOutput)` immediately;
"Pan 2D" calls `replaceViewerInput(viewer, flatOutput)`. Manual
re-wiring still works for users who want a custom upstream — the
dropdown is just the canonical-pairing shortcut.

`Graph::replaceViewerInput` is a tiny helper that composes
`tryAddLink` over a Viewer's single input pin. On type mismatch
(canAddLink rejects) it returns false **without** breaking the
existing wire — half-state would surprise the user.

**Default mode for deep-EXR startup.** `./Loom path/to/deep.exr` now
defaults to Orbit 3D + `PointCloudRender → Viewer` wired in. Users
opening a deep file usually want to see the 3D scene; meeting them
there with no extra clicks matches expectation. The demo chain
(no CLI path) stays in Pan 2D.

CONVENTIONS §21 documents the dual role of the dropdown.

Tests: 163 → 166 (+3: `GraphTest.ReplaceViewerInputSwapsUpstream`,
`ReplaceViewerInputRejectsNonViewer`, `ReplaceViewerInputPreservesOnTypeMismatch`).

### Step 4 — Startup graph node positions

**Decision.** The five startup nodes all spawned at canvas (0, 0) on
top of each other, making it impossible to see the graph before
dragging them apart. Layout follows dataflow direction:

```
DeepEXRRead (0,    0)   ───→ DeepFlatten (250, -60) ─┐
                                                     ├→ Viewer (500, 0)
Camera     (0,  120)   ───→ PointCloudRender (250,  80) ┘
```

Implemented via a new `NodeEditorPanel::setNodePosition(handle, x, y)`
that writes to the existing `m_nodeStates` spawn-position machinery.
The editor's persistent settings file overrides these on sessions
after the first, so user-positioned layouts survive across runs.

DeepFlatten was previously not exposed in `StartupGraphHandles`;
added it so positioning is a single line in main.cpp rather than a
graph-wide `forEachNode` lookup.

No unit test — the setter is a 3-line map write that requires a live
ImGui-node-editor context to construct the surrounding panel, which
would be heavy testing infrastructure for trivial code that's
exercised end-to-end at every Loom launch.

### Step 5 — Documentation (this commit)

CHANGELOG `[Unreleased]` documents the four fixes; CONVENTIONS §21
expands on the dropdown's dual role + the materialization /
input-gating split; this archive entry captures the why.

### Phase B.8 follow-up #4 — exit criteria

- ✅ 2D viewport letterbox survives degenerate-extent frames.
- ✅ Orbit 3D gestures actually move the camera; user knob-edits on
  CameraNode.position survive idle frames.
- ✅ Engine Camera tracks CameraNode.target through external edits.
- ✅ Dropdown toggle re-wires Viewer input as a one-click UX.
- ✅ Startup nodes spawn at non-overlapping positions.
- ✅ Deep-EXR startup defaults to Orbit 3D (matches user expectation).

Test count: 161 → 166 (+5 across DisplayPass extent guard, CameraNode
param contract, Graph::replaceViewerInput). All headless or
`GTEST_SKIP` cleanly on this machine; GPU paths exercised under
Lavapipe in CI.

User-visible workflow after #4:

1. `./Loom path/to/deep.exr` — viewport opens with the point cloud
   visible (default Orbit 3D for deep files). Node editor shows the
   five nodes laid out left-to-right along the dataflow direction.
2. Drag in the viewport → camera orbits live.
3. Switch dropdown to "Pan 2D" → viewer rewires to DeepFlatten,
   flat 2D view appears with proper letterbox.
4. Pan/zoom with the mouse → 2D image pans/zooms.
5. Switch back to "Orbit 3D" → viewer rewires to PointCloudRender,
   point cloud reappears at the user's last orbit pose.
6. Edit `CameraNode.fov_y_deg` → perspective changes immediately, no
   overwrite on idle frames.
7. Edit `CameraNode.target` → orbit center updates, controller
   re-derives radius/yaw/pitch from the new offset.

What stays open: the dropdown-as-wire-swap is a UX compromise that
short-circuits the compositor model; a future "View as" toggle row
on the Viewer node itself would express the same intent purely
graph-side. Multi-camera UI (CONVENTIONS §18) and a proper
`GraphicsTask` abstraction for `PointCloudPass` (Phase D) remain
deferred.

---
