# Structures round 4: terraced walls (10 Oct 2026)
Built in `build-agent-g4` (gcc 16). Regenerate with `regen.sh [scenario ...]`. Rendered: s4, s5, s6, s7 only (s1-s3, s8 not re-rendered; see r3).
New render step: terrain mesh 0.5 m on s4/s5/s7.

| Scenario | Images |
|---|---|
| s4 side hill (0.85 slope) | `s4_retaining_wall_run_street/aerial/alongA/alongB/end.png` |
| s5 cutting | `s5_cutting_street/aerial/toe/along.png` |
| s6 embankment | `s6_embankment_street/aerial/end.png` (the bank is only a 5 m grass slope; views remain weak) |
| s7 adjacent levels | `s7_adjacent_levels_street/aerial/alongA/alongB/between/end.png` |

Wall runs at render time: s4 8 tiers (0-7) per side, retained height max 3.74 m (cut) / 3.83 m (fill), all flagged over (tier height 3.4-3.8 m because a 3 m tier cannot out-climb a 0.85 slope at 3.5 m pitch); was one 9-10 m wall.
s7: shared wall between the roads 5.5 m (over: the 3.4 m gap between footprints fits one tier only), lower south wall 2.5 m, upper north wall 2.5 m; the upper road has no wall on the shared side.
Still wrong: s7 east end has a dark pit/gouge and striped cut faces beyond the roads' ends (shared structure tapering); s5/s6 do not show ditch / bank clearly (ditch is in the heightfield: 0.27 m below formation at the toe, cross-section printed in the report).
