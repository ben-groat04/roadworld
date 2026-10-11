# Index (Unreal milestone 2 run)

- renders/20_st_*.jpg: every road tool state at 1600x900 (s02 idle .. s28 crossing, s27b/s27c finished road, s30/s31 road on a slope).
- renders/10_m2_*.jpg: A, S, T roads, snap, undo x3, redo x2, right-click and right-drag.
- compare/*_vs_proto.jpg: render | prototype screenshot. before/: same camera before this run's fixes.
- hud_text.txt: the tool HUD text of every state. results_m2states.json, results_full.json: playtest results.
- Repeat: powershell -Command "& unreal/tools/Run-Playtest.ps1 -ExtraArgs @('-CPPlaytestM2Only','-CPPlaytestStates')", then unreal/tools/Compose-M2States.ps1 -Run (out dir) -Out (dir).
