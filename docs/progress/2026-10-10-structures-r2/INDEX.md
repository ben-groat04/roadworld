# Structures round 2: walls, coping, panel joints, short-span block (10 Oct 2026)

Re-render of s1-s8 after the `structure_mesh` fixes (review fixes 2 and 4 of `../2026-10-10-structures/REVIEW.md`).
Same pipeline as round 1 (`regen.sh` here: same as the round-1 script with the output folder and `build-agent-smesh-gcc`).
Images 1600 x 900. Nothing here is judged for realism yet.

**Grading state at render time:** `sim_core/src/grading.cpp` and `sim_core/include/cp/grading.h` UNMODIFIED in the working
tree (`git status` clean for both, last commit `e60046a`), so this is still the old grading: ~10 m walls on s4/s7, band
`wall_thickness_m` 1.5 m, no wall cap, no benching, untapered wall ends. Re-render after the grading changes land.

## What changed in the structures
- Retaining wall = a 0.4 m face (`wall_face_thickness`) with a 0.15 m coping overhanging 0.05 m each side (0.5 m wide in total)
  on its own surface `coping` (pale stone tint in the viewer). Previously the coping covered the whole 1.5 m grading band.
  For cut walls the rest of the 1.5 m band is a hidden backing up to the ground behind, because the old grading leaves a
  pit there (it disappears once grading's wall band equals the face thickness).
- Every wall is cut into panels of exactly `wall_panel` (4 m; the last one is the remainder, a remainder under 1 m joins the
  previous panel). A joint pilaster (0.12 m wide, 0.04 m proud, own surface `wall_joint`, dark in the viewer) marks every
  panel boundary whether or not the level steps. Steps are at most 0.5 m, in 0.25 m quanta.
- Cut walls show the road-side face at the verge edge; fill walls show the outer face (the face moved out to the back of the band).
- Wing walls use the same face + coping rule.
- s4 uses a new terrain `sidehill_wavy` (the contour wanders +-1.2 m along the road) so the wall top actually steps; on the
  plain `sidehill` the top is level and no steps exist (that is why none were visible in round 1).
- s5/s6 cameras re-aimed (s6 street was inside/at the embankment toe; s5 aerial was too far and too flat-lit).

| Scenario | Images |
|---|---|
| s1 bridge over road | `s1_bridge_over_road_street.png`, `_aerial.png` |
| s2 short gap (solid block) | `s2_bridge_short_gap_street.png`, `_aerial.png` |
| s3 tunnel portal | `s3_tunnel_portal_street.png`, `_aerial.png` |
| s4 retaining wall run (panels, joints, coping) | `s4_retaining_wall_run_street.png`, `_aerial.png` |
| s5 cutting | `s5_cutting_street.png`, `_aerial.png` |
| s6 embankment | `s6_embankment_street.png`, `_aerial.png` |
| s7 adjacent levels | `s7_adjacent_levels_street.png`, `_aerial.png` |
| s8 interchange | `s8_interchange_street.png`, `_aerial.png` |

To see the panel layout: `node prototypes/mesh-view/render.mjs ... --joints-red` paints the joint pilasters red.
