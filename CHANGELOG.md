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

### Changed
- `ConstantNode` and `MergeNode` migrated onto the Param system — fill colours
  are now editable in the UI rather than hardcoded.
- `gpu::HazardTracker` widened from image-only keys to `ResourceKey
  { Kind, poolIndex, generation }`. Buffer hazards (RAW / WAW) are now
  tracked alongside image hazards; deep-EXR payloads decompose into both.
- `gpu::ComputeTask` carries `readBuffers` / `writeBuffers` vectors
  alongside the existing image dependency vectors.

### Changed

### Fixed

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
