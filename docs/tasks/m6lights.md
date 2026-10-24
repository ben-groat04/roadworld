# M6 lights, round 1: a residential street whose road-generated lighting columns match the photos, UK light values checked against BS 5489-1 and the photos

## Owner's words
24 Oct 2026, on vegetation: "How was vegetation getting on btw this is what will make it look good"
11 Oct: "remember (should be in docs already) that the photorealism must come from adding the road builder into this landscape, nothing else"
11 Oct: "it isn't even getting relevant ref images!!!"
11 Oct: agents "should anticipate what the cold read viewer might say and just get round to fixing that also before it actually tries to get its work audited"
(Game rule from CLAUDE.md: a road placed in the open world must produce everything in the picture via a `world_cover` layer and road-generated content, including lighting columns; no hand-dressed look-test levels.)

## Scope
M6 row of docs/unreal-game-plan.md: "Lights, world cover, verge scatter; a road clears vegetation". This round (m6lights1) is ONE case (contract item 22): case 1, the residential street (30 mph, footways both sides, 6 m columns at the back of the footway). Cases 2-4 are later rounds.
sim_core already generates columns (`sim_core/include/cp/street_lights.h`, layer `street_lights`); Unreal draws none (render layers today: ground, ground skirt, road surface, markings). The UK light values in `data/regions/uk/road_types.json` call themselves "recalled, not checked". The round:
- Phase 1 (measure, data, sim_core, tests; gcc; NO Unreal, no Unreal lane): open every reference; write MEASURE.md with photo measurements for case 1 in lane widths (height against eaves, spacing by counting houses and columns per measured span, set-back from kerb, arrangement single-side vs staggered, bracket direction); check every residential street_lights value against the source table below and the photos; fix region data and geometry rules where they disagree; make set-back a region-data function of the speed limit; make lit/unlit default from the speed limit; add tests. Then write CHECKPOINT.md and stop (the `--phase` tool is not merged yet, so the checkpoint is by hand).
- Phase 2 (Unreal lane; starts in a fresh agent from phase 1's STATUS): Unreal `FCPInstanceLayer` (ISM pool keyed by tile and module id; the sim emits instance lists in sim metres and module ids; the mesh table is a content registry in Render, not in sim), designed so trees and hedge shrubs (round pvetrees) use it next; spike first: add and remove 50k instances per tile swap, timed, before the layer is final; two free or scripted column meshes (steel tapered with bracket, post-top; `tools/blender_kit` allowed); renders at the photo framings with the player-placed road in the open world; composites; self-audit; full hand-in.
Region-agnostic core: every number lives in `data/regions/uk/` (set-back by speed limit, heights, outreach caps, arrangement by road type).
Unreal lane: one editor or plugin build at a time; phase 1 runs in a sim_core worktree meanwhile.

## Cases
Case 1, residential street (single-side and staggered, back of footway); finish line "matches the photo" at each photo's framing, item by item (column count per measured span, height in lane widths, set-back, arrangement, bracket direction):
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\101_lights-residential-kingston-road-oxford-1760579.jpg  (rear-of-footway column before a boundary wall, staggered, bollards in a build-out; measure height vs eaves, spacing, set-back)
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\102_lights-residential-overton-avenue-4478491.jpg  (column on the grass strip at a side-road corner; distance from kerb, position at the mouth, height vs house)
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\103_lights-residential-northfield-road-quarrington-row.jpg  (long single-sided row, about 10 columns; best for spacing)
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\104_lights-residential-stray-park-road-camborne-4792200.jpg  (terraced street, column at footway back with a long bracket over the road, alternating sides further away)
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\113_lights-junction-knowsley-tarbock-island-1.jpg  (junction corner columns, as a secondary view; photos 102 and 113 show corner columns)
Photos are CC BY-SA from Wikimedia Commons; sources are in docs\reference\INDEX.md. Scale is not calibrated: measure in lane widths (residential lane about 3.0-3.5 m).
Also open, all of them (M is counted in STATUS.md):
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\research\m6.research.md (the research behind this file; sections 2-5)
- C:\Users\bgbg0\Documents\Programming\roadworld\sim_core\include\cp\street_lights.h and C:\Users\bgbg0\Documents\Programming\roadworld\sim_core\include\cp\region_pack.h (StreetLightRules)
- C:\Users\bgbg0\Documents\Programming\roadworld\data\regions\uk\road_types.json (every street_lights block)
- C:\Users\bgbg0\Documents\Programming\roadworld\sim_core\include\cp\vegetation.h (street trees read the light phase for clearance)
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\unreal-architecture.md (ICPRenderLayer; phase 2 reads it, phase 1 opens it too)
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\INDEX.md and C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\INDEX.md
A pair (render | photo) is valid only if both show the same kind of road at the same distance and angle. A near miss is never paired; say so and fetch a matching photo (pre-approved).

## Written sources (paraphrased; builders need not redo this research)
Key: [BS] BS 5489-1:2020 Design of road lighting Part 1 (licensed copy hosted by Lincolnshire CC, not for resale; paraphrase only, no long quotes in code comments): https://www.lincolnshire.gov.uk/downloads/file/10050/british-standards-bs-5489-1-2020-design-of-road-lighting . [SHR] Shropshire Council Street Lighting Design Guide 2020 v16: https://next.shropshire.gov.uk/media/ggalx1qi/shropshire-lighting-design-guide-2020-v16.pdf . [BRK] Bracknell Forest S38 lighting developer specification, section 4.9: https://www.bracknell-forest.gov.uk/sites/default/files/2024-09/developer-specification-S38-lighting.pdf . [THU] Thurrock Council street lighting policy 2019, sections 6.6-6.8, 6.13, 6.19: https://www.thurrock.gov.uk/sites/default/files/assets/documents/policy-streetlighting-201907-v03.pdf . [MK] Milton Keynes CMK Handbook L1/L2: https://www.milton-keynes.gov.uk/sites/default/files/2022-04/Part_B_-_L1_Lighting.pdf . TD 501 and CD 109 (DMRB) contain no set-back, spacing or height numbers: do not cite them for geometry.

| Item | Value | Source | Repo consequence |
|---|---|---|---|
| Column height, residential and subsidiary | 5 and 6 m typical; not above neighbouring eaves (about 6 m for a two-storey house); P5 = 6 m max | [BS] 6.2.1 Note 2; [SHR] Table 2.1; [MK] L2 5.5 m | residential 6 m: keep |
| Column height, traffic routes | 8, 10, 12 m; M2/M3 12 m max | [BS] 6.2.1; [SHR] Table 2.1 | later rounds |
| Set-back (column face to carriageway edge) by speed limit | 30 mph 0.8 m; 40-50 mph 1.0 m; 60-70 mph 1.5 m | [BS] 6.1.3 Table 1; [BRK]; [SHR] Table 2.2 | set-back is a region-data function of speed limit; column centre adds half the shaft base diameter (0.1-0.19 m) |
| Position with footways | at the rear of the footway wherever possible; in residential roads with footways <= 3 m beside the carriageway, at the rear of the footway; a verge between road and footway may host them if Table 1 clearance holds | [BS] 6.1.3 and Note 3; [THU] 6.8 | `footway_inset` 0.3 m and back-of-footway rule match |
| Footway passable | set-back enough for free passage on the footway | [BS] 6.1.3 | corner rule |
| Dropped kerbs, frontages | no column within a dropped-kerb span or shared surface; on estates not outside building frontages (prefer the party line between two plots) | [THU] 6.6, 6.7 | check against 101-104 |
| Vertical clearance | any bracket or luminaire overhanging the carriageway: at least 5.7 m above it (2.1 m over pedestrian-only area) | [BS] 6.1.3 | a 5-6 m column needs post-top or a short bracket; mesh must respect it |
| Outreach by height | 5-6 m post top, 0.7 m max; 8 m 1.0 m max; 10-12 m 1.5 m max | [BRK] 4.9; [THU] bracket options 0.3-2 m | residential 0.6 m ok |
| Arrangements | opposite: wide roads; staggered: traffic routes, residential and subsidiary; single-sided: narrow roads, curved link roads | [BS] 6.2.3 | choose single vs staggered per road width from photos 101-104 |
| Spacing | NOT a written number anywhere; photometric result (BS EN 13201-3); local specs say "to suit design"; an Irish residential report gives about 30 m +/- 5 m for 6 m columns (snippet, Irish, not UK) | [MK] L2; [THU] 6.19; [BS] 6.2.1 Notes 3-4 | spacing stays photo-derived: count columns per measured span in 101-104, write the value with photo file names in the region-data comment; record spacing/height ratio |
| Trees | columns where tree cutting is not needed; minimum 2 m (L2), 3 m (L1) from trees | [BS] 6.1.2; [MK] | check the street-tree clearance rule in vegetation.cpp |
| Later cases | T-junction: one column opposite the T mouth plus one a short distance into the side road ([BS] 7.2); roundabouts: ring from outside, island column at island radius >= 10 m, the 3.8-5 m centre-island beacon is not a column ([BS] 7.5; [THU] appendix); dual carriageway with centre reserve wider than 9 m is lit as two carriageways ([BS] 6.2.3.3) | as left | not in round 1 |

## Decisions by the main session (contract item 19)
1. Set-back is a region-data function of the speed limit (30 mph 0.8 m, 40-50 mph 1.0 m, 60-70 mph 1.5 m, column face to carriageway edge, [BS] 6.1.3 Table 1; the column centre adds half the base diameter); when a footway exists the column goes at the back of it (`footway_inset` 0.3 m).
2. Heights by class from [BS] 6.2.1 Note 2: residential 6 m (capped to neighbouring two-storey eaves, about 6 m); main street and B road 8-10 m; A road 10 m; dual 12 m (15 m allowed).
3. Outreach caps by height from [BRK] 4.9: 6 m post top 0.7 m, 8 m 1.0 m, 10-12 m 1.5 m. `main_street` 1.2 m becomes 1.0 m. Anything overhanging the carriageway clears 5.7 m ([BS] 6.1.3).
4. Dual carriageway default: columns on the outer verges (photos 31, 110, 111); `central_reservation` twin columns only as a toggle; 9 m median switch in region data. Not in round 1.
5. Spacing is photo-derived; the region-data comment names the photo files; the words "estimate" and "recalled" leave `road_types.json` for every value this round touches.
6. T-junction rule (column opposite the T mouth plus one into the side road, [BS] 7.2) is the default, to be confirmed or rejected from photos 105 and 106 in a later measure step. Not in round 1 unless case 1's photos show a junction.
7. Roundabouts: ring from outside; island column when island radius >= 10 m; the beacon is not a column. Not in round 1.
8. Lit or unlit (road detail, decided): default lit = the road's speed limit is 30 mph or less (built-up); rural types unlit by default; a per-road toggle `lit` overrides it. A `world_cover` built-up area may set it later.
9. One `FCPInstanceLayer` for lights now, designed so trees and hedge shrubs (round pvetrees) use it next; the sim emits instance lists (sim metres, module ids); the mesh table is a content registry in Render. Spike (50k add and remove per tile swap, timed) before the layer is final.
10. Free or scripted column meshes only (`tools/blender_kit`: a scripted tapered steel column with bracket and a post-top); flag any cost. Keep the style ids (`residential_column_6m`, `column_8m`, `column_10m`, `column_12m_twin`) as module ids.
11. The `world_cover` corridor mask (`ground_height`, `grading_footprint`) is DEFERRED until round m2fix5c merges (it edits world_cover); see Waiting.
12. Tests: instance-count golden test per tile on a golden road; driving-side mirror test (flip the side, columns mirror exactly); every expected value names its photo measurement or BS clause; no waivers.
13. Fix the photo 31 caption in `docs\reference\street\INDEX.md` (columns are in the outer verges, not the median); that file is private and gitignored, so it is a local edit only, made in the main checkout path.
14. Two phases with a CHECKPOINT between (see Scope). Phase 1 ends with `CHECKPOINT.md` and the checks in Gates; the Unreal phase starts in a fresh agent.

## Gates
ctest: failed_assertions unchanged from the baseline you measure first on current main (suite expectation: exactly the known `should_fail` pier assertions; write the number in STATUS.md)
scene_check >= 20/21 (baseline 20/21)
No new test waiver, skip, may_fail or should_fail. Every test's expected value names a photo measurement or BS clause.
Instance-count golden test and driving-side mirror test exist and pass.
One clean MSVC build at the end of each phase that changes C++ (gcc while iterating); phase 2: one clean plugin build, the Unreal lane lock held only during builds and runs.

## Faults
Note on gates: scene_check is `node prototypes/road-draw/tools/scene.mjs --check`; the vegetation incremental==full test must stay exact and any new test must be fast (that test already takes 70 s).
(Empty: builders fill from the photos; the main session adds its own after its first look.) Known going in: street/31 caption wrong (decision 13); `main_street` outreach 1.2 m over the 1.0 m cap on an 8 m column; set-back is a per-type constant, not a function of speed limit; lit flag is per road type, not per speed limit.

## Finish line
Case 1's render (the player places the road in the open world, nothing hand-dressed, same framing as each photo) matches photos 101-104 item by item: column count per measured span, height in lane widths against the houses, set-back, arrangement (single-side or staggered), bracket direction; every residential light number in `data/regions/uk/` carries a photo file or BS clause in its comment; Unreal draws the columns through the instance layer with no hitch on a tile swap (spike number recorded). Stop rule: the self-audit residual holds only unfixable items, or 3 passes; no hour limit. Shape (outline overlay, measurements) and look (judged in Unreal) are accepted apart.

## Launched
- m6lights1 phase 1 (measure, data, sim_core, tests; no Unreal lane).

## Waiting
- Phase 2 (Unreal instance layer, meshes, renders, composites): Unreal lane, after phase 1's checkpoint.
- Cases 2-4: one case per round, after case 1.
- The `world_cover` corridor mask (`ground_height`, `grading_footprint`; delete-road restores cover exactly): after round m2fix5c merges (it edits world_cover).

## Later rounds
Kept from the research draft; each is its own round.
Case 2, village road with lighting (B-class context; no photo of a lit rural B road was found, say so):
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\105_lights-village-a1094-saxmundham-road-4327260.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\106_lights-village-a1094-t-junction-4340273.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\107_lights-b-road-beechfield-avenue-torquay-5354218.jpg
Case 3, A road single carriageway:
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\108_lights-a-road-a249-sittingbourne-road-3557499.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\109_lights-a-road-tonbridge-road-maidstone-2442878.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\32_dual-street-lights-verge-kerbed-channel.jpg
Case 4, dual carriageway (verge columns; one real twin-central photo is still missing):
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\110_lights-dual-a164-willerby-bypass-6463042.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\111_lights-dual-willerby-bypass-6126755.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\31_dual-central-reservation-grass-barrier.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\112_lights-dual-slip-new-ferry-bypass.jpg
Junction and roundabout column positions (cases 2 and 4):
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\113_lights-junction-knowsley-tarbock-island-1.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\28_junction-roundabout-splitter-island-elevated.jpg
