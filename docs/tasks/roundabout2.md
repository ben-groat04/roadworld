# Roundabout round 2 (M7): a single-lane 4-arm roundabout that matches the Bloxham photos, arms and islands first

## Owner's words
11 Oct: "it isn't even getting relevant ref images!!!"
11 Oct: agents "should anticipate what the cold read viewer might say and just get round to fixing that also before it actually tries to get its work audited"
11 Oct: "think about why round 9 earthworks failed, improve the system"
11 Oct: "remember (should be in docs already) that the photorealism must come from adding the road builder into this landscape, nothing else"

## Cases
Three cases only. Each is finished when the top-down render, at the photo's scale and orientation, matches the photo item by item, measured in lane widths.
Case 1, whole roundabout top-down (single-lane ring, 4 arms, outer diameter 35-45 m):
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\aerial\64_oxon_ortho_bloxham_a361_roundabout_whole_12cm.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\aerial\66_oxon_ortho_bloxham_north_roundabout_whole_12cm.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\aerial\65_oxon_ortho_winterbrook_wallingford_roundabout_whole_12cm.png
Case 2, one arm close (island, give-way line, entry and exit kerb lines, 50 m frame):
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\aerial\68_oxon_ortho_bloxham_a361_arm_N_50m.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\aerial\69_oxon_ortho_bloxham_a361_arm_W_50m.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\aerial\72_oxon_ortho_bloxham_north_arm_NE_50m.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\aerial\73_oxon_ortho_bloxham_north_arm_W_50m.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\aerial\70_oxon_ortho_winterbrook_arm_SW_50m.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\aerial\71_oxon_ortho_winterbrook_arm_SE_50m.png
Case 3, driver's eye on the approach, unsignalised arm (the earlier round paired signalised arms by mistake): find at least 3 matching photos in `docs\reference\street\INDEX.md` and `docs\reference\MATCHED.md`; street 28 is the only one known; fetch more if fewer than 3 match (pre-approved), never pair a signalised arm.
Also open, all of them:
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\MATCHED.md
- C:\Users\bgbg0\Documents\Programming\roadworld-wt-roundabout\docs\progress\roundabout\AUDIT.md (the audit of round 1, verdict send back: its numbered "For the next builder" list is part of this brief)
- C:\Users\bgbg0\Documents\Programming\roadworld-wt-roundabout\docs\progress\roundabout\STATUS.md and FAULTS.md (what round 1 did and left)
- C:\Users\bgbg0\Documents\Programming\roadworld-wt-roundabout\docs\progress\roundabout\after\R1_whole_topdown.png (round 1's headline render: your "before")
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\road-marking-rules.md
A pair is valid only if photo and render show the same junction type, size class and viewpoint.

Start: worktree C:\Users\bgbg0\Documents\Programming\roadworld-wt\roundabout2, branch agent/roundabout2, cut from current main; then merge branch `agent/roundabout` (tip f48282a, round 1's work plus its audit) into it and carry on from there. You own `sim_core/src/junction_roundabout.cpp` and its header, the roundabout block of `data/regions/uk/region.json`, the roundabout tests, the roundabout mesh part of `render_mesh.cpp`, and the roundabout fixture and views. Shared with the T-junction branch (being audited now) and the slip road job: `junction_geometry.cpp`, `markings.cpp`, `region_pack.*`: change them only where a roundabout fault needs it, keep the edit small and list every such edit. No Unreal. Do not push or merge. Suite runs: one compiler at a time.

## Gates
scene_check >= 20/21 (baseline 20/21)
ctest failed_assertions == 2
No new test waiver, skip or should_fail. Tests that pin a fault (see Faults 2, 4, 5) are to be corrected, and each corrected test must be shown to fail on round 1's geometry.

## Faults
From the audit and from the main session's own look at R1 against aerial 64 (the photos decide, not this list):
1. Arms: every approach flares to about three lane widths over a taper of 50-60 m before the island. In the photos the arm stays close to its normal width and widens only in the last 10-15 m; kerb lines curve into the ring with a short radius. Measure entry width, flare length and entry and exit kerb radii on 64, 66, 68, 69 and make them region data per size class. The changed corner radii in round 1 (class 2 entry 10 to 8 m, exit 16 to 12 m) had no photo behind them: re-derive.
2. Splitter island: round 1's is a needle 4.8 m wide by 14 m long with an exponent profile (`h_edge = h0 - flank`). Photos: a blunt triangle or stub about 2 to 2.6 lane widths wide at the ring (about 7 m), 10-14 m long, rounded tip, PAVED and pale with a kerb, widest at the ring. The island-size test locks in 1.1-1.55 lane widths: correct it from the photos.
3. Hatching: besides the nose taper, round 1 draws a ladder of 5-7 bars beside the island on every arm; the photos show none. The test requires `beside >= 4`, so it enforces the fault. Hatching only in the nose taper, bounded by its edge lines, and a test that fails if any bar lies beside the island.
4. Give-way line: must run across the whole entry on the ring's outer edge, dash and gap from `docs/road-marking-rules.md` and the photos. Round 1's is a short stub. The existing give-way test is sound: keep it.
5. Ring: the photos show a single-lane ring with NO lane dashes; round 1 draws a dashed circle that makes it read as two lanes. Lane lines only when the ring has two lanes; dash style then from photos 66/72 (about 1 m mark / 3 m gap, to be measured), with a test that tells 1/3 from 2/4. The outer edge line must curve into the ring at each mouth, with a test that can fail.
6. Surfaces: paved islands vanished in the flat-shaded render, so round 1 switched to grass. Fix the renderer view (kerb face, pale paving colour), do not change the design to suit the picture. Central island: grass with a kerb; the wide grey apron in R1 has no photo behind it at this size: check the photos and remove or justify it.
7. Small ring (R 8 m): the island is longer than the ring is wide. 3-arm ring: black speckles on the inner kerb. Both from the audit's variants.
8. Long strokes sink below the road: 40 gore-hatch triangles 5 mm to 8 cm under, two 50 m slip-taper strokes 39 cm under (missing vertex-density rule on long strokes, older than this branch). Apply the 0.5 m strip rule to every long stroke type and add a test that samples every marking triangle against the surface.
Not in this round: arrows, zig-zag, signalised arms, two-lane rings beyond keeping them working, the interchange fixture's raised ring (needs earthworks).

## Launched
This job. Also running: measuring hand-in gates (tools), built-in trees evaluation (Unreal lane), earthworks method review, road tool round 3 merger, auditors for the grading layers and the T-junction round.

## Waiting
Slip road round: waits for this branch and the T-junction verdict (same junction files). T-junction send-back or merge: waits for its audit. Earthworks round 10: waits for the method review and the gradelayer verdict. Milestone 3 UI: waits for the road tool merge and the Unreal lane.
