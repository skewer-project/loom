# Contributing to Loom

Thanks for your interest in Loom. This document describes how to set up a development environment, the conventions the project follows, and what reviewers look for in a pull request.

## Development setup

See [README.md](README.md) for build prerequisites (Vulkan SDK, C++20 compiler, CMake 3.21+).

```bash
git clone https://github.com/<owner>/loom.git
cd loom
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
./build/debug/bin/Loom
```

`cmake --preset sanitize` configures an Address + UB-sanitizer build; run the test suite under it before opening a PR.

### Pre-commit hooks

```bash
brew install pre-commit         # macOS
pip install pre-commit          # Linux/Windows
pre-commit install              # from the repo root
```

`pre-commit` runs `clang-format` and basic hygiene checks. CI will reject style violations.

## Branch and commit conventions

- **Branch names:** `feat/<short-desc>`, `fix/<short-desc>`, `refactor/<short-desc>`, `docs/<short-desc>`, `test/<short-desc>`, `chore/<short-desc>`.
- **Commit messages:** [Conventional Commits](https://www.conventionalcommits.org). Subject line ≤72 chars, imperative mood. Body explains *why*, not *what*. Reference issues with `Fixes #123` or `Refs #123`.
- **Atomic commits:** one logical change per commit. A formatting-only commit should not contain behavioural changes.

## Pull request checklist

Before opening a PR:

- [ ] Branch builds cleanly under Debug, Release, and Sanitize.
- [ ] `ctest` passes on every preset that applies to your changes.
- [ ] New behaviour has tests; visual changes update the golden image (see below).
- [ ] No new `clang-tidy` or `scan-build` warnings (CI will enforce).
- [ ] No new Vulkan validation-layer warnings.
- [ ] `CHANGELOG.md` updated under `[Unreleased]` if the change is user-visible.
- [ ] Public API additions documented (Doxygen comments on the header declaration).

The PR description should explain *why* the change is needed and *how* it was tested. Reviewers will look for:

1. Correctness — does it solve the stated problem?
2. Conformance with [CLAUDE.md](CLAUDE.md) — does it follow the project conventions (resource ownership, layout invariants, hazard model, color management, etc.)?
3. Test coverage — does the change add or extend tests appropriately?
4. Scope — is the diff focused, or has unrelated cleanup been bundled in?

## Coding conventions

The authoritative source is [CLAUDE.md](CLAUDE.md). Highlights:

- **C++20.** Use modern idioms: `std::span` for read-only ranges, structured bindings, `if constexpr`, designated initialisers, `[[nodiscard]]` on return values whose discard would be a bug.
- **No raw `std::cout` / `std::cerr`.** Use `loom::log::info/warn/error`.
- **No bare `assert`.** Use `LOOM_ASSERT(cond, msg)` (survives `NDEBUG`) or `LOOM_VK_CHECK(expr)` for Vulkan calls.
- **No raw `new` / `delete`.** Use `std::unique_ptr` / `std::shared_ptr` / VMA / RAII.
- **Headless `core/` cannot include Vulkan headers.** GPU-dependent code lives under `gpu/`.
- **Layout transitions** happen only in `DispatchManager`, `DisplayPass`, and `Swapchain`. Nodes never touch layouts.
- **Internal compositing math is always linear scene-referred float.** The display transform is applied exclusively in `DisplayPass`.

## Tests

- Unit tests live under `tests/core/` (headless) and `tests/gpu/` (Vulkan-required).
- GPU tests `GTEST_SKIP` cleanly when no Vulkan device is present.
- Add a happy-path test plus at least one edge case for every new node, shader, or public API surface.
- Visual changes update `tests/data/golden/*.bin`. Regenerate goldens with:
  ```bash
  LOOM_REGEN_GOLDENS=1 ctest --preset debug
  ```
  Commit golden updates in the same PR as the change that requires them, with a one-line justification in the commit body.

## Reporting bugs and requesting features

Use the GitHub issue templates. For security-sensitive reports, contact the maintainer privately rather than opening a public issue.

## License

By contributing, you agree that your contributions will be licensed under the project's license (see `LICENSE` once added at the repo root).
