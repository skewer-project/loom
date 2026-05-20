# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- OpenEXR + Imath as transitive `FetchContent` dependencies (pinned to
  `v3.2.4` / `v3.1.12`). Loom now reads and writes deep EXRs standalone from a
  fresh checkout; tools and tests are disabled in the OpenEXR sub-build to
  keep configure-time blast radius small.
- `tools/exr_spike` and `tools/make_deep_fixture` — throwaway Phase A.0
  command-line tools for deep-EXR inspection and committed-fixture generation.
- `tests/data/deep_smoke.exr` — 16×16 deep EXR fixture with the v1 channel
  set (`Z`, `ZBack`, `R`, `G`, `B`, `A`).
- `core::DeepLayout` — interned, hashable, ordered channel-list abstraction
  with per-channel byte offsets and packed-sample stride. Process-wide
  `getDeepLayout(channels)` returns content-addressed pointers so equal
  layouts compare by pointer equality.
- `ResourceRef::DeepRef` extended with `const core::DeepLayout* layout`;
  `ResourceRef::fromDeep` helper added for symmetry with `fromImage` / `fromBuffer`.
- `core::Camera` — RH/Y-up perspective camera with lazy-rebuilt view /
  projection matrices and Vulkan-correct clip-space (Y-flipped, Z in [0, 1]).
- `core::EvaluationContext` carries `const Camera* camera` and `uint64_t frame`
  threaded from the UI through graph evaluation.
- `glm` (1.0.1) added as a header-only `FetchContent` dependency for vector /
  matrix math.
- `core::Param` — generic per-node parameter tag-union over `float / int /
  bool / glm::vec3 / std::string`, with optional `ParamRange` bounds and JSON
  round-trip via the vendored `crude_json`.
- Nodes carry a `std::vector<Param>` and override `buildParams()` (called once
  from `Graph::addNode`). `Node::setParam` is the canonical mutation entry
  point and flips `isDirty`.
- `NodeEditorPanel` renders generic Param widgets (slider / drag / checkbox /
  color picker / text input) keyed off the variant tag.
- `gpu::StagingArena` — host-visible bump arena for CPU→GPU upload staging.
- `gpu::uploadDeepImage` — packs a CPU-side SoA `io::ParsedDeepImage` into
  the device-local AoS sample layout described by a `core::DeepLayout`,
  emitting count / offset image uploads and the samples-buffer copy with
  proper barriers.
- `io::ParsedDeepImage` — CPU-side parsed deep-image struct (the
  `IDeepReader::readFrame` output shape, ahead of Phase B.1).
- `io::IDeepReader` + `io::SyncDeepReader` — async-shaped reader interface
  (`std::future<DeepFrame> readFrame(path)`) with a v1 synchronous OpenEXR
  Deep implementation. Builds and interns a `core::DeepLayout` from the EXR
  header's channel list; returns an invalid `DeepFrame` on read failure
  rather than throwing.
- `LoomTests` carries a `LOOM_TESTDATA_DIR` compile-time define so tests
  resolve committed-fixture paths from the source tree regardless of cwd.
- `core::DeepEXRReadNode` — pure-source node with `file_path: string`
  / `frame_index: int` params and a single `Kind::Deep` output pin.
  Loads via `IDeepReader` and stages onto the GPU via `uploadDeepImage`.
- `core::EvaluationContext` extended with nullable `bufferPool`,
  `stagingArena`, and `deepReader` fields for nodes that need them.
- `core::Node::pullDeepInput` typed accessor mirrors `pullImageInput`
  for Deep-pin consumers.
- `core::DeepFlattenNode` — front-to-back deep composite via a new
  `DeepFlatten.comp` compute shader. Output is RGBA32F sized to the
  source deep image. v1 assumes the standard layout (Z + ZBack +
  Float16 RGBA, stride 16); a layout-flexible variant lands in Phase D.
- `gpu::ResourceRef::DeepRef` carries `width` / `height` so consumers
  can size their dispatch / draw extent from the payload directly.
- `core::buildDeepFlattenTask` task builder.
- `gpu::PipelineCache::getOrCreateGraphics(GraphicsPipelineKey)` —
  graphics-pipeline overload sharing the persistent `VkPipelineCache`
  with the existing compute path. Enum-permutation key (`VertexInputDesc`,
  `Topology`, `BlendMode`, `DepthMode`, color/depth format, samples,
  shaders, layout handle).
- `gpu::PointCloudPass` — depth-tested per-sample point-cloud renderer
  sibling to `DisplayPass`. Owns its pipeline layout, camera UBO, and
  lazy-resized depth attachment. Pulls per-sample data from the deep
  payload via `gl_VertexIndex`. v1 derives world position from source
  pixel + depth; Phase D.1 swaps to true `world_pos.{x,y,z}`.
- `shaders/PointCloud.{vert,frag}` — v1 height-field point-cloud shaders.
- `gpu::ResourceRef::DeepRef` carries `sampleToPixel` (per-sample pixel
  ancestry) and `totalSamples` so the vertex shader is `O(1)` per draw.
- `ui::ViewportMode` enum (`Flat2D` / `PointCloud3D`) with dropdown in
  the viewport panel header.
- Orbit-camera controller in `ImGuiRenderer`: mouse drag → yaw/pitch,
  scroll → multiplicative zoom. Viewport resize syncs aspect ratio.
- `Loom` accepts an optional positional CLI argument — `./Loom
  path/to/deep.exr` builds the `DeepEXRRead → DeepFlatten → Viewer`
  chain at startup, defaults to the legacy `Constant → Viewer` demo
  graph when no path is given.
- Engine main loop constructs `TransientBufferPool`, `StagingArena`,
  `SyncDeepReader`, `Camera`, and `PointCloudPass`; populates the
  new `EvaluationContext` fields; routes per-frame pass selection on
  `ViewportMode` (`Flat2D` → `DisplayPass`, `PointCloud3D` →
  `PointCloudPass`).
- `core::AABB` — minimal axis-aligned bounding-box reducer with a
  `valid` flag; used as the world-space scene-extent hint on deep
  payloads.
- `gpu::ResourceRef::DeepRef::sceneBounds` — `core::AABB` computed
  during `uploadDeepImage`'s SoA→AoS pass. NVS-aware: reads
  `world_pos.{x,y,z}` directly when the layout exposes them, falls
  back to the `PointCloud.vert` height-field synthesis (XY in
  centered [-1, 1], Z = -frontDepth) otherwise. Filters the
  `Z = 1e+10` background sentinel and non-finite samples; an
  all-sentinel payload yields `valid = false`.
- `gpu::reduceSceneBounds` — exposed pure-CPU reduction so the
  bounds-compute behaviour is testable without a GPU device.
- `core::Camera::frameToBounds(center, radius, padding)` — sphere-fit
  reframer. Recenters target, places the camera on +Z at
  `padding * radius / sin(fovY/2)`, and widens near / far planes to
  cover the sphere.
- `ui::ImGuiRenderer::resyncOrbitFromCamera` — exposes the lazy
  orbit-from-camera derivation so external repositioning (auto-frame)
  picks up the new pose cleanly.
- Engine main loop auto-frames the camera the first time a valid deep
  payload with non-empty `sceneBounds` reaches the cache, and again
  whenever the payload identity (samples buffer pool index /
  generation) changes.

### Changed
- Viewport panel opens with `ImGuiWindowFlags_NoScrollbar |
  ImGuiWindowFlags_NoScrollWithMouse` — scroll wheel now reaches the
  orbit camera instead of being consumed by the panel scrollbar.
  Same flags applied to the Node Editor panel so the wheel reaches
  the canvas zoom.
- Orbit drag sensitivity scales by `tan(fovY/2)` and viewport height
  so a one-screen-height drag always produces a fixed yaw / pitch
  regardless of zoom level or window size. Zoom radius bounds widened
  to `[0.001, 1e6]` so kilometer-scale scenes remain reachable.
- `NodeEditorPanel` config now sets a 26-step `CustomZoomLevels`
  array (~12 % per stop) replacing imgui-node-editor's default ~50 %
  stops. A single wheel tick no longer skips past target scale.
- `NodeEditorPanel` additionally accumulates fractional
  `io.MouseWheel` events. imgui-node-editor casts the wheel to int
  before zooming, so trackpad / hi-res-mouse sub-tick events (~0.1
  per frame) used to drop on the floor; we synthesise integer ±1
  pulses once the accumulator crosses 1.0.
- Flat 2D viewport supports drag-to-pan and wheel-zoom (cursor
  anchored). Applied via custom UV coords on the `ImGui::Image`;
  state persists across mode toggles.
- Engine main loop updates `Camera` near / far planes every frame
  from the current orbit distance + the most-recently-framed scene
  radius. The point-cloud no longer clips at the back when the user
  zooms in past the original framing distance.
- `core::CameraNode` — first-class graph node carrying view params as
  knob-editable values (position, target, fov_y_deg, near, far).
  Auto-aspect from the active viewport. Output pin: `Kind::Camera`.
  JSON-serialisable via the existing `Param` infrastructure.
- `core::PointCloudRenderNode` — pulls `Kind::Deep` + `Kind::Camera`,
  produces `Kind::Image` (RGBA32F). Knobs: `z_scale`, `point_size`.
  Delegates rendering to the engine-owned `gpu::PointCloudPass` via
  `EvaluationContext::pointCloudPass`.
- `Kind::Camera` + `CameraRef` value-type payload on `ResourceRef`.
  Pin compatibility extended (`PinType::Camera`); the canonical wire
  is `CameraNode → PointCloudRenderNode`.
- `gpu::computeRecommendedZScale` + `DeepRef::recommendedZScale` —
  per-payload auto-scale for the `PointCloud.vert` synthesis path so
  scenes with extreme depth ranges navigate cleanly at `z_scale = 1`.
- `DisplayPass.frag` aspect-fits the source image into the viewport
  with a black letterbox. Fixes the "garbage outside rendered region"
  glitch surfaced by non-square deep EXRs.
- `TransientImagePool::getExtent` accessor lets `DisplayPass` query
  the source image's native dimensions for aspect-fit math.
- `gpu::PointCloudPass::record` now takes a `ResourceRef::CameraRef`
  value snapshot (replacing the previous `const core::Camera&`),
  matching the cache-carried camera payload.
- `Graph::getCameras()` mirrors `getViewers()`. v1 is single-camera;
  orbit + auto-frame target `getCameras()[0]`.

### Changed
- The viewport-panel dropdown is now an **input mode** selector
  (Pan 2D / Orbit 3D), not a render-mode toggle. The choice of 2D vs
  3D is a graph-wiring decision — wire `DeepFlatten → Viewer` for
  flat 2D, or `PointCloudRender → Viewer` for the 3D point cloud.
  `ViewportMode` renamed to `ViewportInputMode`.
- Startup graph (`./Loom path/to/file.exr`) now spawns CameraNode +
  PointCloudRenderNode pre-wired (Deep + Camera ⇒ PointCloudRender).
  Initial Viewer input is `DeepFlatten` (2D); user drags
  PointCloudRender's output onto the Viewer's input in the node
  editor to swap to 3D.
- Orbit gestures over the viewport mutate the active CameraNode's
  `position` param via `setParam`, not an engine-singleton Camera.
  Auto-frame writes through the same path. The graph re-evaluates
  with the user's view next frame.
- `ConstantNode` and `MergeNode` migrated onto the Param system — fill colours
  are now editable in the UI rather than hardcoded.
- `gpu::HazardTracker` widened from image-only keys to `ResourceKey
  { Kind, poolIndex, generation }`. Buffer hazards (RAW / WAW) are now
  tracked alongside image hazards; deep-EXR payloads decompose into both.
- `gpu::ComputeTask` carries `readBuffers` / `writeBuffers` vectors
  alongside the existing image dependency vectors.

### Changed
- `ui::ViewportInputMode` dropdown is now both a gesture toggle AND a
  graph-wire toggle. Selecting "Orbit 3D" rewires the Viewer's input to
  the registered PointCloudRender output; "Pan 2D" rewires to
  DeepFlatten. Manual node-editor drags still work; the dropdown is the
  one-click path. Default mode for a `./Loom path/to/deep.exr` startup
  is now Orbit 3D — the user opened a deep file, they get the 3D view
  immediately.
- `Graph::replaceViewerInput(viewer, newSource)` helper composes
  `tryAddLink` over a Viewer's single input pin. Returns false (leaves
  the prior wiring intact) when the new wire would be rejected.
- Startup-graph nodes spawn at non-overlapping canvas positions via
  `NodeEditorPanel::setNodePosition`. Layout follows dataflow:
  DeepEXRRead (0,0) → DeepFlatten (250,-60); Camera (0,120) →
  PointCloudRender (250,80); both meet at Viewer (500,0). The editor's
  persistent settings file overrides these on sessions after the first.
- `applyOrbitInput` gates the CameraNode `position`-param write behind
  an "input fired this frame" check. Idle frames no longer redirty the
  CameraNode, so user knob-edits on the position param survive.
- main.cpp materializes CameraNode params (position, target, fov, near,
  far) back onto the engine `core::Camera` each frame after
  `graph.execute`. When the `target` param changes externally (knob
  edit, auto-frame) the orbit controller resyncs from the new
  (position, target) offset so the next drag gesture stays consistent.

### Fixed
- `DisplayPass` framebuffer attachment now loads with `LOAD_OP_CLEAR`
  to opaque black instead of `DONT_CARE`. The fragment shader's
  letterbox branch was supposed to fill the outside-fitted-rect pixels,
  but a degenerate-extent frame (viewport first-allocation or mid-
  resize) makes the aspect-ratio math NaN and falls through to an
  undefined `imageLoad`. CLEAR guarantees the pixels are opaque black
  even when the shader's branch goes wrong.
- `shaders/DisplayPass.frag` early-returns opaque black when either
  the viewport or source extent is zero. Belt-and-suspenders alongside
  the CLEAR loadOp.
- main.cpp guards `displayPass.record` behind `vpW > 0 && vpH > 0 &&
  srcExtent.width > 0 && srcExtent.height > 0`; on degenerate extents
  the viewport falls through to `clearViewportToBlack` instead.

### Removed

---

## [0.2.0] — Cleanup branch (in progress)

The `refactor/claudes-review` branch lands a comprehensive cleanup of code quality, correctness, and project infrastructure ahead of feature work. See [docs/archive/refactor-cleanup-2026.md](docs/archive/refactor-cleanup-2026.md) for the per-phase log.

### Highlights (planned)
- Color management subsystem replaces hardcoded gamma in the display chain.
- `RenderCache` overwrite leak fixed; `garbageCollect` wired into the frame loop.
- `transitionImageLayout` rewritten as a `vkCmdPipelineBarrier2`-based helper with derived stage/access masks.
- `DispatchManager` hazard logic extracted into a `HazardTracker`; WAW support added; WAR scaffolded.
- `Region` participates in `RenderCache` keys and propagates through `pullInput`.
- `VulkanContext` decomposed into `Instance` / `Device` / `Swapchain` / `FrameLoop` / `ResourceFactory`.
- Timeline semaphores replace the binary-semaphore + per-image-fence sync pattern.
- `VkPipelineCache` persisted to disk between runs.
- Bindless slots gated by frame-retirement, preventing recycle-while-in-flight.
- `ResourceRef` typed pin payloads (`Image` / `Buffer` / `Deep`) replace raw `ImageHandle`.
- CI runs `ctest`, sanitizer matrix, `clang-tidy`, `scan-build`, coverage, and headless GPU tests via Lavapipe.
- Test suite gains GPU readback correctness, push-constant bounds, swapchain recreate, bindless reuse stress, and golden-image regression.

---

## [0.1.0] — Pre-cleanup baseline (retrospective)

The state of the repository at the start of the `refactor/claudes-review` branch. Captured retrospectively for reference; no changelog entries were maintained during this period.

### Added
- Headless DAG model: `SlotMap`, `Handle<Tag>`, `Graph`, four node types (`Constant`, `Merge`, `Viewer`, `Passthrough`).
- 2-pass evaluator: mark required → topological sort → execute.
- `RenderCache` as a transient object.
- Vulkan 1.3 GPU layer: `VulkanContext`, `BindlessHeap`, `TransientImagePool`, `TransientBufferPool`, `PipelineCache`, `DispatchManager`, `DisplayPass`.
- ImGui + imgui-node-editor UI.
- GoogleTest suite (11 test files; ~1,500 lines).

[Unreleased]: https://github.com/<owner>/loom/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/<owner>/loom/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/<owner>/loom/releases/tag/v0.1.0
