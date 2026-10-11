# FAULTS (Unreal milestone 2 run)

Asked of every picture: what would a player who knows the prototype say is missing, different, ugly or confusing?

## Pass 1 (first full set of state pictures; opened the raw renders and composites)

1. Ghost near-black instead of pale 55% white; the red error ghost was black too. Cause: the engine debug material emits colour 1.0 and the level uses fixed EV100 14, so emissive 1 is black. Fixed: new M_Ghost with Brightness 20000. Re-rendered: compare/s13, s18 (pale; red on error).
2. Two HUD panels overlapped (the milestone 1 debug readout on the tool HUD, worst on dual types). Fixed: debug readout moved bottom-left.
3. HUD text about twice the prototype's size. Fixed: 14 px tool HUD, 13 cue, 12 debug.
4. The ghost vanished when the pointer sat on the just-placed point (3-point stroke before finishing, refused finish): Click cleared the held ribbons. Fixed in FCPToolSession::Click. Re-rendered: compare/s26.
5. Min-span error could not be shown at the overview zoom: a 2 m offset is inside the finish ring (a pixel radius), so the HUD said "finish". Same as the prototype at that zoom; the state is now shot zoomed in: compare/s12 matches (red text, no ghost).
6. "inf" printed where the prototype prints the infinity sign. Fixed.
7. My first states script put points off screen so half the states drew nothing. Fixed by calibrating coordinates; every state now logs its HUD text (hud_text.txt) so an empty state shows.

## Pass 2 (re-render at 1600x900; opened every composite in COMPARE.md plus s27b, s27c, s30, s31)

1. Finished roads lose their asphalt where the profile runs below the terrain mesh (no earthworks), leaving kerbs and markings; at a free end the asphalt looks like it tapers to a point. Worst on slopes (s30). Not fixable in M2 (earthworks item).
2. Ghost is carriageway width; the prototype's is total width incl. shoulders (GAP-9, needs the sim_core merge).
3. No profile panel, no G2 tick, no tuning panel, no scene bar, no colour modes (after M2).
4. Ground and sky look unlike the prototype (dark, flat): world look item, not M2.
5. The offset-guide polyline kinks near one end of the road (from the sim guide).
6. A refused finish keeps its red text; the ghost goes red only while a ribbon is shown.
7. Warnings "kerb: mixed tangent handedness" on every curved commit (sim_core).

No pass 3: pass 2 found nothing more that I can fix inside milestone 2.

## Residual

- Asphalt under terrain / taper at road ends (needs earthworks, next queue item). Evidence: renders/20_st_s27b_finished_topdown.jpg, s30_road_on_slope.jpg.
- Ghost width, G2 flag, profile series: wait for the sim_core merge (Unreal side: BuildLines, DrawRibbon, new profile widget).
- Esc in play-in-editor not run; overlay is debug drawing (not in Shipping, M8); ghost through hills not pictured.
- Ground and sky look; profile, tuning and scene panels.
- Harness loses mouse input in about 2 of 9 runs (rerun).
