# Cold-read review: second Unreal renders (2026-10-11), revised after opening the photos

Revision 2. 15 reference images were opened and looked at. GSV: `175143`, `180248`, `180306`, `180326`, `175655`, `180456`, `174828`, plus the composite `gsv/compare/...180248` (it pairs the GSV with an older render of ours, not a UE frame). Aerial: `04`, `05`, `10`, `35`. Street: `02`, `07`, `22`. `docs/unreal-photorealism.md` and the other context docs were not re-read. Views 01-06 are judged. look_07 is dropped (a flat blue-grey screen, because the road is back-face culled).

## 0. What the photos show that the INDEX text did not
- **Asphalt is neutral to slightly cool grey, never brown.** Dry UK trunk and slip surfaces are about sRGB 70-110, with albedo about 0.10-0.18. Fresh surfaces (180306, 174828) are darker and bluer, and older ones paler and greyer; old minor roads (street/35, gsv 180248, 175937) read mid to light grey, brighter than sRGB 110, so allow the upper end of the range for them (audit 11 Oct 2026). Our brown (about 110,90,70) is the single biggest colour tell. It makes the road read as soil.
- **Variation is large but patchy, not noisy.**
  - Trunk slips (175655): about 5-10% of the area is dark patches and crack-seal lines, with sparse long seams, a few metres apart.
  - Minor and rural roads (180248, 180306): faded surface with 1-3 long diagonal crack-seal lines and a few patches.
  - Neglected road (street/22): 30-50% patched.
  - Wheel tracks are visibly darker and smoother than the centre strip and shoulder (A20, 175655, 180326 shows a dark tyre band curving into the entry). They are not lighter, as my first brief said.
- **Markings are mostly crisp, bright white, 150 mm.** Wear is about 5-15% on main roads, with chipped edges. Only old minor roads (180248 give-way triangle and dashes) are faded 30-50%. Paint also follows the surface texture, so it is never perfectly flat white.
- **Kerbs are low (about 100 mm) and neutral grey,** not cream. Pre-cast concrete is light grey (street/02); granite is dark blue-grey (street/07). They are often half-buried: grass or a dirt-and-gravel strip runs hard to the kerb or over it (180248, 180306), and a dark grime line sits at the base. Footways beyond them are often tarmac or small slabs, not big pale flags.
- **Verges are untidy and green.** Mown grass strip, then rough tall grass, then hedge; there is a 0.2-0.5 m dirt and gravel fringe between the asphalt and the grass, and the edge is ragged. No surface in any photo ends at a clean line.
- **Sky and light.** Most UK photos are overcast or hazy with soft shadows (180248, 180306, 175655, 180456, aerial 05). The sunny ones (180326, aerial 10, 04, 35, street/02) have the sun at about 45-60 degrees and shadows with soft penumbra. The overcast photos have a pale sky; the sunny ones show a clear mid to deep blue (gsv 175937, 180326, street/33, 21, 04, aerial 26), so do not desaturate the sky in sun scenes (corrected after the 11 Oct audit). Aerial 04 and 35 carry green fields with colour variation.
- **The background is full.** Most street views have trees or hedges at the horizon, plus signs, vehicles and often buildings; light columns are common on trunk roads but absent on rural lanes (street/35, 12, 13), and street/10 is open moorland (corrected after the 11 Oct audit). Aerials show hedgerows, field patterns, verges with distinct slopes and shadow. Our empty brown plane and blue horizon band is the largest tell in every frame.

## 1. Scores
- **Photorealism: 2 / 10** (was 2.5). The colour is wrong (brown asphalt and brown ground), and the scenes are empty.
- **Geometry and markings correctness: 6.5 / 10** (unchanged). The marking placement and proportions in 04 compare well with 180248, and the kerb height matches. The junction detail faults stay.

## 2. Per view
Compared against the photos opened:

**01 aerial: 1.5/10.** Opened: `aerial/10`, `04`, `35`.
- It betrays itself through: roads pale beige on a brown field, where the real ones are a dark cool grey; no field pattern, hedges or trees (all three references are full of them); and no verge slopes or shadows, so the roads look drawn on.
- Wrong: the central reservation is the same colour as the floor, so the dual carriageway reads as two lines with a gap. The camera is too high (see section 5).

**02 roundabout oblique: 2.5/10.** Opened: `aerial/10`, `04`, `gsv/175143`.
- It betrays itself through: huge black shadows on a floor 30 m below (the road looks like a flyover); a central island with the same brown as the floor (real ones are mown grass with a pale kerb, trees or a chevron board); and cream kerb ribbons with no grime.
- Wrong: the splitter at lower right is an elongated sliver with a dark strip, not a teardrop; one kerb loop at upper left does not close.

**03 roundabout approach: 2/10.** Opened: `gsv/175143`, `180326`.
- It betrays itself through: the splitter island paved with huge pale flags (the references show tarmac with a ladder of white bars and a kerb); a black vertical slot at about x=880, y 620-1080; and no give-way line, edge lines or hatching on the approach.
- Wrong: the paving flags are about 3x too large, and kerb faces on the right are near-black. The camera is at about 1.7 m, so the eye height is fine.

**04 tee minor arm: 3.5/10.** Opened: `gsv/180248`, `180306`, and the compare composite.
- It betrays itself through: brown, uniform asphalt with no tracks, crack seals or patches; clean pure-white paint (the references are 5-15% chipped on main roads, 30-50% on minor roads like 180248); and a cream kerb with a clean edge to a dead floor (the references show grass or gravel over the kerb).
- Right: centre dashes, edge lines and give-way dashes are in believable places and proportions. The kerb upstand looks right.
- Wrong: a wide asphalt strip sits between kerb and edge line at the mouth.

**05 slip merge: 2.5/10.** Opened: `gsv/175655`, `180456`, `aerial/05`.
- It betrays itself through: a pale flat grey where the real slip is dark with patches; a brown gap between the carriageways (the real one has a barrier, grass and posts); and black shadow slabs.
- Wrong: the chevrons are tiny slivers at the right edge. There are no solid diverge lines or hatch outline, and the road edge steps at about x=430, y=410. The oblique view hides the gore (see section 5).

**06 kerb closeup: 2.5/10.** Opened: `street/07`, `street/02`, `gsv/180248`.
- It betrays itself through: coarse blobby asphalt with the texture scale about 3x too big; a camera at about 0.3 m (not a believable viewpoint); and an unstained, cream concrete face with no dark base line.
- Wrong: the kerb reads as a wall about 0.9 m tall; at 100-125 mm it is the camera that is too low. A dark kerb sliver at right (x~1340-1430, y~520-565) looks unfinished.

## 3. Ranked changes (re-checked against the photos)

**Cheap and big**
1. **Asphalt colour and master material (road material), a day.** First, correct hue to neutral-cool grey. Then add track, patch, crack and edge-dirt layers. Done = look_04 reads as grey UK asphalt, with darker wheel bands.
2. **Ground and verge (ground and verge), hours to a day.** Grass colour (not brown), a ragged dirt and gravel fringe against the asphalt, and grass lapping the kerb. Done = no straight clean edge between road and ground.
3. **Sky, horizon and fog (sky and atmosphere), hours.** Remove the blue band, add height fog and a paler, hazier sky. Done = horizon dissolves into haze.
4. **Kerb and footway material (road material), hours.** Neutral grey kerb (not cream), grime at the base, tarmac or small slabs at the proper scale. Remove the paved splitter. Done = look_03 has no big pale flags.
5. **Lighting and exposure (lighting and exposure), hours.** Sun 45-55 degrees with strong sky fill, so shadows are blue-grey, not black. Fixed exposure. Done = no black shadow slabs.
6. **Marking material (marking material), hours.** 5-15% erosion default with a per-instance override, bright but not pure white, pixel depth offset. Done = paint edges slightly chipped at 5 m.
7. **Camera set (look-test level setup), hours.** Use section 5, and drop look_07.

**Expensive and big**
8. **Vegetation (vegetation), days.** Hedges, trees at the horizon, grass tufts. This fills the background, which is the largest remaining tell.
9. **Street furniture (props and street furniture), days.** Light columns (8-10 m), signs, chevron boards, barriers, vehicles.
10. **Junction geometry (geometry in sim_core), days.** Close the kerb loops, a teardrop splitter with ladder bars, diverge lines at the gore, and remove the edge-line gap at the mouths.

## 4. Brief for item 1: `M_RoadAsphalt`

Corrections to my first version are marked **(changed)**. Inputs: vertex colour R = wear, G = edge dirt, B = variation; UV1 = lane-relative (U 0..1 across the lane, V metres along).

- **Textures** (from `MANIFEST.md`): `asphalt_01` (2.08 m) is the base, `asphalt_02` (3.0 m) is the worn and crack layer. `gravel_floor_02` only for the edge fringe.
- **Colour (changed).** Correct the hue in the material: neutral to slightly cool (B channel 5-8% above R). Albedo (linear) 0.10-0.18 for typical dry road. Allow 0.07 for new surfaces and up to 0.22 for old ones, set with the B channel (variation). The brown tint of the Poly Haven texture is removed with a desaturate of about 60%.
- **Anti-tiling.** World-space UVs, each set sampled twice at different rotation (about 37 degrees) and scale (1.0 and about 0.37), blended with a 20-40 m noise. Do not use UV1 for the base.
- **Macro variation.** x0.85-1.15 tone from a 30-80 m noise plus B.
- **Wheel tracks (changed).** From UV1.x, two soft bands centred at 0.25 and 0.75 (each about 0.2 wide). Inside the bands the albedo is 10-15% **darker**, roughness 0.1 lower and the aggregate normal flattened by 30%. The centre strip and shoulder are slightly paler and rougher.
- **Edge dirt (G).** Darken 20-35%, rougher, with a ragged noise edge, plus a fringe of gravel and soil where G > 0.7.
- **Wear (R) and patches (changed).** Crack-seal lines: sparse, long, one per several metres of lane (dark thin lines, from `asphalt_02` cracks). Patches: default 5-10% of the area, as rectangular or rounded blobs with a sharp edge and +-10% albedo and different roughness; a per-instance override for rural or neglected roads up to 30-40%.
- **Oil stains (changed).** Lowest priority; none of the photos show them clearly. Drop them or keep them under 1% area.
- **Roughness.** Dry 0.65-0.9. Polished tracks may fall to 0.55. Metallic 0. A wet variant can be a separate instance (street/22 shows dark, glossy puddled patches).
- **Markings (changed).** Same wear signal, 5-15% erosion on main roads (a per-instance override up to 30-50% for old minor roads), a ragged edge, albedo about 0.75, and a little asphalt normal bleed. Pixel depth offset plus 2-3 mm lift.

## 5. Camera and exposure
- **look_01:** 750 m is far too high. Use 250-350 m, oblique 50-60 degrees, about 35 mm full-frame (aerial 10, 04, 35 are oblique at about that scale).
- **look_02:** about 100 m is fine, with a 50 mm lens. Raise the floor so the road is not a flyover.
- **look_03, look_04:** match the GSV camera: 2.5 m height, wide (about 90 degree horizontal FOV, about 24 mm). The horizon sits about a third from the top.
- **look_05:** a driver view from the slip (2.5 m, 24 mm) as `gsv/175655`, not a side oblique.
- **look_06:** 1.0-1.5 m, pitched 30 degrees down, 35 mm, about 3 m from the kerb (as `street/02` and `07`).
- **Light:** for the sunny set, sun elevation 45-60 degrees (180326, aerial 10) with soft shadow edges. For the overcast set (180248, 175655), no direct sun at all and strong sky fill. Fixed exposure, about EV 13-14 for sun and EV 11-12 for overcast. The current long, black shadows imply a sun well under 25 degrees.

## 6. Confidence
- **Firm** (seen in many photos): asphalt is neutral grey, not brown; the background must be full; verges and kerb edges are never clean; wheel tracks are darker; markings are mostly crisp; kerb upstand about 100 mm and grey.
- **Less firm** (few references): the patch density figures (street/22 is a single extreme example, and 175655 and aerials give the 5-10%); marking wear percentages (only 180248 shows old paint, so the 30-50% figure rests on one image); sun elevation (read from few shadows, and Street View sun is partly overcast); the splitter-island surfacing (only 175143 and 180326 show it). The compare composite shows an older render, so it was used for framing only.
