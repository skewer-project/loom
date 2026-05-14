# Loom — Documentation

This directory holds the project's reference, design, and archive documentation. The repo root keeps only the files GitHub expects there (`README.md`, `CONTRIBUTING.md`, `CHANGELOG.md`, `LICENSE`).

## Read these first

| Document | Purpose |
|----------|---------|
| [CONVENTIONS.md](CONVENTIONS.md) | The plan of record. Engineering invariants, layout / hazard / lifetime contracts, project conventions. PRs cite section headings. |
| [architecture.md](architecture.md) | One-page system overview. Layer diagram, frame lifecycle, compile-time dependency map. |

## Reference index

| Document | Purpose |
|----------|---------|
| [Doxyfile.in](Doxyfile.in) | Doxygen configuration template. Build with `cmake --build build --target docs`. Output: `${CMAKE_BINARY_DIR}/docs/html/index.html`. |

## Archive

`docs/archive/` is **read-only history**. Entries are captured at the point a phase or branch lands and never edited afterward — the codebase, `git log`, and the Doxygen-rendered reference are the live sources.

| Document | Period | Purpose |
|----------|--------|---------|
| [archive/refactor-cleanup-2026.md](archive/refactor-cleanup-2026.md) | 2026, `refactor/claudes-review` branch | Per-phase log of the eleven-phase cleanup that brought the codebase to a professional baseline. Captures the *why* behind each decision and the verification steps. |
| [archive/build-out-phases-1-6.md](archive/build-out-phases-1-6.md) | Pre-2026, original implementation phases | The original phase-by-phase build-out log — SlotMap → DAG → topological sort → UI → Vulkan bindless → dispatch → display chain. Read for historical context; the current implementation diverges. |

## When to add documentation

- **A change in convention** (new invariant, new layout rule, new resource contract): update `CONVENTIONS.md` in the same PR as the code change. Cite the section in the PR description.
- **A change in architecture** (new subsystem, new layer, removed component): update `architecture.md`'s diagram and the per-layer paragraph.
- **A change in public API**: write the Doxygen comment on the header declaration. The Doxygen build picks it up automatically.
- **A design decision worth preserving** (the *why* behind a non-obvious choice): consider an ADR-style file under `docs/decisions/` when that directory lands. Until then, capture it in the PR description and link the PR from `CONVENTIONS.md` if the decision is load-bearing.

## What does *not* go here

- Setup instructions → `CONTRIBUTING.md` at the repo root.
- User-visible release notes → `CHANGELOG.md` at the repo root.
- Per-PR descriptions → the PR itself (use the template at `.github/pull_request_template.md`).
