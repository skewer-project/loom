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
