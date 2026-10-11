# Slip road round: diverge nose and merge taper markings to a close match with matched UK photos

## Owner's words
11 Oct: "All the time you should be using reference images, this is at the end of the day what I want it to look like"
11 Oct, on junction comparisons made against photos of the wrong junction type, scale and viewpoint: "it isn't even getting relevant ref images!!!"
11 Oct: "there's so much waiting"

## Cases
Case C: UK dual carriageway or motorway slip road. Views: (1) top-down of a diverge nose with its hatched area and boundary lines, (2) top-down of a merge taper, (3) driver's-eye of each.
Files to open, all of them (M = 8, plus every row for case C in MATCHED.md):
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\MATCHED.md (photos matched to exactly these views with measurements and render cameras; being finished by a reference agent now: if case C rows are missing when you need them, fetch matching photos yourself; never pair a near miss)
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\33_dual-slip-road-ladder-chevron.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\aerial\35_a160_a180_habrough_junction_drone_uk.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\aerial\62_oxon_ortho_marcham_a34_ring_slips_12cm.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\aerial\05_m1_j10_slip_end_interchange.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\INDEX.md (pick and open every slip, merge and gore photo it lists; name them)
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\road-marking-rules.md
- C:\Users\bgbg0\Documents\Programming\roadworld-wt-junc\docs\progress\2026-10-11-junction-markings\AUDIT.md (independent audit of the previous round: slip gore items 12-13 and process item 14, each with the test to add)
A pair is valid only if photo and render show the same junction type, size class and viewpoint; set each render camera to match its photo.
Start: worktree C:\Users\bgbg0\Documents\Programming\roadworld-wt-sliproad, branch agent/sliproad, cut from integ/2026-10-11. Two other agents are in the same markings source file right now (roundabout round, T-junction round): edit ONLY the functions that produce slip road, gore, merge and diverge geometry and markings, their tests and region data keys; do not reformat, reorder or rename anything else in shared files, so the three branches merge cleanly. Do not touch roundabout, T-junction mouth, grading, road tool or world cache code.

## Faults
Starting list (the photos decide, not this list):
1. The gore is open V chevrons with no boundary lines and a blunt end: the photos show a solid line each side converging to a pointed nose, with the bars or chevrons between them cut cleanly at the lines. The marking style follows the matched UK photos for the road class (region data toggle between chevron and diagonal bars); the main session decided this, an earlier note that the owner chose open chevrons does not override the photos.
2. Grass notches are cut into the carriageway at the nose of each gore: paved surface must be continuous to the physical nose.
3. No separate slip lane is visible past the gore; no dashed line across the slip mouth; slip road edge lines and the lane line missing or ragged; holes in the hatching at the nose.
4. The low camera (view G) showed only speckle at the horizon: every view must frame the junction as its photo does.
5. Merge end taper: length from speed (region data), edge line continuous through the taper.
Every "meets / continuous / bounded" property becomes a test. Full suite on gcc and MSVC, clean builds, before hand-in. Renders with the prototypes/road-draw tools as the previous round did (regen.sh and views.json next to the audit file).

## Launched
This, the T-junction round and the roundabout round (three worktrees, disjoint functions). Also running: grading world layer, road tool follow-up, earthworks round 9, integration, audit gate tooling, matched reference set, two free asset searches, milestone 2 auditor, two Unreal builders.

## Waiting
Unreal wiring of gore_length and junction overrides: after these three rounds merge.
