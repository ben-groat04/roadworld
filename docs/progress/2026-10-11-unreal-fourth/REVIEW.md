# Cold-read review: fourth Unreal set, stage1_shadows_final (2026-10-11)

## 1. Files opened
- Renders (all 9): `stage1_shadows_final/look_01_aerial`, `02_roundabout_oblique`, `03_roundabout_approach`, `04_tee_minor_arm`, `05_slip_merge`, `06_kerb_closeup`, `07_underside`, `08_dual_carriageway`, `09_tee_oblique`.
- Previous round (`unreal-third/stage3_kerb_footway_marking/`): look_01, 02, 03, 04, 05, 06, 08, 09 (no 07 existed). I also read the third-round `REVIEW.md`.
- References (15, all opened with Read; INDEX text not used as evidence):
  - aerial: `aerial/10_a16_a158_partney_roundabout_sun`, `aerial/41_kolbaskowo_roundabout_drone_poland`, `aerial/11_a68_grade_separated_roundabout_fields`
  - roundabout: `street/28_junction-roundabout-splitter-island-elevated`, `street/30_junction-roundabout-approach-zigzag`, `gsv/Screenshot 2026-10-10 175143`
  - junction/tee: `street/26_junction-t-minor-double-dash-give-way`, `gsv/Screenshot 2026-10-10 180248`, `street/35_rural-lane-hedges-sun-cloud`
  - dual/slip: `street/31_dual-central-reservation-grass-barrier`, `street/33_dual-slip-road-ladder-chevron`, `gsv/Screenshot 2026-10-10 175655`
  - kerb: `street/02_wall-sandstone-footway-low-sun`, `street/07_kerb-granite-channel-stone-wall-corner`, `street/32_dual-street-lights-verge-kerbed-channel`

## 2. Scores (previous -> now)
- Photorealism of what is generated: 3.5 -> **4.5** (+1).
- Geometry and markings: 7 -> **7** (0). The markings are unchanged and the geometry faults are unchanged.
- Lighting/exposure: not scored last round (my estimate is about 2, because of the black voids and the navy band) -> **4**.

## 3. Worst remaining fault per view
- **01 aerial:** the flat, uniform mid-green ground with dark blotches, and no field, hedge or verge structure. Target: aerial 10 (brown, yellow and green fields with hedge lines) and aerial 41 (graded verges beside the road).
- **02 roundabout oblique:** the shade under the raised kerb is still a hard-edged dark teal band (bottom, and left and right edges). A distant moire grid also shows in the grass at the top. Target: street 28 (soft shade, tarmac island edge).
- **03 roundabout approach:** the centre line is a raised dark slot or ridge running from the lower right to the middle, which reads as a mesh gap. There is no give-way line and no hatching at the mouth. Target: gsv 175143 (ladder hatch and a solid diverge line) and street 30.
- **04 tee minor arm:** the distant median or verge strips are near-black teal bands on both sides, and the hold-line dashes are hairline thin. Target: street 26 (blocky 0.3 m dashes).
- **05 slip merge:** the grey wedge sliver lying on the carriageway (right of centre, about x 1150-1410, y 630-715) reads as a ramp. The right island is a flagged slab, not a tarmac island. Target: street 33 (hatched gore, tarmac nose) and street 28.
- **06 kerb close-up:** the best view. The footway is a bit light and uniform, and the kerb face is paler than the dark granite in street 07. Target: street 07 and street 02 (kerb with a dark channel).
- **07 underside:** a blank grey-blue gradient with no geometry, so it is unjudgeable. Treat the camera as broken.
- **08 dual carriageway:** the median is a wide, saturated dark-teal strip, and the ground meets the asphalt edge with no dirt or verge line. Target: street 31 (median in even light, grass to the barrier) and street 35 (a 10-30 cm dirt line at the road edge).
- **09 tee oblique:** the slab grid across the junction asphalt and the sunburst moire in the far grass. Target: gsv 180248 (continuous tarmac, only wear lines).

## 4. Fixed / regressed / unchanged
**Fixed**
- **Navy horizon band:** gone in 03, 04, 05 and 08. A 1-2 px dark line remains at the horizon in 03. The lowest sky is still green-teal, and the haze is still slightly mismatched.
- **Black voids:** no longer pure black, and most are now dark teal-green. This is a partial fix.
- **Asphalt:** darker, neutral charcoal, closer to street 26 and gsv 175655. The tan fringe stains are gone, as are the camouflage blotches on kerbs and footways. Wheel-track wear is still weak.
- **Ground:** the hard-edged disc and lozenge patches and the crosshatch weave in 01 are gone. Hue is plausible (compare street 35).
- **Markings:** the chipped white edge lines in 08 are good.

**Shade level**
- Shade is now too dark and too saturated beside raised kerbs (02, 03, 08). In street 02 and street 28 the shaded areas are still readable, with sky fill. It is not too flat anywhere.
- Shadow direction is consistent across 01, 02, 03 and 09: shadows fall to the right or lower right, which fits one sun.
- The 08 median shadow is wide and has no visible caster. The median is only a little lower than the right carriageway, so a shadow that wide is unexplained. That is a fault.

**Regressed**
- Nothing clear. The slab grid in 09 is slightly more visible against the cleaner asphalt.

**Unchanged**
- The mouth markings: no hatch or give-way line (02, 03), tiny dashes (04, 05), and the triangle in 09 is fine.
- The 03 centre-line slot.
- The 05 wedge and slab island.
- The floating junction wedges at the horizon in 08.
- Sky clouds that tile regularly.
- The footway width of about 3-4 m in 04, 05 and 09, which is too wide against street 02 and street 32.

## 5. Ranked next changes
1. **Lighting:** raise the sky fill or ambient so shade reads at about 40-55% of lit brightness (now roughly 15%). Desaturate the shadowed grass hue by about 50%, so shade stops going teal. Target: street 02, street 28.
2. **Geometry (sim):** fix the 03 centre-line ridge or slot. Find what emits a thin vertical sliver along the lane split in the junction mouth, and weld or remove it. Also fix the 05 wedge sliver on the carriageway.
3. **Geometry (sim):** give the splitter islands tarmac tops with a 100 mm kerb, not footway flags. Add hatched gore or chevrons at slip and roundabout mouths. Add a solid give-way line plus 0.6 m dashes in 04, and make the dashes about 0.3 by 0.6 m blocks. Target: street 33, street 26.
4. **Material:** remove the junction slab grid. Make the junction asphalt a single continuous tile with the wear texture, and make the edge dirt line 10-30 cm. Target: gsv 180248, street 35.
5. **Material:** ground structure. Use a 3-scale grass blend that breaks up the sunburst moire (UV rotation or a random offset), a dirt shoulder strip, and field-colour variation at aerial scale. Target: aerial 10, 41.
6. **Missing-system / camera:** the 07 underside capture is empty, so fix the camera. Also reduce the cloud tiling regularity and fade the horizon haze to a pale grey-blue (drop the green-teal low sky).

## 6. Confidence and limits
Confidence: medium-high on tone, shadow and marking calls (15 references, 3+ per view type). Medium on exact shade ratios, which are estimated by eye rather than measured pixels. I could not judge: 07 (empty), wear on long real roads at game scale, anything involving objects not yet generated, and the camera and lens match to the references (several are Street View frames).
