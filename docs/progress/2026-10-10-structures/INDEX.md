# Structures and grading: first renders (10 Oct 2026)

First time bridges, tunnels, retaining walls and the grading round 3 rules (sloped approaches with wing walls,
rounded slope tops/toes, neighbour-aware walls) have been rendered. All 1600 x 900, `prototypes/mesh-view/`
(three.js in headless Edge), sim_core OBJ exports. Every scenario has one street-level 3/4 view (`_street`) and one
aerial oblique (`_aerial`). Nothing here is judged for realism yet.

| Scenario | Layout / fixture | Natural terrain (`--terrain`) | Images | What to look at |
|---|---|---|---|---|
| s1 bridge over a road | `layouts/s1_bridge_over_road.json`: a_road_rural at z 0, b_road_rural on 7.5 m deck, 40 m bridge | flat | `s1_bridge_over_road_street.png`, `_aerial.png` | 2 piers, abutments, wing walls, embankment approaches |
| s2 bridge over a short gap (14 m < 20 m: solid block) | `layouts/s2_bridge_short_gap.json` | ditch (5 m deep, ~12 m wide) | `s2_bridge_short_gap_street.png`, `_aerial.png` | block, no piers (`solid_block=1`) |
| s3 tunnel portal + approach | `layouts/s3_tunnel_portal.json`: 130 m tunnel, road at z 8 | hill (38 m) | `s3_tunnel_portal_street.png`, `_aerial.png` | portal headwall, wing walls, slope approach, bore cut in the terrain |
| s4 retaining wall run, 4 m panels | `layouts/s4_retaining_wall_run.json` (gradient 1.5% so panels step) | sidehill (0.85 slope) | `s4_retaining_wall_run_street.png`, `_aerial.png` | cut-side wall, panel steps, coping (sun from the south) |
| s5 cutting with rounded top | `layouts/s5_cutting.json` | ridge (10 m) | `s5_cutting_street.png`, `_aerial.png` | rounded slope tops |
| s6 embankment with rounded toe | `layouts/s6_embankment.json`: road ramps 0 -> 5 m | flat | `s6_embankment_street.png`, `_aerial.png` | rounded toe |
| s7 two roads at different levels | `layouts/s7_adjacent_levels.json`: y = 0 at z 2, y = 16 at z 7.5 | terrace (0.45 slope) | `s7_adjacent_levels_street.png`, `_aerial.png` | no giant wall between the roads |
| s8 interchange | built-in fixture `interchange` (flat, z 0) | flat | `s8_interchange_street.png` (on the minor road, looking at the bridge), `s8_interchange_aerial.png` | whole thing; 40 m bridge has no pier (central reservation 0.2 m) |

Cameras are in `views/<scenario>.json` (sim metres: x east, y north, z up; `rel` = z above road surface; `sun` =
[compass azimuth, elevation]).

## Regenerate

```
bash docs/progress/2026-10-10-structures/regen.sh              # everything
bash docs/progress/2026-10-10-structures/regen.sh s3_tunnel_portal s8_interchange   # some
```
Prerequisites: a gcc 16 Ninja Release build (`BUILD=<dir>`, default `build-agent-struct`; configure per HANDOFF
section 1: `cmake -S . -B build-agent-struct -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++` with
`C:\msys64\mingw64\bin` and CMake/Ninja on PATH, then `cmake --build build-agent-struct -j 3`), Node 24 and Edge.
Per scenario the script runs:

1. `export_mesh --out <tmp>/<name> --layout layouts/<name>.json` (or `export_mesh --out <dir> interchange`): road
   surfaces, markings, and structures on FLAT ground at z = 0 with no walls.
2. `export_graded --terrain <terrain> --step <m> --margin <m> --out <tmp>/<name> --layout ...` (new,
   `prototypes/mesh-view/tools/export_graded.cpp`, compiled by the script against `libsim_core.a`/`libscene_tools.a`):
   grades the analytic terrain with `cp::grading` (0.5 m texels), builds retaining walls from `wall_runs`, rebuilds the
   structures on the graded ground, writes `<name>_terrain.obj` (cells inside a tunnel bore dropped), replaces the
   `*_structure_*.obj`, and rewrites `export.json`.
3. `node prototypes/mesh-view/render.mjs --in <tmp>/<name> --out docs/progress/2026-10-10-structures --views views/<name>.json`.

Terrain mesh step is 1-2 m (2 m for s1 and the interchange to keep the OBJ small); grading itself is always 0.5 m.

## Tooling changes and limits

- `export_mesh` alone cannot show terrain/grading/walls (flat z = 0 ground, no grading). `export_graded` adds that.
- `export_mesh`'s own `export.json` is malformed (its final `fprintf` passes the structures JSON where "cameras" goes and
  the cameras where "files" goes, so the file list is lost). `export_graded` rewrites a valid `export.json` from the
  directory; `export_mesh` was NOT edited (not in this task's scope). Layout cameras are therefore dropped.
- `mesh-view.js`: structure surfaces get concrete/brick materials (`wall_facing` = brick texture, placeholder), terrain
  uses the grass slot, optional per-view `sun`.
- Terrain is natural analytic terrain plus grading output only: no water, no vegetation, no material variation.
- Walls and terrain meet by position only; the terrain under wall solids is whatever grading returns.
