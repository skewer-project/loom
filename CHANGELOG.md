# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
<!-- Note: items land here as phases of the cleanup branch merge. -->

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
