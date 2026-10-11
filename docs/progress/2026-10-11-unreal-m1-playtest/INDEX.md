# Unreal M1 playtest (11 Oct 2026)

Milestone 1 (orbit camera, sim picking, HUD, ground layer in `L_Sandbox`) was exercised for the first time with real input. Driving method: **B** (scripted Slate mouse and keyboard events inside a real `-game` session, through the real viewport, Enhanced Input, controller and pawn) for everything that needs held buttons, wheel and pointer positions; **A** (live editor over Epic's MCP server on port 8000) for the editor-viewport, play-in-editor start/stop and leak checks. MCP has no console, held-key or wheel tool, see `unreal/tools/README.md` ("Driving the live editor over MCP").

Repeat everything: `powershell -NoProfile -ExecutionPolicy Bypass -File unreal\tools\Run-Playtest.ps1` (about 3 to 4 minutes; writes `results.json` and screenshots). The table below is `final/results.json` of the last run: **37 checks, 0 failed**, 1600x900, fixed 30 fps time step, RTX 4060, while a CFD job was running on the same machine.

Frame time while orbiting (wall clock, 307 frames): mean 18 ms, p95 19 ms, max about 400 ms (one first-frame hitch). Earlier identical runs while the CFD job was busier gave p95 27 to 80 ms, so the number is only a gross-regression guard (fails above 120 ms p95).

## Checks (control, expected, observed, result, image)
| Control | Expected | Observed | Result | Image |
|---|---|---|---|---|
﻿| cursor: shown, not locked | bShowMouseCursor, lock mode DoNotLock | show=1 lock=0 capture=3 | pass |  |
| rotate: right-drag +200 px | azimuth -1.396 rad (OrbitControls: one viewport height = one turn, dragging right decreases it) | azimuth -1.396 rad; polar change +0.000 | pass | [01_rotate_after.png](final/01_rotate_after.png) |
| rotate: left-drag -200 px | azimuth +1.396 rad | azimuth +1.396 rad | pass |  |
| orbit pitch: drag down +100 px | polar -0.698 rad (camera rises toward overhead) | polar -0.698 rad | pass |  |
| pitch limit: drag down far (overhead) | polar clamps at 0.05 rad, view looks down at the target, no flip | polar 0.050, forward.z -0.999, forward.target dot 1.0000, roll -0.00 deg | pass | [02_pitch_overhead.png](final/02_pitch_overhead.png) |
| pitch limit: drag up far (horizon) | polar clamps at 1.42 rad; camera at least 3 m above the ground; looking down, not flipped | polar 1.420, clearance 29.08 m, forward.z -0.150, roll -0.00 deg | pass | [02_pitch_horizon.png](final/02_pitch_horizon.png) |
| camera never under the terrain (15 m, polar 1.42) | clearance >= 3 m | clearance 3.00 m | pass | [02_near_ground.png](final/02_near_ground.png) |
| frame time while orbiting (wall clock) | p95 under 120 ms: only a guard against gross regressions, the number itself is informational (this machine also runs a CFD job; p95 varied 27..80 ms between identical runs; max includes a first-frame hitch) | mean 18.2, p95 19.2, max 402.4 ms over 307 frames | pass |  |
| pan: middle-drag +100 px at 20 m | ground follows the cursor: old target moves +100 px on screen; target moves 2.07 m; target z on the ground | old target moved +100.0 px; target moved 2.07 m (100% of expected); target z error 0.00 cm | pass |  |
| pan: middle-drag +100 px at 400 m | ground follows the cursor: old target moves +100 px on screen; target moves 41.45 m; target z on the ground | old target moved +100.0 px; target moved 41.45 m (100% of expected); target z error 0.00 cm | pass | [03_pan_after.png](final/03_pan_after.png) |
| pan: middle-drag +100 px at 3000 m | ground follows the cursor: old target moves +100 px on screen; target moves 310.87 m; target z on the ground | old target moved +99.1 px; target moved 310.87 m (100% of expected); target z error 0.00 cm | pass |  |
| zoom: wheel +5 notches (pointer off centre) | distance 452.5 m (x0.95^1.1 per notch); zooms toward the screen centre like the prototype: target unchanged | distance 452.5 m; target moved 0.00 cm | pass | [04_zoom_after.png](final/04_zoom_after.png) |
| zoom: minimum distance | 15 m, camera 3 m above ground | 15.00 m, clearance 8.09 m | pass | [04_zoom_min.png](final/04_zoom_min.png) |
| zoom: maximum distance | 3800 m | 3800.0 m | pass | [04_zoom_max.png](final/04_zoom_max.png) |
| keyboard pan: W for 1 s at 400 m | moves 360 m forward (0.9 x distance per second) | moved 372.0 m, forward dot 1.000 | pass |  |
| keyboard pan: Shift+W | 3x the plain pan | 1116.0 m vs 372.0 m, ratio 3.00 | pass |  |
| keyboard pan: A (left) | moves in the matching ground direction | direction dot expected 1.000, moved 192.0 m | pass |  |
| keyboard pan: S (back) | moves in the matching ground direction | direction dot expected 1.000, moved 192.0 m | pass |  |
| keyboard pan: D (right) | moves in the matching ground direction | direction dot expected 1.000, moved 192.0 m | pass |  |
| keyboard pan: Up arrow | moves in the matching ground direction | direction dot expected 1.000, moved 192.0 m | pass |  |
| keyboard pan: Down arrow | moves in the matching ground direction | direction dot expected 1.000, moved 192.0 m | pass |  |
| keyboard pan: Left arrow | moves in the matching ground direction | direction dot expected 1.000, moved 192.0 m | pass |  |
| keyboard pan: Right arrow | moves in the matching ground direction | direction dot expected 1.000, moved 192.0 m | pass |  |
| keyboard rotate: E for 1 s | azimuth -1.3 rad (prototype: camera orbits clockwise seen from above), target fixed | azimuth -1.343 rad, target moved 0.00 cm | pass |  |
| keyboard rotate: Q for 1 s | azimuth +1.3 rad | azimuth +1.343 rad | pass |  |
| world edge: Shift+W north until clamped | target stops at the world edge; camera stays 3 m above the ground | target x 102400.0 of max 102400.0 cm; camera x 88419.4; clearance 54.64 m | pass | [05_edge.png](final/05_edge.png) |
| pick 1 (0.50, 0.50) | pick height equals sim height at the picked x,y (cm); point reprojects to the pointer (px); HUD text equals the pick (cm) | sim x 0.00 y -0.00 h -1.519 m; height error 0.00 cm; surface z error 0.00 cm; reprojection 0.00 px; HUD vs pick 0.1 cm | pass | [06_pick_centre.png](final/06_pick_centre.png) |
| pick 2 (0.25, 0.30) | pick height equals sim height at the picked x,y (cm); point reprojects to the pointer (px); HUD text equals the pick (cm) | sim x -239.83 y -47.62 h 51.365 m; height error -0.00 cm; surface z error -0.00 cm; reprojection 0.00 px; HUD vs pick 0.5 cm | pass |  |
| pick 3 (0.75, 0.30) | pick height equals sim height at the picked x,y (cm); point reprojects to the pointer (px); HUD text equals the pick (cm) | sim x 111.96 y 320.16 h -4.105 m; height error 0.00 cm; surface z error 0.00 cm; reprojection 0.00 px; HUD vs pick 0.5 cm | pass |  |
| pick 4 (0.25, 0.80) | pick height equals sim height at the picked x,y (cm); point reprojects to the pointer (px); HUD text equals the pick (cm) | sim x -31.67 y -224.17 h -0.491 m; height error 0.00 cm; surface z error 0.00 cm; reprojection 0.00 px; HUD vs pick 0.5 cm | pass |  |
| pick 5 (0.75, 0.80) | pick height equals sim height at the picked x,y (cm); point reprojects to the pointer (px); HUD text equals the pick (cm) | sim x 220.41 y -50.20 h -2.210 m; height error -0.00 cm; surface z error -0.00 cm; reprojection 0.00 px; HUD vs pick 0.1 cm | pass |  |
| HUD updates every frame | text changes on at least 9 of 11 frame steps while the pointer moves | 11 of 11 changed; last: x -35.37 m   y -130.17 m   height -0.51 m   slope 2.0 % (1.2 deg) / frame 33.3 ms (30 fps) | pass | [06_hud.png](final/06_hud.png) |
| pick over the sky | no pick, HUD says the cursor is not over the ground | pick=0 hud: cursor not over the ground / frame 33.3 ms (30 fps) | pass |  |
| HUD does not swallow clicks: right-drag starting on the HUD panel | azimuth -0.698 rad | azimuth -0.698 rad | pass |  |
| window resize | viewport becomes about 1000x600 | 1000x600 | pass |  |
| pick after resize (0.60, 0.70) | pick height equals sim height at the picked x,y (cm); point reprojects to the pointer (px); HUD text equals the pick (cm) | sim x 120.56 y -64.82 h -1.413 m; height error 0.00 cm; surface z error 0.00 cm; reprojection 0.00 px; HUD vs pick 0.3 cm | pass | [07_resized.png](final/07_resized.png) |
| rotate after resize: right-drag +150 px | azimuth -1.571 rad (uses the new height) | azimuth -1.571 rad | pass |  |

## Play-in-editor and editor viewport (MCP, `editor/`)
| Check | Expected | Observed | Result | Image |
|---|---|---|---|---|
| Terrain in the editor viewport without Play | ground visible in the level editor | green ground with skirt to the horizon, 1 transient `CP_RenderRoot`, 65 procedural mesh components (64 tiles + skirt) | pass | [pie_editor_before.png](editor/pie_editor_before.png) |
| PIE start/stop twice in a row | no duplicate terrain, no leaked actors | each PIE world built exactly 1 render root and 65 mesh components (24 actors after build, 20 before teardown, identical in both sessions); editor world still has exactly 1 root after both sessions; `IsPIERunning` false after each stop | pass | [pie_running_0.png](editor/pie_running_0.png), [pie_leak_check.json](editor/pie_leak_check.json) |

## Bugs found and fixed
| Bug (before: [before-fixes/](before-fixes/)) | Fix |
|---|---|
| Middle-drag pan was 24 percent too fast: the camera FOV of 60 is horizontal in UE (the prototype uses 50 vertical). Checked: the old target moved 177.7 px on screen for a 100 px drag. | `CPCameraPawn.cpp:32-37`, `:78`. Verified in `CameraStackTypes.cpp` that with `AspectRatio_MaintainYFOV` the FOV is still the horizontal angle at the camera aspect, so the horizontal angle for a 50 degree vertical one is set; pan uses the 50 degree constant. Now 100.0 px and 100 percent at 20 / 400 / 3000 m. |
| Q/E rotated the wrong way (E turned counter-clockwise; prototype: clockwise). | `CPCameraPawn.cpp:112` (`Azimuth -= ...`). |
| Ground was flat grey-brown: `MI_Ground` reads vertex colour G as soil amount and the terrain mesh has no colours (white = all soil). | `CPRenderSettings.h:22` default material `MI_Ground_Floor` (VertexDirtWeight 0): grass with the existing world-space texture coordinates (planar projection; at the 2 km world's slopes the stretch is under a few percent, a triplanar variant is only needed for steep terrain). |
| Dark void beyond the world edge. | New layer `FCPGroundSkirtLayer` (`CityPainterRender/Private/CPGroundSkirtLayer.cpp`, registered in `CityPainterRender.cpp`): the ground continues 20 km at the edge height (`SkirtDistanceM`, `CPRenderSettings.h:30`), purely visual (picking and camera limits ignore it). |
| Red on-screen warning "Cached lighting in Lumen ... clipped" on every frame (fixed EV 14 is outside the default range). | `Config/DefaultEngine.ini:114` `r.EyeAdaptation.CachedLightingPreExposure=8`. |
| HUD showed 0.1 m precision (centimetre comparison with the sim impossible). | `CPHudWidget.cpp:47`: two decimals. |
| HUD panel could swallow a click that starts over it. | `CPHudWidget.cpp:23` panel `HitTestInvisible` (two runs with the whole widget set invisible to hit tests lost all mouse input, but the same loss happened later without that change, see the flakiness note, so that link is unproven). Covered by the "HUD does not swallow clicks" check; not observed failing before the fix. |

## Not tested with this method, and why
- The real OS cursor (visible, arrow shape) and a real mouse: events are synthesised in Slate and screenshots do not include the OS cursor. Verified only the settings: `bShowMouseCursor`, no lock, capture mode `CaptureDuringMouseDown` (the mouse is captured only while a button is held, the cursor stays visible).
- Real input device timing, high-DPI scaling, multi-monitor, fullscreen.
- Visual taste: images were looked at, but the grass material shows a faint tile grid at grazing angles and Lumen cloud shadows are strong at far zoom (known material faults, unchanged).
- Frame time on the integrated GPU or at 4K; numbers were taken at 1600x900 on the RTX 4060 with a CFD job competing.
- **Known flakiness of the playtest itself (unresolved)**: in about 4 of 11 full runs ALL synthetic mouse input was lost from the first drag on (pointer never seen by the viewport, `GetMousePosition` false, azimuth change 0), while keyboard, wheel and clamps still worked; once only the steps after the window resize failed. The other runs passed 36 or 37 of 37. Not caused by the game code (same binaries pass and fail). Suspected: the operator's real mouse (or window placement) interfering with the Slate cursor state; `Run-Playtest.ps1` now passes `-WinX=20000 -WinY=20000` and the script re-parks the window after the resize, which did not remove it. A failed run shows many `no hit` and azimuth 0 results at once: rerun before believing a regression. A second observation: a game started while another Unreal build holds the UBT mutex can sit at "Launching UnrealBuildTool ... ValidatePlatforms" forever (seen once, killed after 24 min).
- Another agent was building `roadworld-wt-m2` with Unreal during the last hour (the single Unreal lane was not respected by the caller), which also queued my last diagnostic build; those diagnostics were dropped.
