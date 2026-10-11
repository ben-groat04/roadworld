# Unreal M1: orbit camera over the seeded test world (11 Oct 2026)

Captured by `unreal/tools/py/screenshot_views.py` with argument `sandbox` (map `/Game/CityPainter/Maps/L_Sandbox`, seed 1, 1920x1080, RTX 4060, editor viewport, no play mode):

    $env:CP_OUT_DIR=<this folder>; unreal\tools\Run-EditorPython.ps1 -Script py\screenshot_views.py -Render -GraphicsAdapter 0 -ScriptArgs sandbox

| File | View |
|---|---|
| sandbox_01_overview.png | oblique from the south-east corner, 470 m up (world edge and dark void beyond it visible) |
| sandbox_02_valley.png | low, up the valley towards +y (north) |
| sandbox_03_steep_side.png | from the valley east side towards the west scarp |
| sandbox_04_low_oblique.png | 10 m up from the north-east looking south-west to the scarp |
| sandbox_05_straight_down.png | straight down from 2500 m, north up |

Build log numbers (editor log): sim world 190 ms; Ground layer 64 tiles (8 x 8, 64 cells = 256 m each), 524,288 triangles, 270,400 vertices; sim meshing 13-15 ms, total layer build 109-304 ms. Frame time in the captures 16.7-18.8 ms mean (RTX 4060, editor).

Headless play-mode proof (`UnrealEditor.exe CityPainter.uproject /Game/CityPainter/Maps/L_Sandbox -game -ExecCmds="CPPick 0.5 0.5,CPPick 0.1 0.9"`, log line `LogCPGame: CPPick`):
`CPPick 0.50 0.50 (viewport 1280x720): sim x 0.00 y -0.00 height -1.52 m slope 0.7 %` (the camera target at the world centre) and
`CPPick 0.10 0.90: sim x -163.73 y -646.88 height 9.22 m slope 23.7 %`. Also `-nullrhi` automation test `CityPainter.World.Terrain` passes.

## Look check (images opened: sandbox_01..05 and docs/progress/2026-10-11-test-world/seed1_hillshade_contours.png; seed1 only, no photo references involved)
- Straight-down view matches the hillshade: square world, one crescent of steep slope just west of centre (hillshade: x about -110 m), bulging east at its middle and fading at both ends, over roughly the same y range. North is up. Terrain tone is uniform (the ground material, not geometry).
- No holes, no seams between tiles, no lighting seams, no inverted faces visible in any of the five views (also checked numerically by the automation test: all 524,288 triangles CCW seen from above; shared tile borders coincide to < 0.01 cm).
- Not good: the ground material renders flat pinkish grey (MI_Ground, not olive) and the terrain ends at the 2 km edge, leaving a dark navy void to the horizon (overview). In view 03 the scarp reads weakly (eye height and focal length not tuned).
