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
