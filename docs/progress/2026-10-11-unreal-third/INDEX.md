# Third Unreal set: cameras, scene, materials (2026-10-11)

UE 5.8.3, 1920x1080 stills. Images: `stage1_cameras_scene/`, `stage2_asphalt/` (asphalt defaults before the last tint tweak: Cool 0.965/1.04, fringe 1.0), `stage3_kerb_footway_marking/` (final state; markings visible after a plugin rebuild). Nothing here is scored.

## Regenerate
`unreal\tools\Build-Editor.ps1 -EngineRoot C:\UE_5.8 -MaxParallelActions 1` (rebuild whenever sim_core or `data/` changed: a stale binary hid all markings), then `unreal\tools\First-Run.ps1 -Only level -EngineRoot C:\UE_5.8 -MaxParallelActions 1` (rebuilds all materials, assigns them, saves the level), then `-Only screenshots`. Output `unreal\CityPainter\Saved\Screenshots\look\`.

## Plugin C++ change (only one)
`CPRoadMeshBuild.h` (FSection::UV1), `CPRoadMeshBuild.cpp` (ConvertBuffers fills UV1; marking vertex colour is now R=wear 0 instead of 255), `CPRoadMeshActor.cpp` CreateSection (passes UV1 to `CreateMeshSection`). Vertex colour R/G/B and UV1 now reach the materials.
NOTE: UV1.x is metres from the lane centre (not 0..1 as the brief said), UV1.y the lane width (0 = not a lane); wheel tracks are at |x| = 0.9.

## Stage 1: cameras and scene (done)
- Views: aerial 296 m up at 58 degrees, 35 mm; roundabout oblique 50 mm; roundabout approach, T-junction arm, slip-road driver view, all 24 mm (hfov 73.7) at 1.8-2.2 m; kerb close-up 35 mm, 1.4 m up, ~3 m from the kerb; new `look_08_dual_carriageway` (driver height, eastbound) and `look_09_tee_oblique` (15.5 m up). `look_07_underside` only with the `underside` argument.
- Ground: `CP_Floor` 40 km plane at z = -45 cm with `MI_Ground_Floor` (STAND-IN until graded terrain is rendered; commented in the script). The sim's grass verge/island meshes use `MI_Ground`.
- `M_Ground`: grass diffuse/normal/ARM at two scales/rotations blended by noise (27 m), field-scale tone patches (190 m contours), macro dry/green noise, soil patches, vertex-G soil. 151 nodes, 7 texture samples, 5 noise nodes. Parameters: TileMetres, Tile2Scale, FieldPeriodM, DryAmount, FieldContrast, SoilAmount, VertexDirtWeight, NormalStrength, GrassDesaturate 0.85, SoilDesaturate 0.75, GrassTint (0.85,1.25,0.72), DryTint (1.7,1.65,1.2).
- Light and post (constants at the top of `setup_look_test_level.py`): sun 45 degrees elevation travelling toward yaw 40 (sun in the south-west), 100000 lux, 1.2 degree source angle; sky light real-time capture, intensity 3.0 (1.0 left shadows black); fixed exposure EV100 14 (min = max); bloom 0.2, vignette 0.15, no fringe, film grain 0.06, no motion blur; Lumen and VSM on; height fog density 0.006 with pale haze colour; volumetric fog off; atmosphere ground albedo grey-green.

## Stage 2: `M_RoadAsphalt` (done)
351 nodes, 10 texture samples (asphalt_01 diff/normal/ARM x2 scales, asphalt_02 diff/normal/height, gravel diff), 10 noise nodes; shader instruction counts are not readable from Python right after compile (zeros): check in the material editor. Instances: `MI_RoadAsphalt` (used), `_New`, `_Worn`, `_Patched`.
Parameters by group: Textures (TileBaseM 2.08, TileWornM 3, Tile2Scale 0.73, BlendPeriodM 27); Colour (Desaturate 0.7, CoolTint (0.98,1,1.025), AlbedoScale 1.05, MacroStrength 0.15, MacroPeriodM 55, VariationStrength 0.3 driven by vertex B); Tracks (TrackCentreM 0.9, TrackWidthM 0.35, TrackDarken 0.12, TrackRoughDelta -0.10, TrackFlatten 0.3, CentreBrighten 0.04, CentreRoughAdd 0.05); Wear (WearStrength, WearBias, WornBlendStrength 0.4, CrackSealAmount 0.25, CrackDensity, CrackThreshold, PatchAmount 0.07, PatchCellM 6, PatchAlbedoDelta 0.1, PatchRoughDelta 0.08); Edge (EdgeDirtDarken 0.28, EdgeDirtRoughAdd 0.12, EdgeRagged 0.6, FringeAmount 0.6, FringeThreshold 0.76, FringeGravelScale 0.35, FringeTint); Surface (RoughMin 0.66, RoughMax 0.9, NormalStrength, OilAmount 0 = off). Instance overrides: Worn (WearBias 0.25, PatchAmount 0.10, cracks 0.7), Patched (PatchAmount 0.35), New (patches 0.02).
Tracks are darker (about 12%) and smoother; centre strip paler. Texture colour measured and neutralised: see the table below.

## Stage 3: kerb, footway, marking (done)
- `M_Kerb`/`MI_Kerb`: concrete_wall_009 desaturated 60%, albedo x0.62 (neutral grey), 0.915 m joints, per-unit and macro tone, base grime band (v < ~45 mm). 102 nodes, 3 samples.
- `M_Footway`/`MI_Footway`: concrete_pavement desaturated 65%, x0.85, TileMetres 1.5 (flags about 500 x 250 mm), no rotation anti-tiling (it shears joints), two noise tones, dirt blotches. 86 nodes, 3 samples. Splitter islands are the sim's `paving` class so they use this too (small grey flags, no longer huge cream ones); real splitters are mostly tarmac: a sim-side change.
- `M_Marking` (masked, pixel depth offset via MakeMaterialAttributes, -2 cm, works: no z-fight seen) with `MI_Marking_white/yellow` and `_Old` variants (Erosion 0.40). Erosion default 0.10 (5-15%), ChipScaleM 0.22, PaintBrightness 0.92 (final albedo about 0.73 from the region's 0.90 sRGB), Dirt 0.18, WearInfluence (vertex R, 0 until the sim writes marking wear), NormalStrength 0.4. 78 nodes.

## Measured texture colour (sRGB mean) and what the materials do
| Set | Mean | Treatment |
|---|---|---|
| asphalt_01 | (89,80,70) warm | desaturate 70%, cool tint (0.98,1,1.025), x1.05 |
| asphalt_02 | (90,90,84) | same treatment |
| sparse_grass | (79,61,21) brown | desaturate 85%, tint (0.85,1.25,0.72) |
| forest_ground_06 | (90,68,47) | desaturate 75%, x0.85 |
| gravel_floor_02 | (171,167,156) | desaturate 30%, x0.35, tint (1,0.92,0.78) fringe |
| concrete_pavement | (127,116,102) | desaturate 65%, x0.85, cool (0.97,1,1.03) |
| concrete_wall_009 | (161,153,139) | desaturate 60%, x0.62 |
`MANIFEST.md` / `manifest.json` notes corrected.

## Known problems (obvious breakage only)
1. Thin dark-blue band at the horizon persists (atmosphere beyond the plane); fog tuning did not remove it.
2. Pure-black or very dark shadow slabs beside raised roads and in the median (software Lumen has no data for the runtime road meshes; sky light 3.0 only lifts them partly). Needs a human or the MCP to decide between RT Lumen proper, Nanite/static road meshes, or an ambient lift.
3. Minor road on its bridge/approach floats 0-7 m above the flat ground (no embankments until grading is meshed); ground shows a cross-hatch weave pattern in the aerial view (anti-tile blend of the grass maps).
4. Edge fringe is wide and blotchy in junction mouths (G resolved by large junction triangles).
5. Junction mouths show few or no markings (sim). Roundabout approach shows a single dark line (kerb/gap).
6. Asphalt can read slightly mauve in shade (sky fill is blue, texture tint is cool); no grass lapping over kerbs (needs geometry or vegetation); look_07 not in the default set.
7. Shader instruction counts unknown (see stage 2).
