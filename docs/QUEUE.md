# Work queue (the main session's only queue; if it is not here, it is not queued)

Updated by the main session every time an agent is started, an agent reports, or the owner changes direction. Newest state wins; finished items are deleted (git history keeps them). Last updated: 11 Oct 2026, evening.

## Running now
| Item | Branch / worktree | Started from | What "done" is |
|---|---|---|---|
| Earthworks round 9: disturbance view and cross-sections first, then a hard bound on changed ground on every scene, plus round 8 leftovers | `agent/grade-r9`, `..\roadworld-wt-grade-r9` | `agent/grading-r6` 2aff581 + main | `tools/check_handin.mjs` passes on `docs/progress/2026-10-11-structures-r9`; main session opens the disturbance views and sections |
| Integration: merge, in order, world cover r4 (`agent/world-cover-r2` 0da3c74), junction markings (`agent/junction-markings` d611346), earthworks r7+r8 (`agent/grading-r6` 2aff581), road tool gaps (`agent/road-tool-2` d7dfc99), then main | `integ/2026-10-11`, `..\roadworld-wt-integ` | main | clean builds, full suite on gcc and MSVC after each merge; then fast-forward main |
| Unreal milestone 2 code (click-click road drawing, undo), compile only | `agent/unreal-m2`, `..\roadworld-wt-m2` | main 269fa1e | compiles; then merge and playtest (below) |
| Hooks: build and test `brief_gate` (refuse a brief without the contract), `handin_gate` (refuse an agent finishing while the hand-in check fails, 3 tries), contract injection at agent start; table of what they close and what they cannot (owner approved 11 Oct) | `agent/rules-fix`, `../roadworld-wt-rules` | main db2d757 | hand-in passes the check; main session reads the diff, merges, runs the live test in this session |
| Vegetation kit round 2 (Blender) | `agent/veg-kit-r2`, `..\roadworld-wt-veg` | main | hand-in with composites and own fault list |

## Next, in order (start when its blocker clears)
1. **Merge Unreal milestone 2 into main, rebuild, playtest it** with `unreal\tools\Run-Playtest.ps1` extended for drawing. Blocked by: M2 agent still compiling. Unreal lane.
2. **Roundabout round** (fresh agent, new contract): arm shape and markings together to a close photo; island widens to the ring, hatching only in the nose taper, give-way line across the whole entry, islands on all arms, ring markings, no stray give-way on the west arm, explain the staircase structure at the south arm end. Blocked by: integration (needs merged junction code).
3. **T-junction round**, then **slip road round**, same way (give-way line full lane at the main road edge, side-road centre line meets it; chevrons cut cleanly between two solid lines, no holes at the nose, slip edge and lane lines, dashed line across the slip mouth; parking-strip taper). Blocked by: roundabout round (same files).
4. **Road tool follow-up** (fresh agent, new contract; source: read-only check that opened all 45 prototype screenshots in `docs/progress/2026-10-11-prototype-ui/` against `road_tool.h`, `edit_tool.h`, `stroke.h` on `agent/road-tool-2`). Blocked by: integration.
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
5. **Unreal: build the unbuilt wrapper** `Wrap_edit_tool.cpp` with the next plugin build; wire `gore_length` region table and `JunctionOverrides::gore_length`; confirm the dark slot on the raised roundabout arm is gone. Unreal lane.
6. **Unreal background visuals** (world is only a backdrop, planning log Round 50): ground material per field land use from the new `CoverGround` fields, kit hedges and trees instanced; fix tile grid on grass, heavy cloud-shadow blotches from high up, dark blue horizon band, hills barely reading from the ground. Unreal lane, after M2 playtest.
7. **Grading and structures as WorldCache layers with a dirty path** (so a drawn road gets earthworks in the sandbox). Blocked by: earthworks round 9 and integration.
8. **Road-generated content in the Unreal sandbox**: lighting columns instanced (sim_core street-light layer and kit columns exist), verge and road-edge scatter, vegetation kit and approved Poly Haven pieces imported and instanced. Unreal lane, after item 6.
9. **Cold-read reviews** (fresh Sonnet, at least 3 opened photos per case, layout scored apart from look, the open question "what would never be built?") of: earthworks after round 9, roundabout after its round, the M2 playtest pictures. Start each as its hand-in passes the check.
10. **Refresh `docs/HANDOFF.md`** sections 2, 6, 7, 8, 9, 12 to today (they describe the state before the playtest, rounds 8/4, the rules fix and Round 50). After the integration lands.
11. **Milestone 3 onward** per `docs/unreal-game-plan.md` (prototype UI parity: profile panel, readouts, warnings, edit tool).

## Machine notes
- The owner stopped the CFD job on the evening of 11 Oct, so more RAM is free. The one-editor-at-a-time rule stays (builds wait on the UBT mutex anyway); heavier parallel compiles are fine.

## Small fixes (batch into the next docs/tools agent)
- `tools/new_agent_worktree.ps1`: usage line must say `powershell -NoProfile -ExecutionPolicy Bypass -File ...` (plain invocation is blocked on this machine); CLAUDE.md gate text likewise.
- Playtest harness: about 4 in 11 runs lose all synthetic mouse input; cause not found. Rerun before believing a regression. Do not run a UBT build in another worktree while a playtest starts (mutex wait, once hung 24 min).
- World cover round 4 composites were written to `C:\Users\bgbg0\cp_scratch\pairs\` (outside the repo); move to `docs/reference/_compare/world-cover-r4/` and remove the outside folder with the owner's OK.
- Old worktrees and branches to remove once merged: `-wt-grade`, `-wt-junc`, `-wt-cover`, `-wt-tool2`, `-wt-rules`, `-wt-tool`, `-wt-world`; leftover `unreal/CityPainter/Plugins/CityPainterSim` folder.
- Pier clearance 0.3 -> 0.2 m as region data (the interchange-bridge pier test stays should_fail until then).
- Carried over from the handoff, not started: street-light incremental limit for close unconnected roads; world-cache road-mesh rebuild cost and re-measuring the 190 ms WorldCache update (milestone 4); `RoadGraph` mutators still public; big files not split (`markings.cpp`, `structure_mesh.cpp`, `render_mesh.cpp`); about 40 structure dimensions not yet in region data (`docs/repo-health-review.md`).
- Reference photos still missing: a close retaining-wall end in a soil cutting on a highway, a machined benched rock cut, UK kerbside parking near a junction, T-junction ghost island, slip gore close-ups. Toe ditch on soil cuts has no photo support.
- `agent/unreal-m2` shows no commits after several hours (tip still 269fa1e): check its working tree and STATUS when it reports; it may hold everything uncommitted.
- Markings drawn in the road material from lane UVs: spike (idea from the Road Creator Pro research).

## Waiting on the owner (not blocking)
- (Hooks: approved by the owner 11 Oct, now under Running.)
- Agents still running on briefs written before the contract: Unreal M2 and vegetation kit (both were sent the new requirements by message). Their hand-ins get the check like any other; if it fails they go back.
- Confirm the five settled contradictions in planning log Round 49 (fresh agent per round; decision split; 3 photos per case; parallel within 3-4 agents and one editor; stop rule replaces time-boxes).
- Deep cuts on steep hills: rock face (current) vs taller walls or steeper soil banks. Main session will pick a default in round 9 unless told otherwise.

## Dropped (owner decision)
- Further world-cover layout rounds, woods and hedge shape rounds, anything man-made in the starting world, a map editor: not now (Round 50).
- Workflow efficiency audit: not needed (owner, 11 Oct).
