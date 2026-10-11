# Junction markings round (2026-10-11): renders, reference pairing, checklist

Renders: headless mesh-view of the `interchange` fixture (sim_core export_mesh, three.js), same cameras as `../2026-10-11-junction-fixes/` (`regen.sh <export_mesh.exe> <folder>`). `before/` = code at the start of this round, `after/` = this branch. Photos are private (docs/reference, not committed). The renders are flat-colour mesh views, not Unreal.
NOT done: renders framed like each best reference photo beyond the L view (only the junction-fixes cameras A-L were rendered), because no top-down camera per case was built; every comparison below is by opening the photo and the render side by side.

| Render (after/) | Compare with | Case |
|---|---|---|
| A_roundabout_aerial60.png | aerial/09_m181 (whole ring), aerial/61 Cutteslowe (12.5 cm ortho) | whole roundabout |
| B_roundabout_south_arm_aerial.png | street/28, gsv 180326, gsv 175143, aerial/63 | arm + splitter island |
| K_entry_top.png | aerial/63 (south arm crop), aerial/41 | entry from straight above |
| L_ref_aerial_like_09_south_arm.png | aerial/09_m181 | island proportions |
| C_roundabout_south_arm_low.png | gsv 180326 | driver view of arm |
| D_tee_mouth_aerial.png, E_tee_mouth_low.png | aerial/15, gsv 180306, gsv 180248 | T junction (E camera is a poor angle: mostly sky/grass) |
| F_slip_gore_aerial.png, G_slip_gore_low.png | street/33, gsv 175655 | slip diverge and merge |

## Checklist (each line: joins / continues / ends where the photo's does; honest)
Roundabout arm and island (B, K, L vs street/28, gsv 180326, gsv 175143, aerial 41/63)
- Kerbed island visible, widest at the ring, nose pointing away: YES (grass teardrop now stands on the junction surface; before: asphalt-on-asphalt, invisible).
- Island proportions like the photos: NO. Island is about 4 m wide x 26 m long (about 1.2 x 7.5 lane widths); photos show 1.2-1.5 x 4-6 lane widths (orthophotos 61/63: 10-20 m).
- Hatch bars end on boundary lines (within 2 cm, tested): YES.
- Bars fill the painted area ahead of the nose like photo 180326 (wedge fully hatched): NO. Bars run only along the island flanks and 6-12 m past the nose; the painted wedge beyond that is an empty outline.
- Boundary lines converge to the single centre line past the nose: PARTLY. Lines stay 4.4 m apart for about 27 m (the road flare holds to the junction corner trim) and only then converge over 14 m; photos converge within 2-4 lane widths of the nose.
- Give-way dashes across the inbound lane at the ring edge: YES (0.9/0.6 m dashes, on the junction surface; default on, no caller flag). Only one row, only the inbound lane (correct).
- Ring outer/inner edge lines continuous through arm seams (A): NO. The ring edge lines (photos 40/41/61) are not drawn; ring lane dashes exist; arm/ring seam joins only for the splitter lines (they start at the ring edge).
- Centre line continues beyond the taper: YES at the flare end (edge line starts there); gap not measured.
- Exit side marked: PARTLY (island outline only; no exit edge line).
T junction (D vs aerial/15, gsv 180306)
- Major road centre line continues across the side-road mouth: YES (flush dashes; no gap beyond the style gap, tested both driving sides).
- Give-way line + triangle on the side road: YES (unchanged).
- Parking bay lines/dividers stop 10 m short of the mouth: YES (tested); strip taper-out to the kerb: NO (not done).
Slip diverge/merge (F, G vs street/33, gsv 175655)
- No paving/footway in or beside the gore: YES (tested); a bare-ground notch remains at the nose between the two roads (verge), as a verge would.
- Chevron area long, speed-derived (35 m at 80 km/h design speed), per-node override: YES (`JunctionOverrides::gore_length`).
- Main lane lines continue through the junction: YES for the main carriageway pair; slip lane lines: NO (single lane).
- Edge line peeling away along the slip: YES (existing slip_edges), unchanged.
- Dashed line across the slip mouth: NO (not done).
- Chevron size compared with photo: NO (photo street/33 bars are perpendicular ladder bars about 1 m apart; the sim keeps chevrons as chosen by the owner).
Slot / slab grid
- Junction triangle edge cap 5 m -> 2 m: done (counts not measured: not reported, listed in STATUS).
- Dark slot on the elevated arm: probable cause fixed (island and paint stood on the arm profile while the asphalt is the junction blend: 10-25 cm mismatch; both now use the junction surface); not confirmed in Unreal.
