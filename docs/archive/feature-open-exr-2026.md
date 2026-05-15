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
