# Repo health review (10 Oct 2026, cold read of committed state)

## Verdict

**Overall: fair.** Not a mess. The skeleton is sound: an engine-agnostic library with `cp::Command` undo, stable ids, region data in `data/`, tests, CI, and good decision logs. But it is a few days old, written by parallel agents, and it shows exactly the stress fractures that become a mess when blocks, parcels, buildings and traffic arrive: a few god files, per-module copies of the same geometry helpers and structs, tunables that sit in code instead of the region pack, no module boundary rules, and a doc set that has outgrown its entry points.

| Area | Rating |
|---|---|
| Structure / layout | tidy (minor debris) |
| sim_core design | fair, messy in places (duplication, god files) |
| Tests | fair (wide, but loosened and unmeasured) |
| Unreal side | fair (unbuilt, wrapper scheme is fragile) |
| Prototype | fair (quarantined by name, not by rule) |
| Docs | messy in places |
| Data | fair (versioned but unvalidated) |
| Process debris | fair (history is poor, code is clean) |

## What is actually good

- Exceptions are really off: `-fno-exceptions` is in `sim_core/CMakeLists.txt`; zero `std::cout/cerr` in `sim_core/src` (one `snprintf` formatter). Almost no TODO/FIXME: 1 in sim_core (`junction_merge.cpp:15`), 4 in the Unreal plugin, 0 in tests. Almost no commented-out code (1 line).
- Dependency graph between modules is shallow and acyclic: most `.cpp` files include only their own header plus `edge_sampling.h`/`lanes.h`. The junction code is already split into 6 files (geometry, connectors, merge, roundabout).
- `tile_key.h` and `triangulate.h` were extracted once the problem was noticed; the habit exists.
- `.gitignore` covers `build-*/`, `node_modules/`, Unreal `Binaries/Intermediate/Saved`, and the copyrighted reference images. Unreal binary types are LFS-routed via `.gitattributes`.
- 254 test cases / 288 CHECK-style assertions lines, fuzz tests for connectors, cross-language goldens within 0.05 mm for grading.
- Region packs carry `version` and `_comment`/`source` provenance; markings loader rejects unknown keys (`markings.cpp:834`).
- Decisions are logged (planning log, 33 rounds), and HANDOFF is candid about what is broken.

## Real problems (ranked by future pain)

1. **Duplicated geometry helpers and structs, no shared geometry module.** Each `.cpp` re-defines `cross`, `dot` (`render_mesh.cpp`, `structure_mesh.cpp`), `signed_area` (`junction_geometry.cpp`, `junction_roundabout.cpp`), `dist_point_segment` (`render_mesh.cpp`, `triangulate.cpp`), `left_normal`, `edge_heading`, `edge_surface_z` (`markings.cpp:1129` and `render_mesh`), `hash01`, `seg_cross`, `ring_area`, `area2`, and `get_string/get_number/get_bool` JSON readers (2 copies each). `kEps = 1e-9` is redeclared per file (`edge_sampling.cpp:10`, `markings.cpp:30`) beside ad hoc `1e-6`. Walls/tunnels exist twice: `structure_mesh.h:255-280` (`TunnelSection`, `WallEnd`) comment says "merge when convenient" with `grading.h:233-266` (`WallPoint`, `TunnelStation`). Blocks, parcels and buildings will add polygon work on top; every divergent copy is a latent inconsistency bug. *Fix now: cheap (hours). Later: each new system copies a fourth variant.* Task: one agent owns new `cp/geom.h` + `src/geom.cpp` (polygon area/contains, segment distance/intersect, offset, eps constants) and a `cp/json_util.h`; migrate helpers file by file, tests green after each. Second agent unifies the structure/grading structs (`structure_mesh.*`, `grading.*`).

2. **God files with no module seam.** `markings.cpp` 2,586 lines (7 anonymous namespaces: loader at line 260, geometry helpers 908, edge markings 1422, junction markings 1922, aggregate 2533; the loader alone is ~650 lines); `structure_mesh.cpp` 1,675; `render_mesh.cpp` 1,641; `grading.cpp` 1,118; `street_lights.cpp` 996. Test files mirror them (`test_markings.cpp` 1,420, `test_render_mesh.cpp` 1,220). Natural splits exist already (the `// ====` banners in `markings.cpp`). Splitting is mechanical now and awkward once other systems call into the internals (markings already reaches into `render_mesh.h` for surface height). Task: one agent per file, ownership of that file + its test + CMake lines + the matching `Wrap_*.cpp`; no behaviour change.

3. **Region-specific values in core code / tunables not in data.** `junction_merge.cpp:21-28` taper table by kph with a TODO "move to the region pack"; `kGoreLength = 10` (line 30); `GradingParams` and `StructureParams` default values live in headers (`grading.h:48-68`, `structure_mesh.h:112`; HANDOFF confirms `wing_*`/`round_*` are not region fields); `"mph"` special-cased in `markings.cpp:334` and `region_pack.cpp:143` (two parsers for the same unit); `layout_io.cpp:197` and `layout_io.h:89` default region to `"uk"`; `render_mesh.h:88` hardcodes `"asphalt"`; test layout comments assume left-hand. The rule in CLAUDE.md is mostly honoured (no UK road names in logic found), but the pattern "default in header, override maybe later" will spread to parcels/buildings. Fix now: small. Task: one agent owns `region_pack.*` + `data/regions/uk/region.json` and moves the taper table, gore length, grading/structure defaults and unit conversion (single function) there.

4. **Tests are broad but have known soft spots and no measurement.** HANDOFF records two loosened invariants (`ground_under_road` 5 to 3 cm, `no_bare_steps` 1.0 to 1.02 m, in the prototype checks); the roundabout test "no longer asserts ring lane lines"; a test that passed only because a failure "could not be reproduced"; round 3 junction features (lane clear width, splitter shape, apron, class-pair radii, skewed T) have no dedicated tests; turn pockets are dead code behind a disabled flag. The suite is one ~250-case executable with 1.9 million assertions; no timing is recorded, no label split (fast/golden/fuzz), and one skipped case is uninvestigated. Whole-file `main.cpp` has no assertions of its own. Fix: one agent owns `tests/` organisation (doctest tags or CTest labels for fast/golden/fuzz, per-suite timings in HANDOFF) and writes the missing round 3 tests; a separate agent decides whether to delete turn pockets until they are scheduled.

5. **Mutation boundary is only partly enforced.** `RoadGraph` exposes public `insert_node/erase_node/insert_edge/erase_edge/set_region` (`road_graph.h:63-78`), so any code can bypass `cp::Command` and undo. Today `layout_io` and tests use it legitimately, but nothing prevents a new system (parcels, traffic) from doing so. The in-flight work notes `erase_node` bugs (stale dirty set), and there is no change-road-type command. Fix before the second mutable world object exists: make mutators accessible only to commands (friend or a `RoadGraphMutator` token), add the missing commands. Task: agent owning `road_graph.*`, `road_commands.*` (currently being edited by others, so schedule after they land).

6. **Docs have outgrown their entry points** (see below).

7. **Commit history and CI.** 114 commits, 62 of them "WIP" (23 also `[skip ci]`), CI last green at `1ae80b0`; everything since is unverified by CI (including the gcc warnings noted in HANDOFF: `structure_mesh.cpp:1293`, `:1023`). Bisecting or reverting an agent's change is hard because parallel agents shared one checkout and commits mix areas. Fix: land one normal commit per area now, then use short-lived branches per agent task (`isolation: worktree`).

8. **Unreal wrapper scheme.** Every `sim_core/src/*.cpp` needs a hand-made `Wrap_<name>.cpp` (27 files, enforced by CI shell script). It works but is manual and error-prone, and the plugin has never been compiled in UE. Brief note only; the dedicated architecture review covers this.

## Cosmetic / ignore for now

- 1.8 GB of `build-*` dirs on disk (23 dirs, gitignored): delete, but not a repo problem.
- Root `README.md` is 4 lines and says "UK city builder".
- `prototypes/road-draw/index.html` is a generated file that is tracked, plus `x/` and `screenshots/` (about 6 MB of PNGs, 11 files in `x/` with unclear names).
- Line-ending warnings (LF to CRLF) on commit: add `* text=auto eol=lf` or `.gitattributes` rule.
- Two gcc warnings in `structure_mesh.cpp`.
- `layout_io` and `region_pack` use `_comment` keys as documentation; fine.
- `tools/blender_kit` at the root while `unreal/tools` and `sim_core/tools` exist: three `tools` places; acceptable, document it.

## Structure notes

- `CLAUDE.md` Layout omits `unreal/`, `tools/`, `third_party/`, `docs/reference/` (private). Add them.
- `.git` is 186 MB; 197 files / 178 MB of Poly Haven textures sit in LFS-routed `unreal/.../SourceArt`. That is the correct mechanism, but confirm LFS storage quota before committing more art (per-file 4-6 MB 2K normal maps).
- No C++ dependency on the JS prototype except goldens: `tests/test_grading.cpp:2,137` reads `tests/data/grading/*` generated by `prototypes/road-draw/tools/grade_golden.mjs`. Direction is correct (prototype generates, C++ consumes committed files), but the goldens freeze prototype behaviour; once C++ diverges deliberately, the JS port needs to be retired or the goldens re-based. Decide and document.

## Docs

16 docs, 214 KB; `planning-log.md` 36 KB, `performance-plan.md` 29 KB, `unreal-photorealism.md` 25 KB, `HANDOFF.md` 18 KB. Entry points: CLAUDE.md says "read HANDOFF first", but HANDOFF self-describes as dated and partly superseded (section 5a overrides section 5; section 0 revised twice), and its test count (235) is already stale (254 now). Overlaps: `unreal-photorealism` / `ue58-feature-sweep` / `performance-plan` / `unreal-architecture` / `vegetation-and-surface-realism` all discuss the Unreal rendering stack; `pre-research`, `mesh-terrain-research`, `pcg-city-research` are research dumps; `v0-brief` is a scope doc that the planning log has since moved beyond. Recommendation: keep `CLAUDE.md`, `planning-log.md` (decisions), `HANDOFF.md` rewritten as a short "current state" page regenerated at each stop; move research dumps into `docs/research/`; mark `v0-brief` and `pre-research` as historical; add a one-page `docs/README.md` index. Do not delete the log.

## Data

`lane_types.json`, `region.json`, `road_types.json`, `marking_rules.json` and the layouts all carry a version or format key, which is good, and `marking_rules` rejects unknown keys. But there is no JSON Schema or standalone validator; each loader hand-validates (so `region_pack.cpp`, `markings.cpp` and `layout_io.cpp` each have their own `get_*` helpers). Only `uk` exists, so cross-region consistency is untested; a second tiny fake region pack in `tests/data` would prove the core is region-agnostic. Layouts in `data/layouts/ref` are test fixtures filed under `data/`, which blurs "data a game ships" and "fixtures".

## Cheap clean-ups before the Unreal phase (each under an hour)

1. Rewrite the CLAUDE.md layout section (add `unreal/`, `tools/`, `third_party/`; note `prototypes/` is never depended on except goldens).
2. Add `docs/README.md` index and move research docs to `docs/research/`; refresh HANDOFF counts.
3. Extract `cp/geom.h` with the clearly identical helpers (`cross`, `dot`, `signed_area`, `dist_point_segment`, `kEps`) and replace copies.
4. Move the merge taper table and `kGoreLength` into the region pack (clears the only sim_core TODO).
5. Single speed-unit conversion function used by both `region_pack.cpp` and `markings.cpp`.
6. Fix the two gcc warnings; add `.gitattributes` `* text=auto eol=lf`.
7. Tag tests (fast / golden / fuzz) and record suite timings.
8. Move `prototypes/road-draw/x/` and screenshots out of git (or into one `prototypes/.../evidence/` with a README); stop tracking generated `index.html`.

## Structural fixes worth doing soon

1. Split `markings.cpp` into loader / edge / junction / chain units, same for `structure_mesh.cpp` and `render_mesh.cpp`, one agent per file, behaviour unchanged.
2. Unify structure and grading wall/tunnel structs and give them one owner (a `cp/structure_types.h`).
3. Enforce the command boundary on `RoadGraph` and add the missing commands (change road type, set structure span, junction override) before blocks and parcels add second world objects.
4. Region-pack schema: one shared JSON helper plus a schema/validator test, a second synthetic region in tests, and all remaining tunables (grading, structures, wing/round) moved to data.
5. Process: one normal green-CI commit now, then per-agent worktrees and branches instead of one shared checkout; stop `WIP` as the default message.
