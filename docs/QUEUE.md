# Work queue (the main session's only queue; if it is not here, it is not queued)

Updated by the main session every time an agent is started, an agent reports, or the owner changes direction. Newest state wins; finished items are deleted (git history keeps them). Last updated: 11 Oct 2026, evening.

## Running now
| Item | Branch / worktree | Started from | What "done" is |
|---|---|---|---|
| Earthworks round 9: disturbance view and cross-sections first, then a hard bound on changed ground on every scene, plus round 8 leftovers | `agent/grade-r9`, `..\roadworld-wt-grade-r9` | `agent/grading-r6` 2aff581 + main | `tools/check_handin.mjs` passes on `docs/progress/2026-10-11-structures-r9`; main session opens the disturbance views and sections |
| Integration: merge, in order, world cover r4 (`agent/world-cover-r2` 0da3c74), junction markings (`agent/junction-markings` d611346), earthworks r7+r8 (`agent/grading-r6` 2aff581), road tool gaps (`agent/road-tool-2` d7dfc99), then main | `integ/2026-10-11`, `..\roadworld-wt-integ` | main | clean builds, full suite on gcc and MSVC after each merge; then fast-forward main |
| Unreal milestone 2: merge main into it, build, automation tests, M1 playtest still 37/37, M2 scenarios, pictures of every drawing state beside the 26 prototype screenshots, fix what belongs to M2 (previous agent built it but never ran it; tip 905df6b) | `agent/unreal-m2`, `../roadworld-wt-m2` (holds the Unreal lane) | 905df6b + main | hand-in in `docs/progress/2026-10-11-unreal-m2-run`; main session opens the composites; then fast-forward main |
| Gate notices: BUILT (agent/rules-fix 24f60aa, 110 hook tests by its count, hand-in check passed). Not merged: waits for its auditor | `agent/rules-fix`, `../roadworld-wt-rules` | main | auditor AUDIT.md says accept; then main session merges, reads the 3 existing log items, acks |
| AUDITOR: gate notices and hooks (own tests, full docs page, background-agent case the builder did not test) | writes AUDIT.md in `../roadworld-wt-rules/docs/progress/2026-10-11-gate-notices` | - | AUDIT.md with verdict |
| Audit gate mechanism: auditor briefs (`make_brief --audit`), finish gate refuses builder = auditor, an audit-owed notice on every prompt until AUDIT.md exists, `check_handin --accept` | `agent/auditgate`, `../roadworld-wt-auditgate` | agent/rules-fix 24f60aa | hand-in passes; then its own auditor |
| Vegetation kit round 2: HANDED IN (agent/veg-kit-r2 80bd4d4, check passed, 24 of 30 references opened, long residual list). AUDITOR running; not accepted until AUDIT.md | `agent/veg-kit-r2`, `..oadworld-wt-veg` | main | AUDIT.md verdict; then main session opens the matched composites |

| Matched junction reference set (owner, 11 Oct: "it isn't even getting relevant ref images!!!"): at least 3 UK photos per case AND view that match our junction type, size and a reproducible camera; `docs/reference/MATCHED.md` with measurements and the render camera per photo; existing photos classified usable / not usable | research agent, writes only under `docs/reference` | - | MATCHED.md exists; main session opens the photos; BLOCKS every junction builder |

## Reference relevance (blocks visible rounds)
A comparison pair is valid only when photo and render show the same junction type, size class and viewpoint. A case with no matching photo is reported as "no matching photo" and one is fetched; a near miss is never paired. Junction rounds are briefed from `docs/reference/MATCHED.md`, with render cameras set to match each photo. To make mechanical in the audit gate work: COMPARE.md rows carry type / scale / view match fields and the check fails on a row without them. The same relevance check is owed for the earthworks and vegetation references when those rounds are audited.

## Audit step (part of every round, not support)
Owner, 11 Oct: "is one earthworks agent creating AND auditing? that's wrong. and elsewhere, that's just an expample". Builder and auditor are never the same agent (rule text in CLAUDE.md, Main session). No item above or below is accepted, merged to main or reported as done until a fresh auditor has written `AUDIT.md` in its progress folder. An auditor is started the moment a hand-in passes `check_handin.mjs`, ahead of any new build work.
Auditors owed:
- Earthworks round 9, when it hands in: also check the section sheets and disturbance views against the height data they claim to show.
- Unreal milestone 2 run, when it hands in: composites against the 26 prototype screenshots, and whether the playtest script really exercises what it says it checks.
- Integration, when it hands in: an independent clean build and suite run on both compilers of the merged tip (not the merger's own count).
- Gate notices and hooks: auditor RUNNING. Audit gate mechanism: auditor owed when it hands in.
- Junction markings: AUDITED, verdict send back (AUDIT.md be59b50 in ../roadworld-wt-junc/docs/progress/2026-10-11-junction-markings; 14 items with the test each needs). The roundabout, T-junction and slip rounds below are briefed from that file, not from the older lists here.
- Road tool round 2 (`agent/road-tool-2` d7dfc99): the read-only 45-screenshot check counts as its audit (different agent); nothing more owed.
Auditors are not capped and never wait for a slot (owner, 11 Oct: "wdym \"free slot\" just keep adding them as needed"). The mechanism that makes this automatic is being built (Running: audit gate).

## Next, by priority (start when its blocker clears)
Priority rule (owner, Round 50): the photorealism comes from the road builder placed in the Unreal sandbox. Order = what gets a drawn road, with everything it generates, on screen in the game soonest; then what makes that road look right; everything else is support. A new idea goes where it belongs in this order, not at the top because it was said last.

### Critical path
1. **Fast-forward main to the played-and-fixed milestone 2** once its hand-in passes and the main session has opened the composites. Blocked by: the M2 run (Running).
2. **Grading and structures as WorldCache layers with a dirty path** (so a drawn road gets earthworks in the sandbox). Blocked by: earthworks round 9 and integration.
3. **Roundabout round** (fresh agent, new contract): arm shape and markings together to a close photo; island widens to the ring, hatching only in the nose taper, give-way line across the whole entry, islands on all arms, ring markings, no stray give-way on the west arm, explain the staircase structure at the south arm end. Blocked by: integration (needs merged junction code).
4. **T-junction round**, then **slip road round**, same way (give-way line full lane at the main road edge, side-road centre line meets it; chevrons cut cleanly between two solid lines, no holes at the nose, slip edge and lane lines, dashed line across the slip mouth; parking-strip taper). Blocked by: roundabout round (same files).
5. **Unreal: build the unbuilt wrapper** `Wrap_edit_tool.cpp` with the next plugin build; wire `gore_length` region table and `JunctionOverrides::gore_length`; confirm the dark slot on the raised roundabout arm is gone. Unreal lane.
6. **Road-generated content in the Unreal sandbox**: lighting columns instanced (sim_core street-light layer and kit columns exist), verge and road-edge scatter, vegetation kit and approved Poly Haven pieces imported and instanced. Unreal lane, after item 6.
7. **Unreal background visuals** (world is only a backdrop, planning log Round 50): ground material per field land use from the new `CoverGround` fields, kit hedges and trees instanced; fix tile grid on grass, heavy cloud-shadow blotches from high up, dark blue horizon band, hills barely reading from the ground. Unreal lane, after M2 playtest.
8. **Road tool follow-up** (fresh agent, new contract; source: read-only check that opened all 45 prototype screenshots in `docs/progress/2026-10-11-prototype-ui/` against `road_tool.h`, `edit_tool.h`, `stroke.h` on `agent/road-tool-2`). Blocked by: integration.
   Missing or partly covered, most visible first (screenshot in brackets):
   - Heat-map road colours: no curvature/gradient classes or thresholds, no per-vertex value (29, 30). Still open from the spec's GAP-7.
   - Tuning panel: wall start, batter, vertical-curve K multiplier, water level have no field anywhere in sim_core; only snap angle, snap length, min segment exist (23).
   - Grade advisory text prints "8.0%" where the prototype shows "8%" (14-19; `road_tool.cpp` ~265).
   - Bearing and "R at cursor" readouts exist but no test checks them or the compass convention (04, 10, 11, 13-15).
   - Min-span error should leave no ghost and no profile: behaviour present, not tested (12).
   - Fold error and radius advisory shown together with exact texts: not tested (13).
   - Offset guide at 2x and 3x spacing: not tested (08b).
   - Edit HUD line "selected road #N · L m": no direct length field (32).
   - Profile after delete: prototype shows a stale profile, code shows none; decide and pin with a test (36). Default: none (cleaner).
   - Road type name is not in the preview; host looks it up by id (all).
   - Motorway and B-road ghost widths not tested (19, 22). Dual ghost is refitted per carriageway where the prototype offset the centreline; compare on tight curves (18, 19).
   Differences from the pictures not declared by the builder:
   - Edits that cross another road at the same level make no junction (prototype did); only an advisory.
   - Selection is per engine edge, not per prototype "road": a road through a junction selects and profiles in pieces (41, 42). Needs a road-chain selection.
   - `finish_pick_px` 10: a click within 10 px of the last point finishes; the prototype would add a point there (26).
   - New snap kind Node (auto-finishes on junction nodes): no test; host needs a word and ring colour.
   - Hover reports nodes and interior points of any road; the prototype highlights only the road.
   - `world_margin` drag clamp has no test.
   Also: proper fix of `fit_clothoid_chain` returning a huge chain when a stroke doubles back onto an earlier point (`fit_sane_chain` is a stopgap); update `docs/unreal-ui-parity.md` with all of the above.
   Host-side (Unreal) list from the same check, for milestones 2-3: HUD text, order, glyphs and colours; profile panel drawing; tuning panel, help overlay, inspect bar, frame stats; ring sizes and colours per snap kind, dots, ghost, dashed guides, edit lines and handles; input map and 4 px drag threshold; heat-map surface colouring; scene loader and connector overlay; profile colours from depth and wall cap.
9. **Milestone 3 onward** per `docs/unreal-game-plan.md` (prototype UI parity: profile panel, readouts, warnings, edit tool).

### Support (cheap, no file conflicts; start only in a free slot when no critical-path item can start, never ahead of one)
- **Technical references for the main session's final check** (owner, 11 Oct: "i like the retaining wall side profile thing for earthworks ... maybe allows for more technical drawing style references or general real world guidance to be used as a help for YOU (not the agent) on a final review check"). One research agent: fetch typical cross-section and detail drawings (cutting, embankment, retaining wall with backfill and drainage, benched rock cut, split-level carriageways, roundabout entry with splitter island, T-junction, slip road nose) into `docs/reference/technical/` with sources in the INDEX, and write a one-page list of guidance numbers with their source (slope ratios, bench widths, wall heights, verge widths, island and taper lengths, chevron lengths by speed). Researched from published standards and drawings, not from memory. Use as a SENSE CHECK only (owner: "it doesn't have to match the tech drawings perfectly ofc, they aren't all consistent. but it'll be a good sense check"): plausibility and order of magnitude, not a spec, never a hard test. Then: section, profile and plan diagnostics with dimensions become standard hand-in files for earthworks, structures and junction geometry rounds, and the second gate opens them beside the matching drawing. Waits only for a free agent slot (5 running).
- **Refresh `docs/HANDOFF.md`** sections 2, 6, 7, 8, 9, 12 to today (they describe the state before the playtest, rounds 8/4, the rules fix and Round 50). After the integration lands.

## Machine notes
- Enforcement hooks are LIVE in the main checkout since the evening of 11 Oct (`.claude/settings.json`, `tools/hooks/`): a launch without the contract, `HANDIN_DIR` and the owner`s words is refused unless it carries `NO-CONTRACT: <reason>`; the contract is injected into every agent; an agent with a `HANDIN_DIR` cannot finish while `tools/check_handin.mjs` fails (3 tries, then `HANDIN_FAILED.txt`). Log: `.claude/state/gate.log` (grep FAIL-OPEN to see a gate that did not run). Confirmed live in the session: refusal and injection. Agents started before the merge carry no `HANDIN_DIR`, so the finish gate does not apply to them: run the check by hand on their hand-ins.
- The owner stopped the CFD job on the evening of 11 Oct, so more RAM is free. The one-editor-at-a-time rule stays (builds wait on the UBT mutex anyway); heavier parallel compiles are fine.

## Small fixes (batch into the next docs/tools agent)
- `tools/new_agent_worktree.ps1`: usage line must say `powershell -NoProfile -ExecutionPolicy Bypass -File ...` (plain invocation is blocked on this machine); CLAUDE.md gate text likewise.
- Playtest harness: about 4 in 11 runs lose all synthetic mouse input; cause not found. Rerun before believing a regression. Do not run a UBT build in another worktree while a playtest starts (mutex wait, once hung 24 min).
- World cover round 4 composites were written to `C:\Users\bgbg0\cp_scratch\pairs\` (outside the repo); move to `docs/reference/_compare/world-cover-r4/` and remove the outside folder with the owner's OK.
- Old worktrees and branches to remove once merged: `-wt-grade`, `-wt-junc`, `-wt-cover`, `-wt-tool2`, `-wt-rules`, `-wt-tool`, `-wt-world`; leftover `unreal/CityPainter/Plugins/CityPainterSim` folder.
- Pier clearance 0.3 -> 0.2 m as region data (the interchange-bridge pier test stays should_fail until then).
- Carried over from the handoff, not started: street-light incremental limit for close unconnected roads; world-cache road-mesh rebuild cost and re-measuring the 190 ms WorldCache update (milestone 4); `RoadGraph` mutators still public; big files not split (`markings.cpp`, `structure_mesh.cpp`, `render_mesh.cpp`); about 40 structure dimensions not yet in region data (`docs/repo-health-review.md`).
- Reference photos still missing: a close retaining-wall end in a soil cutting on a highway, a machined benched rock cut, UK kerbside parking near a junction, T-junction ghost island, slip gore close-ups. Toe ditch on soil cuts has no photo support.
- Markings drawn in the road material from lane UVs: spike (idea from the Road Creator Pro research).

## Waiting on the owner (not blocking)
- (Hooks: approved by the owner 11 Oct, now under Running.)
- Agents still running on briefs written before the contract: Unreal M2 and vegetation kit (both were sent the new requirements by message). Their hand-ins get the check like any other; if it fails they go back.
- Confirm the five settled contradictions in planning log Round 49 (fresh agent per round; decision split; 3 photos per case; parallel within 3-4 agents and one editor; stop rule replaces time-boxes).
- Deep cuts on steep hills: rock face (current) vs taller walls or steeper soil banks. Main session will pick a default in round 9 unless told otherwise.

## Dropped (owner decision)
- Further world-cover layout rounds, woods and hedge shape rounds, anything man-made in the starting world, a map editor: not now (Round 50).
- Workflow efficiency audit: not needed (owner, 11 Oct).
