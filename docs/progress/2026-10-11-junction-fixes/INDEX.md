# Junction fixes: before and after (2026-10-11)

Headless mesh-view renders of the `interchange` fixture (sim_core export_mesh, three.js), no Unreal. `before/` is the code before this work (with export_mesh also fed the roundabout layouts, so splitter markings show), `after/` the fixed code. Same cameras in both (`views.json`, `views_extra.json`; `regen.sh`).

| View | Shows | What to look at |
|---|---|---|
| A_roundabout_aerial60 | whole ring from 75 m up | entry dashes now at the ring edge on every arm (before: none visible, they were 44-55 m out at the end of the flare); short islands |
| B_roundabout_south_arm_aerial | south arm from 38 m up | before: 51 m constant-width cream strip; after: a 26 m kerbed triangle with a rounded nose, ladder bars and lane-edge lines on the painted gap |
| C_roundabout_south_arm_low | driver-height view along the arm | island, nose kerb, ladder bars |
| K_entry_top | straight down at the ring edge of the south arm | the entry give-way dashes on the lane (before: absent) and the island base |
| L_ref_aerial_like_09_south_arm | oblique from 75 m, framed like `docs/reference/aerial/09` (M181) | island proportions against the photo (about 17-26 m long, 4 m wide at the ring) |
| D_tee_mouth_aerial, E_tee_mouth_low | T junction (residential side road) | closed kerb loops, give-way line and triangle on the side road: unchanged and correct |
| F_slip_gore_aerial, G_slip_gore_low | diverge / merge noses | before: the gore was a hole in the surface (bare ground showing, chevrons floating on it) and 0.25-0.5 m2 pavement shards sat behind the node and past the trim; after: the gore is paved, bordered and chevroned |
