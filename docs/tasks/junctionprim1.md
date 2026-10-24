# Junction primitives round 1: rebuild junction geometry from straights and tangent arcs (roundabout first)

## Owner's words
24 Oct 2026: "i don't know how geometry of roads here is calculated but i feel like youre overcomplicating theres all sorts of odd bends etc"
24 Oct 2026: "its amazing you spend so many rounds of my time and money and then this is the solution"
24 Oct 2026: "always think if it can be made simpler"
11 Oct: "remember (should be in docs already) that the photorealism must come from adding the road builder into this landscape, nothing else"

## Why
A read-only review (24 Oct) found that every kerb, lane edge, island and marking at a junction is its own lateral offset of a sampled polyline, with its own formula (smoothstep or cubic flares, Newton fillet with grid restarts and bisection, power-law islands, mean-of-two-curves lane lines, 3-point fillet circles for edge lines, per-arm override tables). They disagree by tens of centimetres and the joins show as kinks, hooks, S-bends, Y-forks and bulges. The offset of a straight is a straight and the offset of an arc is a concentric arc, so geometry built from those primitives cannot kink against itself.

## Decisions (made here, contract item 19; builders implement them or write why with a photo measurement)
1. Model. A junction approach is a `KerbPath`: straight, circular arc, straight (a clothoid only where the road centreline already has one). Kerbs, lane edges, splitter island edges, edge lines, lane lines, give-way lines and hatching are exact offsets of `KerbPath`s or of the ring circle. No sampled-polyline lateral formulas, no smoothing passes, no per-arm override tables.
2. Corner arc. One closed-form `tangent_arc()`: an arc of the class radius tangent to two straight kerb lines, or to a straight and a circle (ring), or two circles. It replaces `generate_geometry`'s corner part, `solve_fillet` and `newton_fillet` (junction_geometry.cpp). If the class radius does not fit, the edit is refused with a reason; the radius is never silently shrunk.
3. Parameters. Per roundabout size class in `data/regions/uk/region.json`, about 8 numbers, each with its source (CD 116 clause or a photo measurement in lane widths): circulatory width, entry kerb radius, exit kerb radius, flare length, entry widening (e - v), splitter island length, island base width, island nose radius. Entry angle comes from the road the player drew. Delete the fields and tables these replace (`ArmFlare` kerb_* fields, `ArmShape` per-arm values, `SplitterIslandOverride`, `KerbProfileOverride`, `corner_fit`, the 1.8-lane and 0.65/0.85 marking constants).
4. Do not overfit to the photos (owner 24 Oct: "remember don't overfit to the photos. you know this already"). Class numbers come from the design rule first (CD 116, region data with source); a photo only picks a point inside the range the rule allows. Photo traces stay test data and are never geometry. What the class numbers cannot explain (a site's bellmouth, access, strip to a neighbouring junction) is reported per case in COMPARE.md as a site difference, never given a parameter, per-arm table or special case. A parameter that exists only to bring one photo closer is deleted.
5. Base. Build on current main, NOT on agent/roundabout5 (its 3600 added lines are the layers being removed). Take from agent/roundabout5 only: the photo traces (`data/regions/uk/roundabout_traces/`), the trace comparison test harness, and the fixture cameras (`views_final.json`); read them with `git -C <worktree> show agent/roundabout5:<path>`.
6. Fixture. The ordinary roundabout fixture: four player-drawn roads meeting a ring at the photo's arm angles, nothing else. No fixture code that draws arms along traced centrelines.
7. Shape only. This round is accepted on shape (outline overlay against aerial 64, 68, 69, measured); the look (surface tones, signs, lamp columns, eye-level views) is a later round. No Unreal.
8. Size. The round must REMOVE more junction code than it adds; STATUS.md reports lines before and after for junction_geometry.cpp, junction.cpp, junction_roundabout.cpp and the junction part of markings.cpp. T-junction corners go onto the same `tangent_arc()` where main already calls `solve_fillet`; T-junction photo cases are the next round.

## Cases
One case: the Bloxham A361 roundabout (single-lane ring, four arms). Finished when the top-down render at the photo's scale and orientation matches the photo item by item: kerb outline within 0.5 m at every 1 m station, splitter islands within 0.5 m, no kink (heading change over any 1 m of kerb or line more than the arc it lies on allows), no marking fragment shorter than one dash.
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\aerial\64_oxon_ortho_bloxham_a361_roundabout_whole_12cm.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\aerial\68_oxon_ortho_bloxham_a361_arm_N_50m.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\aerial\69_oxon_ortho_bloxham_a361_arm_W_50m.png
Also open, all of them:
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\road-marking-rules.md
- C:\Users\bgbg0\Documents\Programming\roadworld-wt\roundabout5\docs\progress\roundabout5\AUDIT.md (faults of the last roundabout render; each kink, hook, fork and notch it names must be gone, by construction)
- C:\Users\bgbg0\Documents\Programming\roadworld-wt\roundabout5\docs\progress\roundabout5\after\C1_whole.png (the last render: your "before")
CD 116 (roundabout geometry, read the clauses you cite): https://standardsforhighways.co.uk/tses/attachments/html/7b5ea157-9b3e-4774-9781-7d1656e83338

## Tests that must exist (each fails on the current main geometry or on agent/roundabout5's, shown in fail_first files)
- Kerb, lane edge and edge line at every junction are offsets of one KerbPath: sampled distance between kerb and its edge line is constant within 1 mm along the whole path.
- No kink: heading change per metre along every kerb and line equals the primitive's curvature within 1e-6 rad/m except at tangent joins, where it is continuous.
- Outline against the aerial 64 trace within 0.5 m at every 1 m station, with class parameters only.
- No marking other than give-way dashes crosses an entry lane between the give-way line and the island tip.
- Refusal: a class radius that does not fit refuses the edit with a reason.

## Gates
ctest failed_assertions <= 2
No new test waiver, skip or should_fail. Old roundabout and junction tests that test removed fields are rewritten against the new model with the photo or rule source named, never deleted to pass.

## Faults
The faults of C1_whole.png and AUDIT.md above (curved lane-line fragments, hook by the give-way arc, S-kinked mouth edge line, Y-fork at the island nose, bulging arm kerbs, jagged island notch, wobbling arms); the geometry review's mechanism for each is in "Why".

## Launched
This round only (phase 1). Running alongside: earthworks grade-r14, street lights m6lights1, trees pvetrees2, M3 UI m3ui-r3b, M2 fixes m2fix5c, suite speed 2. T-junction round 8 stopped at a checkpoint: its photo measurements become test data for junctionprim2.

## Waiting
T-junction cases on the new model (junctionprim2): wait for this round's tangent_arc and KerbPath. Roundabout look (surfaces, signs, lamp columns, eye views): waits for this round's shape acceptance. Class 2 ring (aerial 65, 66): after round 1 on the same model.

## Phases
Phase 1 (fresh agent): KerbPath, tangent_arc, roundabout approach from class parameters, splitter island, fixture; tests; gcc; HANDOFF.md and CHECKPOINT.md with a top-down image. Phase 2 (fresh agent): markings as offsets, class fit against the traces, renders at the photo framings, outline overlays, composites, self-audit, MSVC once, full hand-in.
