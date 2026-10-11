# Unreal sandbox look: ground, sky and light so a drawn road sits in a believable landscape

## Owner's words
11 Oct: "i think this procedural world gen will be worked into a map editor far later, not sure how much more actually needs to be done, enough to act as a background for photorealism in the roads once the road builder is integrated into here. remember (should be in docs already) that the photorealism must come from adding the road builder into this landscape, nothing else"
11 Oct: "yes search far and wide for the best free assets you can. for everything. no idea why thats even a question."
11 Oct: "there's so much waiting"
11 Oct: "i will never open it, this takes too long. just mess around with it yourself to test it (ideally in an agent)"

## Cases
The game's sandbox (map /Game/CityPainter/Maps/L_Sandbox) seen from three cameras: (1) builder's view 150-300 m up looking down at 45 degrees, (2) low oblique 20 m up along a road, (3) driver's eye on a finished road. Target: UK lowland farmland in summer daylight.
Files to open, all of them (M = 12):
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\aerial\19_a23_handcross_lane_green_countryside.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\aerial\21_sixhills_lane_hedged_fields.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\aerial\11_a68_grade_separated_roundabout_fields.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\aerial\26_a9_gelly_overbridge_earthworks_scotland.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\aerial\INDEX.md and C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\INDEX.md (pick at least 3 more photos per camera that match its height and angle; open and name them; fetch more if a camera has fewer than 3 matches)
- C:\Users\bgbg0\Documents\Programming\roadworld-wt-m2\docs\progress\2026-10-11-unreal-m2-run\renders\20_st_s30_road_on_slope.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld-wt-m2\docs\progress\2026-10-11-unreal-m2-run\renders\10_m2_06_t_snap_to_road.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld-wt-m2\docs\progress\2026-10-11-unreal-m2-run\renders\20_st_s27c_finished_low_oblique.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-unreal-m1-playtest\INDEX.md (milestone 1 pictures and the look faults noted then)
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\unreal-photorealism.md
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\unreal-architecture.md
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\research\free-assets-roads-world.md (being written now by an asset agent, with downloads under unreal\CityPainter\SourceArt\Free; use it when it appears; until then use the Poly Haven textures already in SourceArt and engine content)
A pair is valid only if photo and render have the same height, angle and kind of landscape.
Start: worktree C:\Users\bgbg0\Documents\Programming\roadworld-wt-uelook, branch agent/uelook, cut from agent/unreal-m2 (milestone 2, played). Another Unreal builder (milestone 3 UI) works in the game module's HUD and tool files: you own the ground and sky only: materials (MI_Ground_Floor and its parent), the ground and skirt render layers (CPGroundSkirtLayer and the terrain tile layer), sky/light/fog/exposure/post-process setup of L_Sandbox, and the world cover data they read (CoverGround fields field_id, use, tint, row_dir). Do not change tool, HUD, playtest-script or sim_core junction/grading/road tool code.
UNREAL LANE (one Unreal build, editor or -game process at a time on this machine, several agents share it): before any UBT build, editor or game launch, take the lock by creating the directory C:\Users\bgbg0\Documents\Programming\roadworld\.claude\state\unreal_lane.lock (mkdir fails if it exists: then wait and retry every 60 s; also wait while any UnrealEditor, UnrealBuildTool or dotnet UBT process that you did not start is running), write a file owner.txt inside with your branch name and the time, and remove the directory as soon as your build or run ends, every time, including on failure. Never hold it while you are only editing or thinking. If it is older than 60 minutes and no Unreal process runs, say so in STATUS.md, then take it. Build and run commands: unreal\tools\README.md.

## Faults
Seen in the milestone 1 and 2 pictures (the photos decide, not this list):
1. The ground is dark, dull olive with a visible repeating tile grid and blurry dark blotches; from the builder's view it reads as a carpet. Needs: correct brightness against the photos, macro variation, distance blending without visible repeat, per-field land use (pasture, arable, crop rows by row_dir, stubble) from the CoverGround fields.
2. Heavy cloud-shadow blotches from high up; dark blue band at the horizon; hills barely read from the ground; exposure fixed at EV100 14 makes everything flat.
3. No hedges or trees yet (asset agents are finding them): leave clean hooks, do not build vegetation.
4. The ground skirt meets the terrain with a visible change.
Check the frame time before and after at the three cameras and report it. Research every Unreal setting from C:\UE_5.8 source or docs, never from memory.

## Launched
This and the milestone 3 UI builder (both need the Unreal lane: share it by the lock). Also running: roundabout, T-junction and slip road rounds, grading world layer, road tool follow-up, earthworks round 9, integration, audit gate tooling, matched reference set, two free asset searches, milestone 2 auditor.

## Waiting
Hedges, trees and verge scatter in the sandbox: wait for the free asset results. Earthworks under drawn roads in Unreal: waits for the grading world layer.
