# Work queue (the main session's only queue; if it is not here, it is not queued)

Updated by the main session every time an agent is started, an agent reports, or the owner changes direction. Newest state wins; finished items are deleted (git history keeps them). Last updated: 11 Oct 2026, evening.

## Running now
| Item | Branch / worktree | Started from | What "done" is |
|---|---|---|---|
| Earthworks round 9: disturbance view and cross-sections first, then a hard bound on changed ground on every scene, plus round 8 leftovers | `agent/grade-r9`, `..\roadworld-wt-grade-r9` | `agent/grading-r6` 2aff581 + main | `tools/check_handin.mjs` passes on `docs/progress/2026-10-11-structures-r9`; main session opens the disturbance views and sections |
| Integration: merge, in order, world cover r4 (`agent/world-cover-r2` 0da3c74), junction markings (`agent/junction-markings` d611346), earthworks r7+r8 (`agent/grading-r6` 2aff581), road tool gaps (`agent/road-tool-2` d7dfc99), then main | `integ/2026-10-11`, `..\roadworld-wt-integ` | main | clean builds, full suite on gcc and MSVC after each merge; then fast-forward main |
| Unreal milestone 2 code (click-click road drawing, undo), compile only | `agent/unreal-m2`, `..\roadworld-wt-m2` | main 269fa1e | compiles; then merge and playtest (below) |
| Road tool vs ALL prototype screenshots: read-only check, every image opened against the code (the builder opened only 3) | reads `..oadworld-wt-tool2` | - | gap list in its report; main session adds each gap to item 4 below |
| Vegetation kit round 2 (Blender) | `agent/veg-kit-r2`, `..\roadworld-wt-veg` | main | hand-in with composites and own fault list |

## Next, in order (start when its blocker clears)
1. **Merge Unreal milestone 2 into main, rebuild, playtest it** with `unreal\tools\Run-Playtest.ps1` extended for drawing. Blocked by: M2 agent still compiling. Unreal lane.
2. **Roundabout round** (fresh agent, new contract): arm shape and markings together to a close photo; island widens to the ring, hatching only in the nose taper, give-way line across the whole entry, islands on all arms, ring markings, no stray give-way on the west arm, explain the staircase structure at the south arm end. Blocked by: integration (needs merged junction code).
3. **T-junction round**, then **slip road round**, same way (give-way line full lane at the main road edge, side-road centre line meets it; chevrons cut cleanly between two solid lines, no holes at the nose, slip edge and lane lines, dashed line across the slip mouth; parking-strip taper). Blocked by: roundabout round (same files).
4. **Road tool follow-up**: edits that cross another road at the same level create a junction (prototype did); proper fix of `fit_clothoid_chain` returning a huge chain when a stroke doubles back onto an earlier point (guard `fit_sane_chain` is only a stopgap); update `docs/unreal-ui-parity.md`. Blocked by: integration.
5. **Unreal: build the unbuilt wrapper** `Wrap_edit_tool.cpp` with the next plugin build; wire `gore_length` region table and `JunctionOverrides::gore_length`; confirm the dark slot on the raised roundabout arm is gone. Unreal lane.
6. **Unreal background visuals** (world is only a backdrop, planning log Round 50): ground material per field land use from the new `CoverGround` fields, kit hedges and trees instanced; fix tile grid on grass, heavy cloud-shadow blotches from high up, dark blue horizon band, hills barely reading from the ground. Unreal lane, after M2 playtest.
7. **Grading and structures as WorldCache layers with a dirty path** (so a drawn road gets earthworks in the sandbox). Blocked by: earthworks round 9 and integration.
8. **Milestone 3 onward** per `docs/unreal-game-plan.md` (prototype UI parity: profile panel, readouts, warnings, edit tool).

## Small fixes (batch into the next docs/tools agent)
- `tools/new_agent_worktree.ps1`: usage line must say `powershell -NoProfile -ExecutionPolicy Bypass -File ...` (plain invocation is blocked on this machine); CLAUDE.md gate text likewise.
- Playtest harness: about 4 in 11 runs lose all synthetic mouse input; cause not found. Rerun before believing a regression. Do not run a UBT build in another worktree while a playtest starts (mutex wait, once hung 24 min).
- World cover round 4 composites were written to `C:\Users\bgbg0\cp_scratch\pairs\` (outside the repo); move to `docs/reference/_compare/world-cover-r4/` and remove the outside folder with the owner's OK.
- Old worktrees and branches to remove once merged: `-wt-grade`, `-wt-junc`, `-wt-cover`, `-wt-tool2`, `-wt-rules`; leftover `unreal/CityPainter/Plugins/CityPainterSim` folder.
- Pier clearance 0.3 -> 0.2 m as region data.
- Markings drawn in the road material from lane UVs: spike (idea from the Road Creator Pro research).

## Waiting on the owner (not blocking)
- Confirm the five settled contradictions in planning log Round 49 (fresh agent per round; decision split; 3 photos per case; parallel within 3-4 agents and one editor; stop rule replaces time-boxes).
- Deep cuts on steep hills: rock face (current) vs taller walls or steeper soil banks. Main session will pick a default in round 9 unless told otherwise.

## Dropped (owner decision)
- Further world-cover layout rounds, woods and hedge shape rounds, anything man-made in the starting world, a map editor: not now (Round 50).
- Workflow efficiency audit: not needed (owner, 11 Oct).
