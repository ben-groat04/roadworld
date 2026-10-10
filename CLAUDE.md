# City Painter (roadworld)

Photoreal UK sandbox city builder. Source of truth for decisions: `docs/planning-log.md`.
v0 scope: `docs/v0-brief.md`. Markings reference: `docs/road-marking-rules.md`.

## Layout
- `sim_core/` – engine-agnostic headless C++20 library. No Unreal headers, no GPL/copyleft deps (no CGAL). Clipper2 is the approved geometry lib.
- `tests/` – doctest unit/golden tests. Build: `cmake -S . -B build && cmake --build build && ctest --test-dir build`.
- `prototypes/` – throwaway browser UX test tools (not production code).
- `data/` – data-defined road types, lane types, marking rules.
- `docs/` – planning docs and design proposals.

## Rules
- Every player edit is a `cp::Command` executed through `cp::CommandStack`.
- Objects are referenced by stable `cp::Id<Tag>`; IDs are never reused.
- World coordinates are doubles (20 km+ maps).
- Lane graph and junction rules are driving-side agnostic: one setting per save, everything mirrors from it.
