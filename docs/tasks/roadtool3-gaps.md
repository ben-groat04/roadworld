# Road tool follow-up: gaps found by the read-only check of the 45 prototype screenshots

Moved out of docs/QUEUE.md (11 Oct 2026). Source: a read-only check that opened all 45 screenshots in docs/progress/2026-10-11-prototype-ui/ against road_tool.h, edit_tool.h, stroke.h on agent/road-tool-2. Used as the brief input of the roadtool3 round (agent/roadtool3, worktree ../roadworld-wt-roadtool3). Check each item against the roadtool3 STATUS.md before re-briefing: some may be done.

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
