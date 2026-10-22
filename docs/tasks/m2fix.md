# Unreal milestone 2 fixes: the auditor's 9 items for the road tool (HUD at the held point, refused-crossing ghost, open-stroke undo, glyphs, grade readout) and a harness that asserts what the pictures show

## Owner's words
11 Oct: "Screenshots. It opened only 3 of the prototype screenshots I listed and took the rest from the written spec. disappointing"
11 Oct: "remember a lot of these features were in the inital html, that ui was sort of what i want, that was the point of it"
11 Oct: "i will never open it, this takes too long. just mess around with it yourself to test it (ideally in an agent)"
11 Oct: "there's so much waiting"
11 Oct: agents "should anticipate what the cold read viewer might say and just get round to fixing that also before it actually tries to get its work audited"

## Cases
Milestone 2 (click a road, undo/redo) mechanics were accepted by the auditor; the finished-road look was NOT accepted, and the auditor listed 9 items that belong to M2 plus a harness strengthening (section "For the next Unreal builder" of the M2 AUDIT.md, copied in Faults below). This job fixes those and nothing from M3 (profile panel, tuning panel, edit mode, colour modes: the separate `m3ui` job) and nothing from earthworks (asphalt under terrain: the grading rounds). Each state below is a case: reproduce it in the game with real mouse and keys at the prototype screenshot's framing and compare item by item (text, order, colours, ghost, rings).
Open ALL of these prototype screenshots (M = 14 screenshots + 11 documents = 25):
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\04_midstroke.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\12_error_min_span.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\13_error_too_tight.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\14_advisory_radius.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\15_advisory_grade.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\16_advisory_bridge.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\17_advisory_tunnel.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\18_type3_dual_ghost.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\19_type4_motorway_ghost.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\20_type3_single_carriageway_O.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\21_type1_oneway.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\22_type2_b_road_ghost.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\26_stroke_3pts_before_finish.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\28_error_crossing_too_close.png
Documents (open all, name them):
- C:\Users\bgbg0\Documents\Programming\roadworld-wt-m2\docs\progress\2026-10-11-unreal-m2-run\AUDIT.md (the 9 items and the harness strengthening are in "For the next Unreal builder"; also read the auditor's own fault list and which pictures it opened)
- C:\Users\bgbg0\Documents\Programming\roadworld-wt-m2\docs\progress\2026-10-11-unreal-m2-run\STATUS.md
- C:\Users\bgbg0\Documents\Programming\roadworld-wt-m2\docs\progress\2026-10-11-unreal-m2-run\FAULTS.md
- C:\Users\bgbg0\Documents\Programming\roadworld-wt-m2\docs\progress\2026-10-11-unreal-m2-run\COMPARE.md (open its composites in `compare\` too: s04, s12 to s22, s26, s28 `_vs_proto.jpg`)
- C:\Users\bgbg0\Documents\Programming\roadworld-wt-m2\docs\progress\2026-10-11-unreal-m2-run\hud_text.txt
- C:\Users\bgbg0\Documents\Programming\roadworld-wt-m2\docs\progress\2026-10-11-unreal-m2-run\results_m2states.json
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\unreal-ui-parity.md (HUD text, order and colours per state)
- C:\Users\bgbg0\Documents\Programming\roadworld\unreal\tools\README.md (build, editor and playtest commands) and C:\Users\bgbg0\Documents\Programming\roadworld\unreal\tools\KNOWN_RISKS.md
- C:\Users\bgbg0\Documents\Programming\roadworld\prototypes\road-draw\src\ui.js
- C:\Users\bgbg0\Documents\Programming\roadworld\prototypes\road-draw\src\input.js (name the functions that build each HUD line and handle the cancel and undo keys)
Start: worktree C:\Users\bgbg0\Documents\Programming\roadworld-wt\m2fix, branch `agent/m2fix`, cut from main AFTER the roadtool3 merge (`merge/roadtool3`) has landed (the merge changes `DrawParams::finish_pick_px` to default 0, so a click near the last point adds a point and finishing is Enter, right-click or a snap; the host code and the playtest were written against the old behaviour, and the merger/auditor of roadtool3 may have left Unreal-side notes: read the merger's STATUS first). If main has not got the merge yet, wait and say so; do not branch from the old M2 branch.
Scope fence with the M3 UI job (`docs/tasks/m3ui.md`, runs after this one, same Unreal files): m3ui deletes `UCPToolHudFormat::BuildLines` and replaces it with `cp::draw_hud` / `cp::edit_hud`, deletes the finish ring in `CPOverlayComponent.cpp` and the `would_finish` branch in `CPToolSession.cpp`. So for items 1 and 2 below, first check what `finish_pick_px = 0` does after the merge: if the "last placed point finishes the road" behaviour no longer exists, items 1 and 2 are already moot: say so with the evidence (a picture and the HUD text of the state), fix only whatever the HUD still prints wrong in that state, and do NOT build new code on `BuildLines` that m3ui will delete. Everything else is yours: you own `CPRoadDrawTool.*`, `CPToolSession.*` (plugin, Sim), `CPPlaytest*.cpp/.h`, the tool/HUD files named in the items, and the playtest scripts. Do not change ground/sky/materials, no sim_core junction or grading code, and no sim_core change at all unless an item cannot be done otherwise (then list it).
UNREAL LANE (one Unreal build, editor or -game process at a time on this machine, several agents share it): before any UBT build, editor or game launch, take the lock by creating the directory C:\Users\bgbg0\Documents\Programming\roadworld\.claude\state\unreal_lane.lock (mkdir fails if it exists: then wait and retry every 60 s; also wait while any UnrealEditor, UnrealBuildTool or dotnet UBT process that you did not start is running), write a file owner.txt inside with your branch name and the time, and remove the directory as soon as your build or run ends, every time, including on failure. Never hold it while you are only editing or thinking. If it is older than 60 minutes and no Unreal process runs, say so in STATUS.md, then take it. Build and run commands: unreal\tools\README.md. You drive the editor or -game yourself with `-CPPlaytestStates` and the MCP/Python tools (`unreal\tools\mcp_call.py`); you never ask the owner to open Unreal or to play. Research every Unreal fact (fonts, glyphs, input, line batching, widgets) from C:\UE_5.8 source or docs, never from memory.

## Faults
Gaps from the M2 auditor (the screenshots decide, not this list; numbers are the auditor's):
1. `UCPToolHudFormat::BuildLines`: when the pointer is on the last placed point (`bWouldFinish`), still print the span / bearing / R / grade / total readouts of the held ribbons (state s26, prototype 26). Add "G2" only after the sim_core merge (it is on main once roadtool3 landed: use it if the engine now provides it).
2. Cue text: when `PointCount() < 2` and `bWouldFinish`, do not print "Click again here to finish the road"; print "Click to add the next point" (`CPToolHudFormat.cpp` line about 108).
3. Refused crossing (s28, prototype 28): show the red ghost ribbon for the stroke the refusal came from and keep the text; today only text and a ring are visible. Decide where: `UCPRoadDrawTool::OnClick` / `Feedback.Preview`.
4. Open-stroke undo: decide and implement (cancel the stroke on Ctrl+Z, or undo the last point first, or keep parity with the prototype and say so in `docs/`); the prototype's Backspace removes the last point and Esc cancels; add a playtest check.
5. Replace the "!" and "X" prefixes with the prototype's glyphs (see prototype 12 to 17) if the Unreal font has them; otherwise log which font lacks them and which font was tried (check the engine's font assets and Slate/UMG font fallback in `C:\UE_5.8`).
6. Investigate the max-grade readout on flat ground (states s04, s14, s18 to s22): print `Edge` grade samples for the s18 stroke and compare with a hand computation; fix or explain with numbers.
7. Strengthen the harness (`CPPlaytestRoads.cpp`, `CPPlaytestRoadStates.cpp`, `CPPlaytestRoadSteps.h`):
   - add `AssertHud(Label, Contains...)` and assert, per state, the snap word ("snap: end", "snap: road", "snap: guide", "snap: angle", "snap: free"), the refusal texts ("segment shorter than 4 m", "too tight", "crossing too close"), the advisory words, and "single carriageway" / "one-way" in the header (exact strings from the screenshots and `docs/unreal-ui-parity.md`);
   - "Esc cancels": assert `PointCount() == 0` (add an accessor on `UCPPlayerController` / tool) instead of unchanged counts;
   - "A drawn" etc.: also assert a road surface mesh exists (count of vertices or sections from `UCPRenderSubsystem`) and that the first and last control points are at the clicked XY within 0.5 m;
   - "everything undone": assert the exact number of commands undone (the undo stack depth), not 8 blind presses;
   - add scenarios: undo during an open stroke; Enter and click on a one-point stroke; shallow-angle crossing; road over a slope; ghost over a hill (check the ghost material has depth test off and take a picture over the terrain edge).
8. The playtest prints one line "MOUSE INPUT LOST: rerun" when the early M1 checks all fail together, and reruns once automatically; today it prints 14 separate failures (the machine loses all synthetic mouse input in about 4 of 11 runs, cause unknown: look for the cause in `CPPlaytestInput.cpp` and window focus before only masking it, and report what you found).
9. Pictures: ghost through hills (s16/s17 on real terrain, as prototype 16/17) and a refusal picture for a real over-water bridge advisory.
Not in this job (stay as gaps, list them unchanged in STATUS.md): the M3 items (profile panel, G2 tick, total ribbon width, tuning/perf/help panels, colour modes, edit mode, offset-guide arc, line-batch overlay replacement), the earthworks items (regrade after commit, road profile against ground, bridges and tunnel meshes, vertical-gap check), the sandbox look.
Playtest gates: milestone 1 and 2 checks must still pass 51/51 after your changes (rerun once before believing a mouse-input failure; report every rerun), plus your new assertions; each new assertion shown to FAIL on the pre-fix behaviour (state which build/commit you used to show it). Composites (game | prototype) for every state you touched, opened and named. Clean build of the plugin with `unreal\tools\Build-Editor.ps1 -EngineRoot C:\UE_5.8 -MaxParallelActions 1`; no new compiler warnings.

## Launched
This job: one Unreal builder (Sonnet), then a fresh auditor, then a merger. Independent items: the HUD and cue fixes (1, 2, 5, 6), the refused-crossing ghost (3), the open-stroke undo (4), the harness work (7, 8) and the two pictures (9) can be coded in parallel in one worktree by one agent; they all need the Unreal lane for builds and runs, so they are one lane, not several agents. It starts after the roadtool3 merge lands and the Unreal lane is free (pvetrees, which has no agent running yet, also needs the lane: one at a time, pvetrees only needs it for editor sessions).

## Waiting
Unreal M3 UI (`docs/tasks/m3ui.md`): waits for this job to merge (same tool, HUD and playtest files) and for the roadtool3 merge. Sandbox look (`docs/tasks/uelook.md`): held until the free assets are chosen. Earthworks game wiring: waits for grading round 10 and gradelayer round 2.
