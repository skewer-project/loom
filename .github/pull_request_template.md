## Summary
<!-- One or two sentences. What does this PR do and why? -->

## Changes
<!-- Bulleted list. One bullet per logical change. -->

-
-

## Motivation
<!-- The "why". What problem does this solve, what bug does it fix, what feature does it enable? -->

## Test plan

- [ ] `cmake --build --preset debug` succeeds.
- [ ] `ctest --preset debug` passes.
- [ ] `cmake --build --preset sanitize` + `ctest` passes (if touching runtime code).
- [ ] No new Vulkan validation-layer warnings (if touching GPU code).
- [ ] No new `clang-tidy` warnings on changed files (if touching `.cpp` / `.hpp`).
- [ ] Visual smoke test: `./build/debug/bin/Loom` renders correctly (if touching shaders or display path).
- [ ] Golden image updated (if visual output changed).
- [ ] `CHANGELOG.md` updated under `[Unreleased]` (if user-visible).

## Conformance
<!-- Confirm the PR follows the relevant CLAUDE.md sections. Cite the section if you intentionally deviate. -->

- [ ] Conforms to [CLAUDE.md](../CLAUDE.md): resource ownership, layout invariants, hazard model, color management, push-constant budget, logging, assertions, profiling.

## Screenshots / captures
<!-- Optional. Before/after images, RenderDoc captures, etc. -->

## Linked issues
<!-- Fixes #123, Refs #456 -->
