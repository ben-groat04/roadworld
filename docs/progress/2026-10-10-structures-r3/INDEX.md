# Structures round 3: new grading rules (10 Oct 2026)

State: git HEAD `e66edb7` (grading: 3 m wall cap, slope/bench tiers, shared structure, tapered ends, toe ditch). `sim_core/` untouched;
only `prototypes/mesh-view/tools/export_graded.cpp` changed (below). Built in `build-agent-r3` (gcc 16, Ninja, Release, `-j 2`).
Regenerate with `regen.sh` (same as r2, new output folder and build dir). Images 1600 x 900. Materials and lighting are placeholders.

## What changed since r2
- Grading: walls capped at 3 m with slope/bench tiers, one shared structure between parallel roads, wall runs taper to zero at their
  ends, embankment end run-out, toe ditch in cuttings. `WallRun` gained `tier`, `over`, per-point `nat`.
- Tool: `export_graded.cpp` needed NO change to compile or consume the new `WallRun` (tiers are separate runs with the offset baked
  into the points, so the existing copy of `lo/hi/pts` suffices; `tier`, `over`, `nat` are not forwarded to `structure_mesh`).
  Added only a per-run diagnostic line (side, cut, tier, over, retained-height range, x range) to stdout.
- Cameras unchanged from r2 except the new views.

## Images
| Scenario | Images |
|---|---|
| s1 bridge over road | `s1_bridge_over_road_street/aerial.png` |
| s2 short gap | `s2_bridge_short_gap_street/aerial.png` |
| s3 tunnel portal | `s3_tunnel_portal_street/aerial.png` |
| s4 retaining wall run | `s4_retaining_wall_run_street/aerial.png`, `_alongA.png` (from west end, eye height), `_alongB.png` (from east end) |
| s5 cutting | `s5_cutting_street/aerial.png` |
| s6 embankment | `s6_embankment_street/aerial.png` |
| s7 adjacent levels | `s7_adjacent_levels_street/aerial.png`, `_alongA.png`, `_alongB.png`, plus extras `_between.png` (high, between the roads) and `_end.png` (east end, aerial); the last two are not in `views/` |
| s8 interchange | `s8_interchange_street/aerial.png` |

Wall-run diagnostics at render time: s4 two runs, both `over=1`, retained 9.1 m (cut) and 9.8 m (fill), single tier; s7 three runs:
upper road cut 5.5 m `over=1`, the others 2.5 m; no run in any scenario has tier > 0.

Review: `REVIEW.md`.
