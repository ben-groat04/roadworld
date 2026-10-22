# INDEX: roadtool3 (engine work)

Branch `agent/roadtool3` (worktree `C:\Users\bgbg0\Documents\Programming\roadworld-wt-roadtool3`), cut from `integ/2026-10-11` at e0c0d4b. No 3D renders (engine work, the 45 prototype screenshots are the spec). The "render" of each screenshot is the text sim_core produces for its scenario: `renders/NN_name.txt` (45 files, written by `CP_TOOLUI_DUMP=<dir> sim_core_tests -sf="*test_tool_ui*"`; 24, 25 and 37 to 40 say they are host or other agents' work). `before/NN_name.txt` is what the base commit gave for the same scenario. Composites (screenshot beside the render text and the before text) are in `C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\_compare\roadtool3\NN_name.png` (private, gitignored). `COMPARE.md` is the five-column table, `COMPARE_DETAIL.md` the per-row explanation (sim_core field and pinning test).

## New files
- `sim_core/include/cp/tool_hud.h`, `sim_core/src/tool_hud.cpp`: HUD lines (`draw_hud`, `edit_hud`, `error_hud_line`, `warning_hud_line`), `snap_kind_word`, profile title / empty text / axes / bands, `ui_theme()`, `ring_style`, `ghost_colour`, help rows and paragraph, inspect bar text.
- `sim_core/include/cp/road_heat.h`, `sim_core/src/road_heat.cpp`: colour modes, heat classes and colours, `heat_vertices`.
- `sim_core/include/cp/tool_tuning.h`, `sim_core/src/tool_tuning.cpp`: the eight tuning fields, defaults from the region, `apply_tuning`, `tuned_region`.
- `tests/test_tool_ui.cpp`: 33 cases pinning the screenshots (texts, snaps, guides, errors, advisories, widths, heat, tuning, profile axes, edit HUD, road chain, junction-making edits, world margin, finish gestures, fit refusal); also writes the `renders/` texts when `CP_TOOLUI_DUMP` is set.

## Changed files
- `sim_core/include/cp/road_tool.h`, `sim_core/src/road_tool.cpp`: `finish_pick_px` default 0, `DrawPreview` carries the road type name / one-way / dual flag, advisory words (`8%`, `hud_name()`), water level in `ProfileSeries`, `profile_of_geometry`, `join_profiles`.
- `sim_core/include/cp/edit_tool.h`, `sim_core/src/edit_tool.cpp`: road chains, selection info, `hover_chain`, whole-road handles / profile / delete / type / one-way, `select_at`, junction-making edits (interior handles, free ends, point height), `make_junctions`, `chain_selection`, `water_level`.
- `sim_core/src/clothoid_chain.cpp`, `sim_core/include/cp/clothoid_chain.h`, `sim_core/src/stroke.cpp`, `sim_core/include/cp/stroke.h`: runaway-chain refusal moved into `fit_clothoid_chain` (`fit_sane_chain` no longer a stopgap).
- `sim_core/include/cp/region_pack.h`, `sim_core/src/region_pack.cpp`, `data/regions/uk/region.json`, `data/regions/uk/road_types.json`: `road_tool` rules (snap, min span, water level, K multiplier, chain turn limit, heat-map classes and colours), `RoadType::short_name`.
- `sim_core/CMakeLists.txt`, `tests/CMakeLists.txt`: new sources and test file.
- `tests/test_road_tool.cpp`, `tests/test_edit_tool.cpp`: the finish cue is opt-in now (`finish_pick_px = 10` set where the cue is tested); the edit-makes-no-junction test became the junction-making test.
- `docs/unreal-ui-parity.md`: section H (GAP status, new interface, declared differences, host-side list for milestones 2 to 3).
- `docs/progress/roadtool3/`: STATUS.md, FAULTS.md, COMPARE.md, COMPARE_DETAIL.md, INDEX.md, `renders/`, `before/`.

## Not touched (other agents)
Junction and markings files, grading and earthworks files, world_cache files, Unreal project.
