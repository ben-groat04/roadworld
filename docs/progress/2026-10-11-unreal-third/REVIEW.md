# Cold-read review: third Unreal set (2026-10-11)

Judged: `stage3_kerb_footway_marking/` (all 8 views); stage1 and stage2 `look_04` opened to see what each step changed.

**Reference files opened (11):**
- GSV: `Screenshot 2026-10-10 180248`, `180326`, `175655`, `175143`.
- Aerial: `10_a16_a158_partney_roundabout_sun`, `11_a68_grade_separated_roundabout_fields`, `41_kolbaskowo_roundabout_drone_poland`.
- Street: `31_dual-central-reservation-grass-barrier`, `02_wall-sandstone-footway-low-sun`, `26_junction-t-minor-double-dash-give-way`, `35_rural-lane-hedges-sun-cloud`.

Not opened: the INDEX tone remarks and the previous REVIEW's photo claims, which I used only to choose files.

## 1. Scores (before -> now)
Overall: **photorealism 2 -> 3.5; geometry and markings 6.5 -> 7**. Markings now exist at all; the junction faults remain.

| View | Photoreal | Geom/markings (now) | Opened for it |
|---|---|---|---|
| 01 aerial | 1.5 -> 3 | 6.5 | aerial 10, 11, 41 |
| 02 roundabout oblique | 2.5 -> 3.5 | 6 | aerial 41, 10; gsv 175143 |
| 03 roundabout approach | 2 -> 3 | 4.5 | gsv 175143, 180326 |
| 04 tee minor arm | 3.5 -> 5 | 7.5 | gsv 180248; street 26, 35 |
| 05 slip merge | 2.5 -> 4 | 5.5 | gsv 175655; street 31 |
| 06 kerb closeup | 2.5 -> 4.5 | 7 | street 02, 35 |
| 08 dual carriageway (new) | 4 | 7 | street 31; gsv 175655 |
| 09 tee oblique (new) | 4 | 7 | aerial 41; gsv 180248 |

Per-view geometry scores for the previous pass were not given, so only the "now" column is scored.

## 2. Did each change land?
- **Asphalt tone: better, the biggest win.**
  - Stage 1 `look_04` was brown, stage 2 mauve-grey, stage 3 neutral mid-grey. The brown tell is gone.
  - It is still too light and too warm-tan compared with the photos. Street 31 and 26 and gsv 175655 show dark charcoal (roughly sRGB 55-85), and ours sits near 110-125 in 04, 05 and 08.
  - Hue: the junction areas in 03 and 06 skew warm brown, while 04 and 08 are close.
- **Wheel tracks: barely there, so under-delivered.**
  - 05 (lower centre) and 03 (centre-left) show darker bands, but they read as blotchy stains, not two parallel bands.
  - In 08 (the straight road) no tracks are readable at all. gsv 175655 shows a clear dark band in the left lane.
  - 12% darkening is too weak; the tracks also need to be longer and more continuous.
- **Cracks: good.** The long crack-seals in 08 (right lane, running toward the horizon) are sparse and long, like gsv 175655.
- **Edge fringe: same problem, worse in character.**
  - Tan or ochre blotches 0.5-1.5 m wide at junction mouths and kerb lines: 02 (below the island kerb), 03 (both sides of the mouth), 09 (inner corners).
  - They are too wide and too saturated yellow-orange, and they look like a stain, not gravel. Street 35 and gsv 180248 show a thin, even dirt line about 10-30 cm.
  - Along straight kerbs (04, 08) the fringe is fine or absent.
- **Tiling and seams:**
  - 09 shows a visible rectangular slab grid across the junction asphalt, so it reads as concrete panels.
  - Macro variation is otherwise subtle and OK.
- **Kerb: better.**
  - In 06 it is neutral grey with joints and a dark base, and it reads correctly. Stage 1 was cream.
  - The kerb tops in 01, 02 and 09 carry dark camouflage blotches that read like leopard print. That is over-strong.
- **Footway: better, but too uniform and too wide.**
  - Flags are now credible in scale (06). The footway is 3-4 m wide in 04 and 05, which looks like a runway apron. Street 02 shows a narrow strip.
  - The dark blotching is too bold in 01 and 09 and on the 06 flags.
- **Markings: much better, the clearest improvement.**
  - They are now crisp, white and slightly chipped. The edge-line chipping in 08 (right edge, lower right) is good and plausible for a trunk road.
  - Pure white reads slightly hot; street 26 paint is cream-grey.
  - The give-way triangle and dashes in 09 match gsv 180248. In 04 the junction dashes are thin and tiny.
  - No over-chipping.
- **Ground: hue now right, structure wrong.**
  - It is olive green, no longer brown, which matches street 35 and 31 in hue.
  - It is duller and more uniform than street 35 (bright mid-green) and aerial 41.
  - The disc and lozenge "field patches" have hard edges (01 and 02 centre-top, 03, 05 left) and a crosshatch weave in 01.
  - Aerial 10 and 11 show hedge-bounded fields with variation along straight boundaries, not soft circles.
- **Fog and horizon: same.** The dark navy band at the horizon is still there (03, 04, 05, 08 at y about 480-530). The lowest sky also goes green-teal.
- **Sun, sky, exposure and shadows:**
  - Sky: a deep saturated blue with cumulus is plausible for sunny UK (gsv 180326, street 35). The cloud tile pattern is quite regular.
  - Exposure: OK, and the road is not over-exposed.
  - Shadows: **worse in effect**. Hard black voids (see section 3) dominate. Real shadows are soft and blue-grey.

## 3. Outright wrong: confirm or reject, ranked by damage
1. **Black shadow slabs: confirmed, worst.**
   - 02 (bottom, left), 03 (left, right), 08 (median, the big black strip), 09 (hard black line under the footway), 05 (under the kerb at right).
   - They are near-black, with no sky fill. This breaks every view.
2. **Horizon band: confirmed.** A thin dark navy line sits across 03, 04, 05 and 08, against hazy horizons in all the photos.
3. **Floating roads: confirmed.**
   - The roads sit on bare wedges with black voids beneath (02 bottom, 08 distant junction wedges).
   - Aerial 41 shows embankments and grass slopes.
4. **Fringe at junction mouths: confirmed** (see section 2).
5. **Mouth markings: confirmed.**
   - 03 has no give-way line or hatching, only a single dark pale-edged line down the middle that reads as a slot or crack. gsv 175143 and 180326 show ladder hatch and a solid diverge line.
   - 05 has only sparse edge marks. The 09 mouth is better (triangle and dashes).
6. **Splitter in paving: confirmed.** In 05 the right-hand island is a grey flagged slab. gsv 175143 shows a tarmac island with a kerb.
7. **Aerial crosshatch weave: confirmed**, plus the hard-edged field discs. This is the single most obvious "CG" tell in 01.

## 4. Top ten for the next round
**What a tweak can still buy (material, lighting, post):**
1. **Shadow slabs** (lighting and exposure). Hours. Done = no black region darker than about 40% of the sunlit road tone; the median in 08 reads as shaded grass.
2. **Horizon band and haze** (sky and atmosphere). Hours. Done = ground blends into pale haze, with no navy line and no green tint in the sky.
3. **Asphalt tone, tracks and fringe** (road material). Hours. Done = 08 and 05 read as dark charcoal with two continuous darker bands; the 03 mouth fringe is a thin line.
4. **Ground field-patch edges and crosshatch** (ground and verge). Hours. Done = no hard-edged discs in 01 or 02; the weave is invisible at 296 m.
5. **Kerb and footway blotch strength, footway width** (road material, geometry in sim_core). Hours. Done = no leopard print; the footway is about 2 m wide.
6. **Junction-mouth markings and splitter** (geometry in sim_core). A day. Done = give-way line, hatch and a tarmac island visible in 03 and 05.
7. **Marking paint** (marking material). Hours. Done = cream-white, with edge chips on at least the minor roads.

**What now needs new content:**
8. **Embankments and graded terrain** (geometry in sim_core / ground). Days. Done = no floating roads, with slopes on the aerial.
9. **Vegetation** (vegetation). Days. Hedges, trees at the horizon, tall verge grass: the biggest remaining tell in every ground view.
10. **Street furniture** (props). Days. Lighting columns, signs, barriers, chevron boards.

## 5. Parameter guidance for items 1-3
1. **Shadows** (the settings are in `setup_look_test_level.py`; the asphalt and ground parameters cannot fix this).
   - Raise sky light intensity from 3.0 to about 5-6, or add a fill/ambient lift for the runtime meshes.
   - Alternative: switch the road meshes to something Lumen can see.
   - Target: median shadow in 08 close to the grass tone. Street 31 shows the median in full light with a pale overcast sky.
2. **Horizon.**
   - Raise height fog density 0.006 -> about 0.012.
   - Match the fog colour to the pale haze at the horizon, and extend or fade the ground.
   - Reference: the horizon in gsv 180248 and street 35 is a pale blue-grey.
3. **Asphalt.**
   - `AlbedoScale` 1.05 -> about 0.75, and `Desaturate` 0.7 -> 0.8. Charcoal in street 31 and gsv 175655.
   - `TrackDarken` 0.12 -> about 0.22, `TrackWidthM` 0.35 -> about 0.45, and `TrackRoughDelta` -0.10 -> -0.15.
   - `FringeAmount` 0.6 -> about 0.25, `FringeThreshold` 0.76 -> about 0.88. Darken and desaturate `FringeTint`, reduce `FringeGravelScale` 0.35 -> about 0.25, and cut `EdgeRagged` 0.6 -> 0.4. Street 35 shows a thin dirt line.
   - Ground (if time): `FieldContrast` down by about half, and `GrassTint` slightly more saturated, since street 35 is a brighter green.
   - Marking (if time): `PaintBrightness` 0.92 -> about 0.85. `Erosion` 0.10 is fine; raise it only for the old minor-road instance.

## 6. Confidence
- **Firm:**
  - The black shadows, horizon band and fringe width are plain in the images.
  - Asphalt is too light for trunk roads (street 31, 26 and gsv 175655 agree).
  - Marking quality is good.
  - The ground hue is acceptable and its structure is wrong.
- **Few photos behind them:**
  - Wheel-track strength (gsv 175655 and street 31 only).
  - Footway width (street 02 only).
  - Paint cream versus white (street 26 only).
  - Rural embankment form (aerial 41 plus 10 and 11).
