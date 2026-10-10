# Second Unreal screenshots: road surfaces fixed (2026-10-11)

UE 5.8.3, same level and views as `../2026-10-10-unreal-first/`, plus one new view. Same 1920x1080 viewport captures.

## What changed
- **Winding fix (the bug from the first set).** `CPRoadMeshBuild.cpp` swapped triangle indices 1 and 2 for surfaces and markings, assuming Unreal's front face is clockwise. In `ProceduralMeshComponent`, the front face is counter-clockwise seen from outside (engine `KismetProceduralMeshLibrary.cpp` `GenerateBoxMesh`/`ConvertQuadToTriangles`: the +Z face is `(v0,v1,v3)`, CCW from above). The sim->UE axis swap is a mirror, but Unreal is left-handed, so the picture seen from above is unchanged and sim's CCW order is already correct. Swap removed; rule documented in `CPRoadMeshBuild.h`. See `unreal/tools/KNOWN_RISKS.md` H14.
- Floor plane enlarged from 2 km to 20 km (`FLOOR_SCALE` in `setup_look_test_level.py`; reused floors are replaced when the scale differs).
- New view `look_07_underside`: camera 6 m below the dual carriageway looking up.

| File | Shows |
|---|---|
| look_01_aerial | 750 m straight down (screen up = east, because the camera yaw is 90): whole interchange, roads, roundabout, T-junction stub, slips. Not mirrored (T side road runs east = up on screen, as in the sim). |
| look_02_roundabout_oblique | roundabout from ~100 m, kerbs with pavement behind, markings on asphalt, shadows on the floor |
| look_03_roundabout_approach | driver height on the approach: paved island, kerb faces outward |
| look_04_tee_minor_arm | side road at the T: lane lines, give-way dashes, kerbs and pavement |
| look_05_slip_merge | slip merge with chevron hatching (readable, not mirrored) |
| look_06_kerb_closeup | asphalt close-up, concrete kerb face with pavement behind |
| look_07_underside | from below the road: only sky, no road or pavement: the underside is back-face culled (materials remain single-sided) |

## Regenerate
`powershell -NoProfile -ExecutionPolicy Bypass -File unreal\tools\Build-Editor.ps1 -EngineRoot C:\UE_5.8 -MaxParallelActions 1 -Stream`, then `unreal\tools\First-Run.ps1 -Only level -EngineRoot C:\UE_5.8 -MaxParallelActions 1`, then `-Only screenshots`. Output: `unreal\CityPainter\Saved\Screenshots\look\`.

## Known problems (obvious breakage only, no realism judgement)
1. A thin dark-blue band remains at the horizon, beyond the 20 km floor (sky-atmosphere ground; much narrower than before). Add fog or a horizon mesh if it matters.
2. Roundabout circulation direction (clockwise for UK left-hand traffic) is not confirmable from these shots: no arrows are visible in them. Orientation is not mirrored (T stub check above), and direction comes from sim_core.
3. Markings sit on a 3 mm lift; no z-fighting seen at these distances, but no pixel depth offset is set on `M_Marking` (still open from the first set).
4. The floor ("grass") is a flat brown colour; the central reservation of the dual carriageway shows as the same brown, so it can look like a gap (look_05).
5. Road shadows on the floor 30 m below are large and black (floor is far below the road); expected, not a mesh fault.
6. Kerb-only ends near some junction mouths show small unfinished slivers (look_02 right side, look_06 right); not investigated.
