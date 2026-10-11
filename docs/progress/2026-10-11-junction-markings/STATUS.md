# STATUS (WIP checkpoint; builds on gcc, suite NOT green yet)

Done in code (rendered once, not yet cold-read):
- Item 1 splitter paint: bars end on boundary strokes (centre lines), painted taper beyond the nose limited by region data `splitter.taper_length_m` / `taper_max_island_ratio`; boundary lines run from the ring edge to the flare end.
- Item 2: splitter paint was buried under the junction mesh near the ring (z followed the arm, not the junction blend): now follows JunctionSurface. `use_roundabout_give_way` default true, empty list = every inbound non-ring arm is an entry; entry style `give_way_ring` (0.3 w, 0.9 mark, 0.6 gap).
- Item 3 (part): major road centre line carried across the side-road mouth (JGen::through_centre_lines); bay line stops `keepout_m` (10 m) short of a junction (BoundaryRule.keepout_m). Taper-out of the parking strip: NOT done.
- Item 4 (part): continuing lane lines through merge/diverge (JGen::through_lane_lines). NOT started: pavement/paving removal in gore, longer speed-derived chevron area + override, mouth dashed line.
- Item 6 (part): junction triangle edge cap 5 m -> 2 m (render_mesh.cpp junction_edge_length). Dark slot on the elevated arm: not investigated.
- Item 5: not started.

Known red tests (old tests assume old behaviour): test_markings 1411 (splitter paint test: bars no longer start at the old inset), 702/824 overlaps, test_structure_mesh 945/946 (check whether caused by 2 m junction cap).

Next: fix/update those tests, add geometric tests (step 3), items 4-6, MSVC clean build, final renders + INDEX.md checklist.
