# First Unreal screenshots of the generated road network (2026-10-10/11)

Engine UE 5.8.3, DX12 SM6, adapter 0 = NVIDIA RTX 4060 Laptop (chosen by default, no `-GraphicsAdapter` needed). 1920x1080, level `/Game/Maps/RoadTest` (Open World template, Landscape and SM_SkySphere stripped), one `CPRoadMeshActor`: 19 nodes, 21 edges, 12 junctions, 10 tiles, 43 sections, 445 marks; triangles asphalt 8542, grass 654, kerb 2856, paving 2362, white markings 5494 (built in about 200 ms).

| File | Shows |
|---|---|
| look_01_aerial.png | 750 m straight down: only the 2 km floor (grass material, z=-30 m). Roads not visible (see problem 1). |
| look_02_roundabout_oblique.png | roundabout from ~100 m: kerb faces and markings show as thin lines, road tops missing |
| look_03_roundabout_approach.png | driver height on the bridge approach: kerb/marking slivers, one wide marking strip |
| look_04_tee_minor_arm.png | T junction view |
| look_05_slip_merge.png | slip merge, mostly floor |
| look_06_kerb_closeup.png | kerb close-up |

Regenerate: `powershell -NoProfile -ExecutionPolicy Bypass -File unreal\tools\First-Run.ps1 -Only level -EngineRoot C:\UE_5.8` then `-Only screenshots`. Output lands in `unreal\CityPainter\Saved\Screenshots\look\`.

## Known problems
1. **Road top surfaces are not rendered (likely inverted winding / back-face culled).** With the floor deleted, the roads vanish and the blue sky-atmosphere ground shows through; only vertical kerb faces and marking edges are seen. This is C++ (`CPRoadMeshBuild.cpp` ConvertBuffers index swap, KNOWN_RISKS H14: remove the swap for surfaces and markings). Not fixed here (C++ out of scope).
2. Floor and (invisible) roads cannot be told apart until 1 is fixed; the flat brown colour is the grass MI on the floor.
3. No pixel depth offset on `M_Marking` (enum hidden from Python); markings rely on the 3 mm lift.
4. Horizon shows a blue band beyond the 2 km floor (sky atmosphere ground).
5. Shots are 1920x1080 viewport captures (no TSR history warm-up beyond 120 frames).
