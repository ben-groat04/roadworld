# Reference claims audit (11 Oct 2026)

Why: a review brief on road materials was written after opening 2 of about 90 reference photos, and its guesses (brown asphalt, lighter polished wheel tracks, heavy marking wear, dense patches and cracks, oil stains, cream kerbs) were handed to coding agents as fact. `progress/2026-10-11-unreal-second/REVIEW.md` is the corrected version (15 photos opened). This audit hunts the same failure everywhere else. Method: list each checkable appearance claim, then open the images. 39 reference images were opened for this audit (list at the end), plus a pixel measurement of four Poly Haven source textures.

Verdict key: S supported, C contradicted, U unsupported (no opened reference shows it either way), N not checkable from our references.

## Part A: the six known wrong claims, everywhere in the repo

Result: the wrong numbers did not leak into docs, tests or sim_core colours. They live in texture choice, material defaults and wear defaults.

| Claim | Where it appears | Verdict | Action |
|---|---|---|---|
| Brown or warm asphalt | `unreal/CityPainter/SourceArt/Textures/PolyHaven/MANIFEST.md:12` and `manifest.json:79` call `asphalt_01` "grey-brown ... without strong colour cast". Measured mean sRGB of `asphalt_01_diff_2k.jpg` is (89, 80, 70): B is 21% below R, a strong warm cast. `asphalt_02` is (91, 90, 85), nearly neutral. | C | Agent-owned, listed below. This is the real source of the brown road. |
| Cream or pale kerb | MANIFEST.md:57 / manifest.json:301 call `concrete_wall_009` "plain light grey". Measured (161, 154, 140): warm, B 13% below R. `concrete_pavement` (128, 117, 103) is warmer still. References show neutral grey kerbs (street/02, 07, 28; gsv 175143, 180248). | C | Agent-owned, listed below. |
| Wheel tracks lighter or polished | Four index entries (gsv 174828, 180306; aerial 01, 05, 10, 34) and two guesses (street/22, 05) said lighter or polished tracks. | C for 174828 and 180306 (tracks are darker); U for the aerials (nothing visible at that range) | Fixed in the three INDEX files. |
| Marking wear 10-40% by default | `data/regions/uk/marking_rules.json:8` `strength 0.3`, `wheel_boost 0.35`; `markings.cpp:851,854` and `markings.h:322,325` use the same fallbacks; `testland` 0.45. `markings.cpp:1023` makes mean wear about `strength`, so the default is about 30% before the wheel boost. Photos show 5-15% on main roads, 30-50% only on old minor roads (gsv 180248, street/26 left half). | C | Agent-owned, listed below. |
| Patches and cracks dense by default | No doc or default states a patch density. `mat_defs.py` has no asphalt master yet. Photos: patches 5-10% on trunk and slip roads (gsv 175655), crack seals sparse. Street/22 and gsv 180653 are neglected-road extremes. | S (nothing wrong present) | None. |
| Oil stains | `docs/unreal-photorealism.md:51` lists "oil stains" among the realism breakers. None of 39 images show oil stains. | U | Tagged inline. |

Other colour constants checked:
- `prototypes/road-draw/src/roadmesh.js:31`: asphalt `#3c3f44` (cool neutral, hue right, but darker than the 0.10-0.18 albedo seen in photos), kerb `#9d9c96` (neutral grey, right), paving `#bdbcb7` and concrete `#d2d0c9` (neutral, pale), marking `#f2f2f2`. Throwaway prototype and `index.html` is generated, so left alone.
- `prototypes/road-draw/src/terrain.js:216`: "earthy brown" soil for fresh cut faces. Photos of raw earthworks (aerial 26) show tan to pink soil, so this is plausible. Not changed.
- `tools/blender_kit`: kerb and paving slots use Poly Haven textures (None in `SLOT_PREVIEW`), no hard-coded kerb colour.
- No test pins an asphalt, kerb or paint colour; `tests/test_region_neutrality.cpp:413-416` pins wear values to the JSON, not to a number.
- `unreal/tools/py/setup_look_test_level.py:47` white paint (0.90, 0.90, 0.88) is above the albedo of about 0.75 the corrected review suggests.

## Part B: claim-by-claim audit of other documents

### The three INDEX files (what they say makes a view "read as real")

| INDEX entry | Claim | Reference opened | Verdict |
|---|---|---|---|
| gsv 174828 | wheel-track band visibly paler | gsv 174828 | C (tracks slightly darker) |
| gsv 180306 | paler, worn wheel-track bands | gsv 180306 | C (dark black tyre streaks) |
| gsv 175023 | worn pale central band between wheels | gsv 175023 | N (road is blurred by the car mask) |
| gsv 175143 | tarmac teardrop splitter, hatch bars, pale kerb line, grass centre, tarmac footway | gsv 175143 | S |
| gsv 175655 | dark patches, crack seals, cast cover, pier flaring into deck, autumn shrubs | gsv 175655 | S |
| gsv 175749 | stacked viaducts, verge brambles, palisade fence, flat-beam underpass | gsv 175749 | S |
| gsv 175937 | pale grey surface, dark wheel tracks, crisp edge line, cover in carriageway | gsv 175937 | S |
| gsv 180248 | washed pale patchy grey, diagonal crack seals, faded paint, low broken kerb | gsv 180248 | S |
| gsv 180326 | ladder-hatched kerbed splitter, dark tyre band into entry, ramped barrier end | gsv 180326 | S |
| gsv 180402 | pale hard shoulder, dark fine asphalt, weeds at barrier base | gsv 180402 | S |
| gsv 180456 | pale chevron gore, brick and corrugated walls | gsv 180456 | S |
| gsv 180653 | not catalogued at all (the header said "Six" for 11 entries; 12 files) | gsv 180653 | N, entry added. It shows a double-dashed give-way line, which contradicts the "single dashed" recurring fact unless scoped to the four junction photos. |
| aerial 01 | lighter polished wheel bands | aerial 01 | U (cool dark grey, only dark streaks) |
| aerial 05 | lighter tyre-polished lanes | aerial 05 | U |
| aerial 10 | lighter worn lane centres; kerbs thin pale lines | aerial 10 | U |
| aerial 20 | light grey-brown band, no markings | aerial 20 | C (dark grey to black; the upper road has a dashed centre line and edge lines) |
| aerial 09 | new asphalt dark, crisp markings, plain grass island | aerial 09 | S |
| aerial 34 | lighter lane centres and visible patches | aerial 34 | U for lane centres, S for a dark patch |
| aerial 40 | darker central wheel path; island shaggy grass with dark ring; pale kerb | aerial 40 | U for wheel path; C for pale kerb (dark to mid grey); island is a mown ring round a rock bed |
| aerial 44 | wet asphalt with lighter patched rectangles | aerial 44 | C (patches are darker) |
| aerial 27 | faint wheel-track darkening | aerial 27 | U |
| aerial 26 | tan-brown raw earthworks with rills | aerial 26 | S |
| aerial 28 | portal faces lighter concrete aprons | aerial 28 | C in part (pale olive-grey rings, openings lit orange); arches, gorse slopes, low walls S |
| street 01-05, 07, 10, 12, 13, 14 | walls, cuttings, embankment, kerb descriptions | same files | S, except street/05 "worn pale with polished tracks" (C: mid-dark and fairly clean) and its "about 1 m total" gabion height (C: about 1.5-2 m) |
| street 02, 21, 26, 28, 33, 35 | kerb about 100 mm and pale grey, red-toned tunnel surface, double-dashed give-way, pale kerbed grass island, paler brown-grey hard shoulder, pale central band between tracks | same files | S, except street/33 "brown-grey" shoulder (C: grey; brown is only the gravel strip) |
| street 22 | each patch a slightly different grey; wheel tracks polished smoother | street/22 | S for patches; U for "polished" (the patches look lighter grey with a wet sheen, not dark) |

Trustworthiness: the structural and engineering content (feature lists, marking layouts, geometry, vegetation and background character, wall and slope shapes) held in most of the entries checked. The tone claims are the weak part: every claim about wheel-track or lane-centre lightness was wrong or unverifiable, and the aerial INDEX tone remarks beyond the 11 entries checked are unverified. Treat the INDEX text as a pointer to which image to open, not as evidence.

### Other documents

| Document and line | Claim | Reference files opened | Verdict | Action |
|---|---|---|---|---|
| `docs/progress/2026-10-10-structures/REVIEW.md:9` | roadside retaining walls (street/04, 05) are 1-3 m with a slope above | street/04, 05 | S for 05 (about 1.5-2 m); partly C for 04 (concrete end block about 3-4 m, plus a 6-8 m netted rock cut) | Rewritten in place |
| same `:3, :18` | portals in 28 are arched with a thin lip, deep approach cutting with slope both sides; 26 approaches broad and gentle | aerial 26, 28 | S | none |
| same `:22` | street/12 shows a drainage ditch | street/12 | U (rough verge, no ditch visible) | Tagged |
| same `:22, :24` | street/10 toe ditch; street/13, 14 gentle 1:2-1:3 grass bank with fence | street/10, 13, 14 | S | none |
| `docs/progress/2026-10-10-structures-r3/REVIEW.md:3` | street/01 wall 2.5-3.5 m with coping and end; street/03 wall end steps into bank at 35-40 degrees. The review states it used INDEX text only. | street/01, 03 | S (wall about 3.5-4 m at the tall end; end about 40-45 degrees) | Note added that the images were not opened by the author, and that the check holds |
| `docs/progress/2026-10-11-unreal-second/REVIEW.md:15` | "The sky is pale, not deep saturated blue" | gsv 175937, 180326, street/33, 21, 04, aerial 26 | C for sunny photos (clear mid to deep blue); S for overcast | Rewritten |
| same `:16` | "Every street view has trees or hedges at the horizon, light columns..." | street/10, 35, 12, 13 | C (street/10 is moorland; rural lanes have no light columns) | Softened to "Most" |
| same `:6` | old surfaces paler, sRGB 70-110 | street/35, gsv 180248, 175937 | partly C: old minor roads read brighter than 110 | Note added |
| same `:6, :11-13, :76-78` | grey asphalt, darker wheel tracks, 5-15% paint wear, grey kerbs, no oil stains, patches 5-10% | gsv 174828, 180306, 180248, 175655, 180653; street/02, 07, 22, 26, 28 | S. Street/22 patches read lighter grey with a wet sheen, not dark and glossy; the brief's "dark glossy puddled patches" is only half right | none (noted) |
| same `:29` and review item 4 | islands: grass with a pale kerb, splitter is tarmac with ladder bars, not slabs | gsv 175143, 180326; street/28 | S | none |
| `docs/unreal-photorealism.md:51` | oil stains break the clean mesh | 39 images | U | Tagged |
| `docs/unreal-photorealism.md:74-76` | aerial perspective and haze make aerial views read large | aerial 01, 05, 09, 34 (hazy ones in the INDEX: 12, 13, 24, 25 not opened) | S | none |
| `docs/vegetation-and-surface-realism.md` (all) | technique and licence research; its only appearance claims are tagged [U] (line 44 overcast recipe) | none needed | N | none |
| `docs/road-marking-rules.md` | dimensions and rules from the Traffic Signs Manual; no appearance claims | n/a | N | none |
| `docs/HANDOFF.md:57` | "11 Street View shots" | gsv folder | C (12 files) | Fixed. Section 4/5 "very clean and sterile" is a quote of the person, not a photo claim |
| `docs/planning-log.md:235` (Round 32) | "grass island and paved splitters" | gsv 175143, 180326; street/28 | U, no photo shows paving slabs on a splitter; they are tarmac or grass | Tagged. Other realism lines (114, 233) set a bar and make no photo claims |
| `docs/progress/2026-10-11-unreal-second/INDEX.md:27`, `2026-10-10-unreal-first/INDEX.md:18` | describe our own renders (flat brown floor) | n/a | N | none |

## Edits made (all under docs/, none of the agent-owned files)

- `docs/reference/gsv/INDEX.md` (private, not for commit): count and header, wheel-track claims for 174828 and 180306 corrected, 175023 tagged, give-way "recurring fact" scoped, gap 8 updated, 180653 entry added.
- `docs/reference/aerial/INDEX.md` (private): entries 01, 05, 10, 20, 27, 28, 34, 40, 44 rewritten or tagged; audit note at the top.
- `docs/reference/street/INDEX.md` (private): entries 05, 22, 33 corrected or tagged; audit note at the top.
- `docs/progress/2026-10-10-structures/REVIEW.md`, `.../structures-r3/REVIEW.md`, `.../2026-10-11-unreal-second/REVIEW.md`: sentences corrected or tagged as listed above.
- `docs/unreal-photorealism.md:51`, `docs/planning-log.md:235`, `docs/HANDOFF.md:57`: tag or count fix.
- `CLAUDE.md`: one line appended to the working-style list. `docs/README.md`: "Reference claims (rule)" section and an index entry for this file.

## Hits in agent-owned files (not edited, need fixing)

1. `unreal/CityPainter/SourceArt/Textures/PolyHaven/MANIFEST.md:12` and `manifest.json:79`: `asphalt_01` is described as grey-brown with no strong colour cast; measured (89, 80, 70), strongly warm. Fix: desaturate in the material (about 60%) and shift to a cool grey (B 5-8% above R), or use `asphalt_02` (91, 90, 85) as the base; correct the text.
2. Same files `:57` and `:301`: `concrete_wall_009` "plain light grey" is warm (161, 154, 140); `concrete_pavement` (128, 117, 103) is warmer. Neutralise in `M_Kerb` and `M_Footway` (`mat_defs.py`, `SURFACE_SLOTS` at `setup_look_test_level.py:45`).
3. `unreal/tools/py/mat_defs.py:70, :80`: `DryTint` (1.75, 1.6, 1.2) and soil multiplied by 0.8 push `M_Ground` towards brown; the review wants green grass and a ragged gravel fringe, not brown ground. When the asphalt master is added, make sure R-wear is a darkening mask on the wheel bands (not a lightening one).
4. `data/regions/uk/marking_rules.json:8` `strength 0.3`, `wheel_boost 0.35` (testland 0.45): mean paint wear about 30% against 5-15% seen on main roads. Suggested uk values: strength about 0.10, with a per-road-type override up to 0.4 for old minor roads. The fallbacks in `sim_core/src/markings.cpp:851, :854` and `sim_core/include/cp/markings.h:322, :325` (0.3, 0.35) must change in the same step.
5. `data/regions/uk/region.json:69` `splitter_surface: "paving"` (also `apron_surface`), with the same defaults in `sim_core/include/cp/region_pack.h:133, :141`; `render_mesh.cpp:1414` renders it, and `setup_look_test_level.py:364` maps `paving` to `MI_Footway`. Photos (gsv 175143, 180326) show splitters as dark tarmac with a kerb, or grass (street/28). Suggested value: `asphalt` (or a darker asphalt variant).
6. `unreal/tools/py/setup_look_test_level.py:47`: paint (0.90, 0.90, 0.88); the corrected review gives about 0.75.
7. `sim_core/include/cp/render_mesh.h:36, :96` `wheel_path_offset 0.9` against `marking_rules.json` `wheel_offset_m 0.8`: inconsistent, and neither value is checkable from our photos (unverified).

## Reference files opened for this audit (39)

GSV (12), folder `docs/reference/gsv/`, "Screenshot 2026-10-10 HHMMSS.png": 174828, 175023, 175143, 175655, 175749, 175937, 180248, 180306, 180326, 180402, 180456, 180653.

Aerial (11), `docs/reference/aerial/`: 01_m25_heathrow, 05_m1_j10, 09_m181_roundabout, 10_a16_a158_partney, 20_beverley_westwood, 26_a9_gelly_overbridge, 27_m7_naas, 28_a3_hindhead_portals, 34_autoroute973, 40_hafnarfjordur_roundabout, 44_iskontie_oulu.

Street (16), `docs/reference/street/`: 01, 02, 03, 04, 05, 07, 10, 12, 13, 14, 21, 22, 26, 28, 33, 35.

Not opened (so their INDEX text stays unverified): aerial 02-04, 06-08, 11-19, 21-25, 29-33, 35-39, 41-43, 45; street 06, 08, 09, 11, 15-20, 23-25, 27, 29-32, 34, 36, 37; gsv `compare/`. Not repo references: mean colour of `asphalt_01`, `asphalt_02`, `concrete_wall_009` and `concrete_pavement` diffuse maps was measured on a 32x32 sample grid.
