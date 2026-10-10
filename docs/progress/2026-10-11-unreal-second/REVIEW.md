# Cold-read review: second Unreal renders (2026-10-11)

Reviewed from the seven PNGs and the references only. No builds run. The view numbers below are the look_0N files.

## 1. Scores
- **Photorealism vs references: 2.5 / 10.** It reads as an engine gizmo scene. The surfaces are uniform, the ground is a flat brown plane, there are no props, and the sky looks stock.
- **Geometry and markings correctness: 6.5 / 10.** The layout is plausible and the junction topology is right. The marking vocabulary is mostly UK-correct. Several mouth and kerb details are wrong (see below).

## 2. Per view

**01 aerial: 2/10.** Closest reference: `aerial/10_a16_a158_partney_roundabout_sun.jpg` (and `aerial/04`).
- It betrays itself as CG through: a flat, vignetted brown field with no field pattern, hedges or trees; roads of identical, unvarying beige-grey with no wear or patching; and a perfectly straight, ruler-drawn grid.
- Wrong or odd: the verge and central reservation are the same brown as the floor, so the dual carriageway reads as two lines with a gap. Roads are too thin to read from 750 m, and the camera is too high. The Partney reference was taken from about 300 m, oblique, with hedged fields.

**02 roundabout oblique: 3/10.** Closest reference: `aerial/10` (junction) and `gsv/Screenshot 2026-10-10 175143.png`.
- It betrays itself through: huge, hard, pitch-black shadows on a floor 30 m below (the road looks like a flyover); a brown, zero-texture central island with no grass or planting; and a kerb line that is a bright cream ribbon with no dirt or shadow gradient.
- Wrong: the splitter on the lower right is an elongated cream sliver with a dark strip in it, not a teardrop island. The top-left arm has a kerb loop that does not close onto the carriageway (an unfinished sliver near x=520, y=400). Several arms have no visible markings from this height.

**03 roundabout approach: 2/10.** Closest reference: `gsv/Screenshot 2026-10-10 175143.png` (Thetford Rd) and `gsv/... 180326.png`.
- It betrays itself through: a splitter island paved with large light flags, which is not a UK treatment (it should be tarmac or grass with a kerb and a ladder of marks); a stark black vertical shadow or slot at x~880, y 620-1080; and no give-way line, no edge lines and no arrows on the approach.
- Wrong: the pavement slab scale is too large and too regular. The kerb face on the right is almost black, with no weathering. The kerb at the left foreground is much taller than 100-125 mm relative to the car-scale road. The camera height is about 1.7 m, so the view is roughly right.

**04 tee minor arm: 4/10.** Closest reference: `gsv/Screenshot 2026-10-10 180248.png` (A40 Brynich T).
- It betrays itself through: asphalt that is one flat brown with no wheel tracks, patches or crack seals; paint that is razor-sharp and pure white with no chips; and a symmetric, clean kerb with a plain cream pavement and no grass boundary.
- Right in this view: the centre dashes, solid edge lines and give-way dashes are in believable places. Wrong: the lane-edge lines are not tied to the kerb, so there is a wide asphalt strip between the kerb and the edge line at the mouth. The brown asphalt also looks like soil.

**05 slip merge: 3/10.** Closest reference: `gsv/Screenshot 2026-10-10 175655.png` (M11 slip) and `gsv/... 180456.png`.
- It betrays itself through: a pale, flat, over-bright grey that has lost all its asphalt texture at this distance; a wide brown strip between the carriageways (the reservation) that looks like a hole; and giant black shadow slabs.
- Wrong: the chevrons are tiny and sit in thin slivers at the right edge, with no solid diverge lines and no hatching edge. A step at x~430, y~410 (the road edge jogs) suggests a width mismatch. The view is too oblique to judge the gore.

**06 kerb closeup: 3/10.** Closest reference: `gsv/Screenshot 2026-10-10 180248.png` (kerb at the bellmouth).
- It betrays itself through: asphalt whose texture scale is wrong, with blobby, oversized aggregate ("brain coral") repeating across the whole foreground; a camera at about 0.3 m that is not a believable viewpoint; and concrete with no staining at the base.
- Wrong: the kerb reads as a wall about 0.9 m tall, which looks like a barrier. At 125 mm it would be hard to see from here, so the camera height or the kerb is wrong. A dark kerb slab sliver at right (x~1340-1430, y~520-565) looks unfinished.

**07 underside: 0/10 for comparison.** A flat blue-grey screen. It is useless as a view because the road is back-face culled. Make the materials two-sided, or drop the view.

## 3. Ranked changes

**Cheap and big**
1. **Sky, ground and horizon (ground and verge / sky and atmosphere), hours.** Replace the brown plane with a grass-textured landscape or a scatter of Poly Haven grass at hundreds of metres. Fix the blue horizon band with exponential height fog. Done = no brown or blue bands in any view.
2. **Asphalt master material (road material), a day.** Wear, edge dirt, wheel tracks, patches and cracks from vertex colour and UV1. Done = look_04 has a lighter centre band, darker wheel tracks and a ragged edge. See section 4.
3. **Kerb and pavement materials (road material), hours.** Paving texture at 450x600 mm scale, with edge dirt and a dark grime band at the base. Replace the splitter-island paving with tarmac or grass inside a kerb. Done = look_03 has no big pale flags.
4. **Lighting and exposure (lighting and exposure), hours.** Lower the sun (about 30-40 degrees), warm the colour, add a moderate fill, and reduce the shadow contrast on the floor so shadows are not pure black. Lock the auto-exposure range. Done = shadows on the road have a blue-grey fill and are not black holes.
5. **Marking material (marking material), hours.** Add wear mask, a slight roughness, and a thickness bump, and make paint 92% white, not 100%. Add pixel depth offset to remove the z-fight risk. Done = paint edges are slightly broken at 5 m.
6. **Camera set (look-test level setup), hours.** Use the matched cameras in section 5 and drop or fix look_07.

**Expensive and big**
7. **Vegetation and verge (vegetation), days.** Grass verges that run hard up to the kerb, hedgerows and trees along the roads. This is the largest realism gap in every reference.
8. **Street furniture (props and street furniture), days.** Lighting columns (8-10 m), give-way signs, chevron boards on the central island, and barriers on the dual carriageway.
9. **Central reservation, slip gore and splitter geometry (geometry in sim_core), days.** Finish junction slivers and kerb ends. Add solid diverge lines and a proper teardrop splitter with a ladder of marks.
10. **Post-process and camera (post-process and camera), hours to a day.** A real focal length with mild vignette, a little bloom, a film curve and slight chromatic aberration.

## 4. Brief for item 2: asphalt master material `M_RoadAsphalt`

Inputs: vertex colour R = wear, G = edge dirt, B = variation. UV0 is world-metric, and UV1 is lane-relative (U across the lane, 0..1 from the left edge to the right edge; V along the lane in metres).

Base layers (from `MANIFEST.md`): `asphalt_01` (2.08 m) is the base, with `diff`, `nor_dx`, `rough` and `ao`. `asphalt_02` (3.0 m) is the worn and crack layer. Use `gravel_floor_02` only sparingly for the exposed-aggregate patches at heavy wear.

- **Anti-tiling.** Sample each set twice, at two rotations (for example 0 and about 37 degrees) and two scales (1.0 and about 0.37), blended with a low-frequency noise (period about 20-40 m). Sample with world-space UVs plus a per-tile random offset. Do not use UV1 for base textures.
- **Macro variation.** Multiply albedo by 0.85-1.15 using a 30-80 m noise, plus B channel (variation) to shift the tone and warm or cool by +-5%. Keep dry asphalt albedo 0.06-0.12 (dark grey, not brown). Desaturate the Poly Haven tint by about 40%, because it currently reads brown.
- **Wheel-track polish.** In UV1.x, build two bands centred at about 0.25 and 0.75 (each about 0.2 wide, soft). Inside the bands lower the roughness by 0.1 and lighten albedo by 3% (polish) with fewer aggregate normals (flatten the normal 30%). Between bands (centre) push roughness up and lighten slightly for the pale central strip seen in the references.
- **Edge dirt.** G channel: darken albedo by 20-35% and raise roughness, with a ragged edge from a noise threshold. Add a gravel and soil fringe using `asphalt_02` and `gravel_floor_02` blended where G > 0.7.
- **Wear (R).** Blend in `asphalt_02` for cracks, a paler worn surface and reduced AO. Above 0.5, use the crack height map to cut hairline crack seams, and use a low-frequency threshold to create patches (rectangular darker or paler repair patches, sharp edge, different albedo by about 10%).
- **Oil stains.** A 2D noise mask along the lane centre (UV1.x about 0.5, +-0.1) with a threshold to give dark, glossy blobs: albedo x0.6, roughness 0.25.
- **Roughness range.** Dry asphalt 0.65-0.9 (never below 0.55 except in oil and polish). Specular 0.5. Metallic 0.
- **Normal.** Blend the two normals; tessellation or parallax is not needed. Keep the aggregate scale such that grains are about 5-10 mm.
- **Markings.** In `M_Marking` reuse the same wear signal (R), threshold a high-frequency noise to erode 10-40% of the paint with a rough edge, tint toward grey-white (0.75 albedo), and let the asphalt normal show through (blend height). Add a small pixel depth offset and a 2-3 mm height.
- **Outputs.** Expose scalar parameters for wear, dirt and variation strength, and make instances `MI_RoadAsphalt_New`, `_Worn` and `_Patched`.

## 5. Camera and exposure notes
- **look_01:** 750 m is too high and too flat. Use 250-350 m, pitched 55-60 degrees, about 35 mm full-frame equivalent, as in `aerial/10`. At 750 m the roads are 2 px wide.
- **look_02:** 100 m is reasonable. Add a long lens (about 50 mm) so perspective is calmer. The floor 30 m below ruins the comparison, so raise the floor to the road level, with a gentle fall.
- **look_03 and look_04:** Use eye height 1.4-1.5 m, a 24 mm (about 74 degree horizontal) lens, as in the GSV images (about 2.5 m for GSV, though, so use 2.5 m for a direct GSV match and a 90 degree horizontal FOV). Frame the horizon at one third from the top.
- **look_05:** Use a driver view from the slip road (2.5 m, 24 mm) looking at the merge, as `gsv/... 175655.png`, not an oblique from the side.
- **look_06:** Raise the camera to about 1.0-1.5 m and angle 30 degrees down, with a 35 mm lens, at about 3 m from the kerb.
- **look_07:** Drop it, or make the materials two-sided.
- **Time of day and sun.** The GSV and aerial shadows (`gsv/175143`, `aerial/10`) are short and soft: sun elevation about 40-55 degrees, azimuth roughly south-west, April/May late morning, thin high cloud. Our current shadows are very long and black, which suggests the sun is under 25 degrees. Aim for 45 degrees elevation and a sky fill, exposure about EV 13-14 with a fixed exposure, not auto.
