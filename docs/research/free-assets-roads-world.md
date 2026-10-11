# Free assets for the road world (research, 11 Oct 2026)

Status: research note, untracked. Downloads are under `unreal/CityPainter/SourceArt/Free/` (untracked, LFS tree, nothing was `git add`ed).

## 0. What could not be verified or got, and categories with no good free option

Read this first.

- **Network was slow (about 0.5 MB/s)**, so the download set was cut: 4K only for the asphalt and a few rock/grass sets, 2K for the rest, no 8K asphalt (ambientCG Road012A 8K JPG is 601 MB: listed as an owner/overnight option, section 6). Anything marked "queued, not fetched" in section 3 was not downloaded.
- **Fab / Quixel Megascans, Epic sample projects (City Sample, Electric Dreams etc.), Sketchfab downloads: all need an Epic or Sketchfab login. I did not sign in, so none were fetched and none are verified.** Fab pages return HTTP 403 to my fetcher, so current Fab listing prices and "free" status are unverified. What I could confirm from Epic's forum post (updated 28 Nov 2024): legacy Megascans claims closed on 31 Dec 2024, claimed assets stay under the Epic Content License, and from 2025 Megascans are sold on Fab with "some assets for free". Current (Oct 2026) Fab free status: not verified.
- **Sketchfab**: its public search API returned nothing usable for anonymous queries, so I could not list CC0 road furniture there. Sketchfab downloads need a login anyway. Unverified.
- **cgbookcase / 3dtextures.me / Texture Ninja**: cgbookcase's front page states "100% free, no restrictions" but gives no licence name; 3dtextures.me licence page returned 404 to me; Texture Ninja not checked. None used. **ShareTextures forbids automated downloads** ("automated downloads, hotlinking, or embedding direct downloads in third-party apps") and redistribution; I did not use it. Owner can hand-pick from it (no account needed).
- **Unreal facts**: checked in `C:\UE_5.8` headers (section 7). Anything about editor UI beyond that is not verified.
- **Previews**: I judged most candidates from the sources' own 256 px preview spheres, and the main downloaded asphalt sets from the files themselves (section 4). I did not render any asset in Unreal.

**Categories with no good free option (must be modelled by us from the photos, or bought):**
1. **UK lighting columns** (tapering steel/concrete outreach columns, photo 32: tall straight round pole, short curved outreach arm, flat-glass cobra-head). Free options found are decorative Victorian lamps (Poly Haven `street_lamp_01/02`) or non-UK low-poly lamps. Model ourselves: one tapered pole + arm is a simple lathe/extrude. Our Blender kit already has `Kit/Lighting`.
2. **Armco / W-beam safety barrier and tensioned-beam central reservation barrier** (photo 31). No free photoreal one found. Profile is a 2-3 m long extruded W section on C posts; model from the photo (we already have `Kit/Barriers`). Concrete barrier blocks exist free (Poly Haven `concrete_road_barrier*`) but are not the UK step profile.
3. **UK signs and posts** (blue/green/white sign faces, reflective sheeting, backs, clips). Sign FACES are free as Crown copyright images (section 3.9); sign plate/post/hardware geometry must be made by us.
4. **Traffic signals (UK, black pole + signal heads + push-button units), Belisha beacons, bollards, marker posts (photo 31: black/white post with amber reflector), pedestrian guard-rail (photo 29), kerb profiles, drainage gullies (full geometry), UK pillar boxes, telegraph poles.** Nothing free and UK-correct found. Poly Haven `modular_electricity_poles` (200k tris, rural poles with crossarms) is the nearest thing to a telegraph pole.
5. **Road-marking paint worn/aged decals beyond ambientCG RoadLines**: ambientCG RoadLines006-011 are photoscanned lines but US-style (yellow double line/white dashes in plan view, strips on a transparent sheet), not UK TSRGD markings (UK double yellow at kerb, broken white with 6 m / 3.6 m patterns, etc.). Use them as WEAR/texture source for our own procedural markings, not as the markings.
6. **Dry-stone walls with real UK stone (photos 02, 03, 08, 22)**: free tiling textures exist (rock_wall_08, rustic_stone_wall, old_stone_wall) but none is a real limestone/gritstone drystone with through stones and a coping/cam. Texture-only: okay at 5-10 m, will look repetitive when close. A modelled wall with scanned stone texture is the realistic route.
7. **Gabion baskets (photo 05)**: no free photoscan. Our kit mesh with a rock texture (ambientCG Rock030/Rock028 look close) plus a wire-mesh alpha is the route.
8. **Photoreal road vehicles**: Poly Haven has only `covered_car` (a car under a cover). No free good cars found. Not pursued further (low priority; ask owner whether a Fab free car is acceptable: login needed).
9. **Photoreal hedge/tree/grass**: another agent covers vegetation. Not touched.
10. **Rural buildings**: nothing free and excellent found; not pursued.

## 1. The look to hit (references opened)

Opened (all in `C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\`): `22_surface-patched-lane-wet.jpg`, `23_surface-potholed-residential-street.jpg`, `32_dual-street-lights-verge-kerbed-channel.jpg`, `31_dual-central-reservation-grass-barrier.jpg`, `35_rural-lane-hedges-sun-cloud.jpg`, `36_rural-b-road-low-sun-winter.jpg`, `07_kerb-granite-channel-stone-wall-corner.jpg`, `05_gabion-wall-bank-hazard-sign.jpg`, `10_cutting-gneiss-rock-face-toe-ditch.jpg`, `29_junction-crossroads-box-signal.jpg` (10 of 10 listed for this task). Current game pictures opened: `roadworld-wt-m2\docs\progress\2026-10-11-unreal-m2-run\renders\20_st_s27c_finished_low_oblique.jpg` and `20_st_s31_road_on_slope_low.jpg` (2 of the 3-4 asked; the others in that folder are UI states).

What the photos show, in plain terms:
- Road surface is **fine-grained, neutral to slightly cool dark grey tarmac** with visible aggregate; wear is long-wavelength (patches, wheel-track polish, cracks, pothole edges, bitumen seals), not noise. Photo 22: whole lane of patch rectangles; photo 23: grit and pothole clusters; photos 31/32: clean grey with a crisp white edge line and a concrete channel/edge strip.
- Kerbs: pale granite/concrete 125 mm face with a channel; photo 07 granite setts and dressed stone; photo 32 a concrete combined kerb-drain (castellated edge).
- Verges: grass that is long, mixed and clumpy, with a bare-soil/chippings strip where the surface ends (photos 35, 22).
- Walls: pale coursed rubble with deep pointing (07), dark layered drystone (22), gabions (05).
- Rock: stratified, angular, grey/brown, vertical (10, plus 38-40 chalk and 09 sandstone).
- Sky: broken cumulus on blue (35), flat layered overcast (36), pale overcast (31).
- Current game pictures: the ground is a **single repeating green tile** with no variation, the road is a flat grey ribbon with no shoulder, no edge wear, no verge, a blank horizon. The biggest fixes by value: (1) ground/verge material variety and de-tiling, (2) asphalt wear and edge detail, (3) kerbs/furniture, (4) sky.

Scale note: Poly Haven's API `dimensions` give the real-world size of each texture (mm); ambientCG gives no size field for materials in its API (dimensionX/Y = 0), so the tiling length is unknown: calibrate by aggregate size against the photo.

## 2. Sources checked (licence quoted, account, access)

| Source | Licence (quoted) | Commercial game use | Account | How I accessed it | Verdict |
|---|---|---|---|---|---|
| Poly Haven (https://polyhaven.com/license) | "Our assets are all licensed as CC0, which is effectively Public Domain"; "You can use our assets for any purpose, including commercial work."; credit "not required" | yes | no | public API `api.polyhaven.com` with a distinct User-Agent. Its site says the API is free for non-commercial use, and commercial integration of the API needs a licence (the assets themselves are CC0 regardless). Fine for our personal use. | Best source for models + HDRIs + 4K-8K textures. |
| ambientCG (https://docs.ambientcg.com/license/) | "All ambientCG assets are provided under the Creative Commons CC0 1.0 Universal License."; "You can copy, modify, distribute and perform the assets, even for commercial purposes, all without asking permission."; "You don't need to give credit" | yes | no | `ambientcg.com/api/v2/full_json` + `ambientcg.com/get?file=...` | Best source for photoscanned asphalt, road patches, rock, ground, grates, road-line decals. |
| GOV.UK traffic sign images (https://www.gov.uk/traffic-sign-images) | "You may reproduce traffic-sign images free of charge and without having to seek permission."; "you must reproduce them accurately and not in a misleading context."; "You should also include a statement that these images are Crown copyright." (fuller: Open Government Licence) | yes, with Crown copyright credit, accurate, not misleading | no | direct zip links on the page | Sign FACES only (JPG small, EPS vector). |
| ShareTextures (https://www.sharetextures.com/p/license) | "Free to use for personal, educational, or commercial projects."; "CC0 only applies to assets downloaded directly from our site."; bans "automated downloads, hotlinking, or embedding direct downloads in third-party apps"; "No asset redistribution ... in collections without our written permission." | yes if you download by hand | no | not downloaded (automation banned) | Owner can browse by hand if wanted. |
| cgbookcase (https://www.cgbookcase.com/) | site says "100% free, no restrictions"; no licence name shown on the page I could read (secondary sources say CC0) | probably | no | not downloaded | Unverified; skipped. |
| 3dtextures.me | licence page 404 to me | unknown | unknown | skipped | Unverified. |
| Fab (fab.com) incl. Megascans | Fab Standard License (per Epic forum post); legacy Megascans: Epic Content License; page text not readable by me (403) | likely, "any engine", not resellable | **Epic account** | not accessed | Owner list (section 6). |
| Epic sample projects (City Sample on Fab, Electric Dreams, Hillside...) | City Sample listing: "UE-Only Content - Licensed for Use Only with Unreal Engine-based Products" (search snippet). Others not verified. | Unreal-engine-only use | **Epic account** | not accessed | Owner list. |
| Sketchfab | per model (CC0 / CC-BY etc.) | per model | **login to download** | anonymous search returned nothing useful | Owner list; check licence per model. |
| Content on disk in `C:\UE_5.8` | Epic EULA (engine content) | in Unreal products | n/a | listed folders | Section 5. |

## 3. Candidate catalogue, ranked by closeness to the photos

Ranking rule: photoscanned > procedural > hand-painted. "Seen" = I looked at the source's own preview; "File" = I opened the downloaded file.

### 3.1 Road asphalt (new/clean) - photos 31, 32, 36, 35
1. **ambientCG Road012A / Road014A / Asphalt024A** (CC0; photoscan; neutral-to-cool dark grey, fine aggregate, no baked markings). Seen: Road012A is a clean worn grey with soft wheel-track mottling, which is exactly the photo 31/32 surface. Road014A is darker.
2. **ambientCG Asphalt025C** (darker, tight grain, smooth).
3. **ambientCG Asphalt032 / Asphalt023S / Asphalt020L** (lighter, coarse chipping; **this is the surface-dressed / chippings look of photos 35 and 23**, use for B roads and lanes).
4. Poly Haven asphalt_01 / asphalt_02 (already in repo, 2K): asphalt_01 measured as warm brown by earlier notes (`Textures/PolyHaven/MANIFEST.md`), worse than the ambientCG set for UK grey.
- Avoid: Road006/Road007/Road008A/Road010A/Road011A/Road003/Road001/Road005 have lane lines baked in (US-style); useful only as wear references. Asphalt029B is red (cycle-lane red, maybe useful). Asphalt031/030 are almost white concrete-like.

### 3.2 Worn, patched, cracked asphalt - photos 22, 23
1. **ambientCG Road009C, Road015C** (CC0; photoscan; patch rectangles + broken seams, patch layout comparable to photo 22, but see 4.1 for the baked yellow line). **Asphalt019, Asphalt016** (large bitumen patch and peeling surface). **Road001 / Road003 / Road005** (small square patches in a road, with lines baked in: use as patch reference or mask source).
2. **Asphalt026C, Asphalt027C, Asphalt021, Asphalt022** (crack networks: crazing / alligator cracking, good for old lanes and for crack decals once the cracks are extracted as a mask).
- No free photoscanned **pothole** decals found; potholes are best done with Nanite displacement (heightmaps from ambientCG) or by our own mesh decals.

### 3.3 Road-marking paint (decals)
- **ambientCG RoadLines006-RoadLines011** (CC0): photoscanned worn markings, plan-view lines on a transparent sheet (colour + opacity + normal + roughness + displacement). Yellow: 006, 008, 011; white: 007, 009, 010. **Not UK patterns**, so use as paint WEAR/thermoplastic bump textures to drive our own marking material; confirmed from the preview sheet.
- No UK-pattern free decal set found (TSRGD markings: modelled by us procedurally, which `docs/road-marking-rules.md` already needs).

### 3.4 Kerbs, footways, drainage - photos 07, 23, 32, 29
- **Kerb geometry**: none free. Our `Kit/Kerbs` stays; apply a concrete/granite material (see concrete below).
- **Footway tarmac**: the same asphalt sets (use Road014A/Asphalt025C at a different tile size, photo 23 shows plain black tarmac pavement).
- **Block paving**: **ambientCG PavingStones085, 087, 088, 089** (CC0; red/charcoal clay block paving: photo 29 and 23 pavement edge). Seen only.
- **Tactile paving**: **ambientCG TactilePaving002** (blister, cream) and TactilePaving004 (corduroy). Seen only.
- **Drain grates and gullies**: **ambientCG Grate001** (square gully, set in concrete; photoscan, checker background in the sphere preview, the maps are the cover itself) and **Grate002** (rectangular cast-iron grating, plan view with real mess, an opacity/cut-out map as a decal). Poly Haven **water_manhole_cover** (6,301 tris, 4K, round cast "WATER" cover; UK covers say e.g. "SEVERN TRENT", so retexture the lettering).

### 3.5 Concrete (retaining walls, bridge parapets, abutments, channel strips) - photos 15-19, 31
- **ambientCG Concrete042A, Concrete031, Concrete033, Concrete036, Concrete024, Concrete017** (CC0; seen). 042A is grey weathered precast, 031/033 form-ply board-marked dark grey, 036 stained weathered, 024/017 light smooth. Poly Haven concrete_wall_009 and concrete_pavement are already in the repo.

### 3.6 Walls - photos 01, 02, 03, 05, 08
- **Poly Haven rock_wall_08** (dark coursed gritstone setts, fairly close to the dark drystone/slate of photo 22), **rustic_stone_wall**, **rustic_stone_wall_02** (pale coursed limestone; closest to photo 07 and 02), **stone_wall** (light random rubble: photo 08 flint-ish), **mossy_stone_wall** (dark moss-covered, photo 22 right wall). All 4K diff/normal(dx)/rough/AO/disp where downloaded (2K fallback noted in the table).
- **Poly Haven brick_wall_005 / brick_wall_02**: red brick, UK-plausible (photo 23's garden wall is yellow-buff brick: none found in buff; `brick_wall_003`/`brick_wall_006` are orange: queued).
- Dry-stone and gabion: see section 0 (no good free option).

### 3.7 Rock faces, cuttings - photos 04, 09, 10, 11, 38-40
- **ambientCG Rock030, Rock028, Rock031, Rock037, Rock048, Rock022 (dark layered), Rock023 (pale cracked limestone/chalk-grey), Rock051 (layered sandstone), Rock026 and Rock018 (white chalk-like)** (CC0; photoscan). Rock030/028 are the nearest to the grey-brown cutting faces of photos 09/10; Rock023/026/018 for chalk (38, 39).
- **Poly Haven cliff_side** (orange layered sandstone, too red), **rock_face_03**.
- **Poly Haven meshes** (photoscanned, FBX with 4K or 2K textures): rock_face_01 (20k tris), rock_face_02 (29.6k), boulder_01 (124k), mountainside (288k, cliff slab), rock_07 (27.8k), rock_09 (23k). For hero cutting faces with Nanite these are usable; at 288k tris `mountainside` is Nanite-only.

### 3.8 Ground, verge, soil, gravel - photos 35, 36, 22, 23
- **Grass**: ambientCG Grass004, Grass005, Grass007 (short mown turf, photoscan). Photo 31's central reservation grass is long and clumpy: *no free texture does this well*; it needs vegetation geometry (other agent) over a soil base. Grass001-003 are dark cut lawn.
- **Soil / mud**: Ground037 (grassy mud/moss: the ground in photo 22/36 verges), Ground003 (mossy dirt), Ground085, Ground067 (brown soil), Ground081, Ground110 (dark muddy gravel with leaf litter).
- **Gravel / chippings**: Gravel041, Gravel040 (grey crushed stone, hardcore verge/ shoulders), Ground062S (pale chippings).
- Poly Haven brown_mud_rocks_01, forrest_ground_01, grey_stone_path.
- The M2 pictures show the existing grass as one repeating tile; the fix is (a) 3-4 of these blended with a world-aligned noise mask and a macro-variation texture, and (b) distance-based tiling breakup, not just a better texture.

### 3.9 Signs (faces) - photos 05, 29, 32, 31
- **GOV.UK DfT traffic sign images (JPG zips, 12 categories; EPS zips for warning, regulatory, speed, direction, motorway kept unopened)**: quoted terms above. JPGs are "not at any size" (limited resolution): for close-up we should use the EPS or rebuild glyphs in vector; I did NOT open the EPS files. Direction signs need our own text (the DfT set has the pictograms and frame; UK direction signs use the Transport font, which I did not research).
- Wikimedia Commons has per-sign SVG versions derived from DfT working drawings (OGL v1.0, some v3.0; check each page). I did not download any.

### 3.10 Roadside furniture models (Poly Haven, CC0, all downloaded as FBX + textures)
| Asset | Tris | Use here | Close view? |
|---|---|---|---|
| water_manhole_cover | 6.3k | manhole cover (retexture) | yes after retexture |
| utility_box_01 / _02 | 4.4k / 6.3k | grey/green street cabinets (photo 23 has a green cabinet at left) | yes (photo 23: green cabinet matches utility_box_02 closely) |
| modular_chainlink_fence | 89k | rural/urban fencing | far/mid only, 89k is heavy; photo 29's guard rail is NOT this |
| modular_electricity_poles | 200k | rural wooden pole + insulators (photo 22 has a pole) | mid view, kit-of-parts, heavy |
| concrete_road_barrier / _02 | 80.8k / 42.6k | concrete blocks (roadwork barrier) | heavy, not UK step barrier |
| street_lamp_01 / _02 | 30.6k / 20.3k | **decorative** lamps: wrong for highway | low value for roads |
| large_iron_gate | 38k | field/house gates | okay mid |
- Everything else in Poly Haven's model list (the ~521 assets) is indoor, props, tools or plants; I found no other road furniture except `fire_hydrant` (US style), `old_tyre`, `modular_street_seating`, `metal_trash_can`.

### 3.11 Sky and light
- **Poly Haven HDRIs** (CC0, 4K or 2K HDR): chosen by preview. UK overcast: `farmland_overcast` (flat layered overcast over fields, closest to photo 36 and 31), `kloppenheim_07_puresky`, `mud_road_puresky`, `kloofendal_overcast_puresky` (sky-only, no ground, for a sky dome over our own terrain); broken cumulus (photo 35): `farm_field_puresky`, `cloud_layers`; clear blue: `kloofendal_43d_clear_puresky`. Road-level HDRIs with rural roads: `rural_asphalt_road`, `small_rural_road`, `misty_farm_road` (useful as reflection/lighting probes, not as a visible backdrop: the ground in them is not UK).
- **Engine content**: `Engine/Content/EngineSky/` has SkyAtmosphere material, VolumetricClouds noise + `m_SimpleVolumetricCloud(_Inst)` presets, `T_Sky_Clouds_M`, `BP_Sky_Sphere`. Use SkyAtmosphere + VolumetricCloud + SkyLight as the main sky (dynamic time-of-day) and the puresky HDRIs only as a fallback or for lookdev. HDRI-only sky is static: it cannot move the sun.

### 3.12 Vehicles, rural buildings
- Nothing free that is good enough found (section 0). Not downloaded.

## 4. What was downloaded (all untracked; each folder has a `LICENCE.txt` with URL, licence, date)

Root: `unreal/CityPainter/SourceArt/Free/<category>/<source>/<asset>/` in the main checkout. Licence: CC0 for every ambientCG and Poly Haven item; the sign images are Crown copyright (see `signs/GOVUK_DfT/LICENCE.txt`).

| Folder | Size | Files | Resolution |
|---|---|---|---|
| concrete/ambientCG/Concrete031 | 28 MB | 7 | 2K |
| concrete/ambientCG/Concrete036 | 24 MB | 6 | 2K |
| concrete/ambientCG/Concrete042A | 25 MB | 8 | 2K |
| decals/ambientCG/Grate001 | 23 MB | 9 | 2K |
| decals/ambientCG/Grate002 | 14 MB | 9 | 2K |
| decals/ambientCG/RoadLines006 | 2 MB | 7 | 2K |
| decals/ambientCG/RoadLines007 | 2 MB | 7 | 2K |
| decals/ambientCG/RoadLines008 | 3 MB | 7 | 2K |
| decals/ambientCG/RoadLines009 | 3 MB | 7 | 2K |
| footway/ambientCG/PavingStones087 | 30 MB | 7 | 2K |
| footway/ambientCG/TactilePaving002 | 15 MB | 6 | 2K |
| furniture/PolyHaven/large_iron_gate | 42 MB | 2 | 4k |
| furniture/PolyHaven/utility_box_01 | 52 MB | 2 | 4k |
| furniture/PolyHaven/utility_box_02 | 51 MB | 2 | 4k |
| furniture/PolyHaven/water_manhole_cover | 57 MB | 2 | 4k |
| ground/ambientCG/Grass004 | 39 MB | 7 | 2K |
| ground/ambientCG/Grass005 | 38 MB | 7 | 2K |
| ground/ambientCG/Grass007 | 35 MB | 7 | 2K |
| ground/ambientCG/Gravel040 | 33 MB | 7 | 2K |
| ground/ambientCG/Gravel041 | 31 MB | 7 | 2K |
| ground/ambientCG/Ground003 | 32 MB | 6 | 2K |
| ground/ambientCG/Ground037 | 37 MB | 7 | 2K |
| ground/ambientCG/Ground062S | 34 MB | 7 | 2K |
| ground/ambientCG/Ground067 | 35 MB | 7 | 2K |
| ground/ambientCG/Ground085 | 36 MB | 7 | 2K |
| rock/PolyHaven/cliff_side | 13 MB | 6 | 2k |
| rock/PolyHaven/rock_face_01 | 48 MB | 2 | 4k |
| rock/PolyHaven/rock_face_02 | 49 MB | 2 | 4k |
| rock/ambientCG/Rock018 | 25 MB | 7 | 2K |
| rock/ambientCG/Rock022 | 25 MB | 7 | 2K |
| rock/ambientCG/Rock023 | 25 MB | 7 | 2K |
| rock/ambientCG/Rock026 | 22 MB | 7 | 2K |
| rock/ambientCG/Rock028 | 31 MB | 7 | 2K |
| rock/ambientCG/Rock030 | 32 MB | 7 | 2K |
| rock/ambientCG/Rock037 | 32 MB | 7 | 2K |
| rock/ambientCG/Rock051 | 29 MB | 8 | 2K |
| sky/PolyHaven/cloud_layers | 28 MB | 1 | 4k |
| sky/PolyHaven/farm_field_puresky | 19 MB | 1 | 4k |
| sky/PolyHaven/farmland_overcast | 26 MB | 1 | 4k |
| sky/PolyHaven/kloofendal_overcast_puresky | 17 MB | 1 | 4k |
| sky/PolyHaven/kloppenheim_07_puresky | 19 MB | 1 | 4k |
| sky/PolyHaven/misty_farm_road | 25 MB | 1 | 4k |
| sky/PolyHaven/mud_road_puresky | 16 MB | 1 | 4k |
| surfaces/asphalt/ambientCG/Asphalt024A | 34 MB | 7 | 2K |
| surfaces/asphalt/ambientCG/Asphalt025C | 128 MB | 7 | 4K |
| surfaces/asphalt/ambientCG/Asphalt032 | 28 MB | 6 | 2K |
| surfaces/asphalt/ambientCG/Road012A | 138 MB | 7 | 4K |
| surfaces/asphalt/ambientCG/Road014A | 137 MB | 7 | 4K |
| surfaces/asphalt_worn_patched/ambientCG/Asphalt016 | 29 MB | 6 | 2K |
| surfaces/asphalt_worn_patched/ambientCG/Asphalt019 | 29 MB | 6 | 2K |
| surfaces/asphalt_worn_patched/ambientCG/Asphalt027C | 31 MB | 7 | 2K |
| surfaces/asphalt_worn_patched/ambientCG/Road001 | 27 MB | 6 | 2K |
| surfaces/asphalt_worn_patched/ambientCG/Road009C | 120 MB | 7 | 4K |
| surfaces/asphalt_worn_patched/ambientCG/Road015C | 30 MB | 7 | 2K |
| walls/PolyHaven/brick_wall_005 | 52 MB | 9 | 2k/4k |
| walls/PolyHaven/mossy_stone_wall | 64 MB | 6 | 4k |
| walls/PolyHaven/rock_wall_08 | 57 MB | 6 | 4k |
| walls/PolyHaven/rustic_stone_wall | 66 MB | 6 | 4k |
| walls/PolyHaven/rustic_stone_wall_02 | 55 MB | 6 | 4k |
| walls/PolyHaven/stone_wall | 48 MB | 6 | 4k |
| signs/GOVUK_DfT | 32 MB | 12 JPG folders (about 450 px each) + 5 EPS zips (unopened) | - |

Total on disk: **2.31 GB** (limit 15 GB). Nothing was over 2 GB. The EPS archives were not opened or run; no executables, installers or scripts were extracted (ambientCG zips also contain `.blend`, `.mtlx` and `.tres` files: not extracted, images only). Poly Haven models come as FBX + textures (FBX not opened).

The fetch script (resumable, skips what exists) is in `docs/research/free-assets-fetch/` (`dl.mjs` + `job5.json`; run `node dl.mjs job5.json`; add lines to the job for more).

**Queued but NOT fetched (bandwidth ran at 0.3-1 MB/s):** ambientCG Road003, Road005, Asphalt021, Asphalt022, Asphalt026C, Asphalt023S, Asphalt020L, RoadLines010, RoadLines011, PavingStones085/088/089, Rock031, Rock048, Rock034, Ground081, Ground110, Concrete024, Concrete033, Concrete017, Road012A 8K; Poly Haven rock_face_03, brown_mud_rocks_01, forrest_ground_01, grey_stone_path, brick_wall_02, meshes boulder_01, mountainside, rock_07, rock_09, modular_chainlink_fence, modular_electricity_poles, concrete_road_barrier(_02), street_lamp_01/02, and HDRIs kloofendal_43d_clear_puresky, rural_asphalt_road, small_rural_road, kloppenheim_04. Section 3 still describes them from their previews.

### 4.1 Quality check against named photos (files opened)

I opened the downloaded maps and set crops beside photo crops (composites live in my scratch folder, not committed):
- **Road014A (4K), Asphalt025C (4K) vs photos 32 and 31**: very dark, fine, almost no visible aggregate, soft mottling. Photos 32/31 read mid-grey with fine grit. Right kind of surface for mid/far view, but albedo must be lifted and a grit/normal detail layer added for near view. Not close-view ready alone.
- **Road012A (4K)**: very uniform dark grey with a slight green cast, nearly flat at tile scale. Good base for far/mid view after desaturating. No cracks or wear: needs wear layers.
- **Asphalt032 (2K) vs photos 35 and 23**: coarse aggregate clearly visible, brown-grey: the surface-dressed chippings look. Best close-up asphalt in the set.
- **Road009C (4K) vs photo 22**: full of rectangular repair patches and seams, **but with a baked yellow centre line (US style)**. Patch structure matches photo 22 well; the line must be masked or painted out before use. **Asphalt019 (2K)**: peeling surface strips, like the broken edge in photo 22. Good for edge wear.
- **rock_wall_08, rustic_stone_wall, mossy_stone_wall (4K) vs photos 22 and 07**: rock_wall_08 is neat dark coursed stone, tidier than photo 22's thin flat-layered drystone; rustic_stone_wall is warm orange sandstone, too saturated against photo 07 (pale grey-cream); mossy_stone_wall is too moss-heavy. Usable as far/mid wall fills, **not close-view matches**. rustic_stone_wall_02 not compared.
- **Rock030 (2K) vs photo 10**: dark flat mica-speckled grey; does not show the faceted pink-grey face of photo 10 (that faceting is geometry). **Rock023 (2K) vs photo 39**: layered grey limestone, plausible for chalk/limestone bedding after a lighter tint; not a match for photo 10.
- **Grass007 and Ground037 (2K) vs photos 22 and 35**: Grass007 is a bright saturated game green, too cartoon-like next to photo 35; Ground037 is yellow-green moss with sticks, too yellow against the dark olive, clumpy verges of photos 22 and 36. **These are weak**, which matches the owner's complaint. Verge needs desaturated blends plus vegetation geometry (other agent).
- **GOV.UK sign JPG (warning-signs-jpg/501.jpg)**: clean, but only about 450 px wide; use EPS (converted outside Unreal) for large signs.
- **Not opened or compared**: all HDRIs, FBX models, RoadLines, Grate, PavingStones, TactilePaving and Concrete (previews only, no render in Unreal).

## 5. Free content already on disk in `C:\UE_5.8` (searched)

- `Engine/Content/StarterContent/`: only `Textures/T_ground_Moss_D.uasset` (the starter pack is not installed). Nothing useful.
- `Engine/Content/EngineSky/` (67 MB): `SkyAtmosphere/SkyAtmosphere_MaterialSkyDome`, `VolumetricClouds/` (noise volumes `T_VolumeNoiseShape128`, `T_Volume_PerlinWorley_Balanced`, `m_SimpleVolumetricCloud` + `_Inst`, weather/gradient textures), `T_Sky_Clouds_M`, `BP_Sky_Sphere`. Use these for the dynamic sky (this is the main answer for sky; no download needed).
- `Engine/Content/OpenWorldTemplate/LandscapeMaterial/`: only a procedural grid material (184 KB). Useless for look.
- `Engine/Plugins/Runtime/HDRIBackdrop/` is installed: an HDRI Backdrop actor that projects a cubemap on a ground plane, good for lookdev with the HDRIs in `Free/sky`.
- `Engine/Plugins/Experimental/PCGBiomeSample/` exists (a PCG sample); I did not open it (not road content).
- `Templates/` and `FeaturePacks/` hold only gameplay templates (`TP_*`), no environment art.

## 6. What needs the owner (accounts), ranked by how much it would improve the result

None of these were fetched. Each needs a login that I must not create or use.

1. **Fab: Megascans free subset and any free road/urban kit** (largest gain for photoreal *close-ups* of tarmac, kerbs, stone walls, cliffs, grass and gravel; Megascans are photoscans at 4K-8K with Nanite meshes). Click path: https://www.fab.com -> sign in with the Epic account -> filter "Price: Free" and "Megascans" (or search "Megascans asphalt", "Megascans road", "Megascans stone wall", "Megascans cliff") -> "Add to My Library". Licence on the listing page: check it reads Fab Standard License or Epic Content License before relying on it; Epic's forum post (28 Nov 2024) says claimed legacy assets use the Epic Content License. In the Epic Games Launcher: Library -> Fab Library -> Add to project. The listing text I could find: "Road Essentials - Modular Roads, Bridges & Guardrails" (guardrails, concrete barriers, signs) at https://www.fab.com/listings/597854da-9725-4871-905e-a9ec04bdd15f (price/free status UNVERIFIED: please check; it is the nearest thing to Armco I found).
2. **Fab: City Sample content packs** (Epic, free sample; roads, kerbs, street furniture, signs, lamps, barriers, from a US-style city). Search "City Sample" on Fab. The listing says some packs are "UE-Only Content - Licensed for Use Only with Unreal Engine-based Products": fine for us, but not UK. Use for kerb/lamp/barrier *shapes only*. Large download (tens of GB): needs the owner to confirm disk space (C: has about 48 GB free at the time of writing).
3. **Fab free monthly/biweekly offers** (Epic gives away packs on a rotating basis; check https://www.fab.com -> "Limited-Time Free"). Cannot be planned in advance.
4. **Sketchfab downloads (CC0 / CC-BY only)**: https://sketchfab.com/search?features=downloadable&licenses=7c23a1ba438d4306920229c12afcb5f9&q=<term> (the licence filter in the sidebar, "CC0"); sign in, "Download 3D model" -> glTF/FBX. Terms worth searching: "UK street light", "belisha beacon", "UK traffic light", "armco barrier", "gabion", "dry stone wall". Check each model's licence line and triangle count; CC-BY needs a credit line. I could not list results.
5. **8K asphalt (optional)**: https://ambientcg.com/view?id=Road012A -> choose "8K-JPG" (601 MB). No account. I skipped it for bandwidth.
6. **ShareTextures** (https://www.sharetextures.com, no account, CC0 "only if downloaded directly from the site"): browse by hand; I did not use it because it bans automated downloads.
7. **Wikimedia Commons UK sign SVGs** (OGL): optional vector signs, one by one: https://commons.wikimedia.org/wiki/Category:SVG_traffic_signs_of_the_United_Kingdom (unverified category name).

## 7. How each kind gets into Unreal 5.8 (facts checked in `C:\UE_5.8` source)

- **Normal maps**: ambientCG ships `NormalDX` (DirectX, green down = Unreal's convention) and `NormalGL`. Import `*_NormalDX*` only. Poly Haven files are named `nor_dx` (DirectX). If a normal map looks inverted, `Texture.h` has `bFlipGreenChannel` (line 1391: "When true the texture's green channel will be inverted"). Compression `TC_Normalmap` ("Normalmap (BC5)", `TextureDefines.h`:394), sRGB off.
- **Packed maps**: Poly Haven `arm` = AO(R) Rough(G) Metal(B): import as `TC_Masks` ("Masks (no sRGB)", `TextureDefines.h`:395). ambientCG gives separate `Roughness`, `AmbientOcclusion` (not in all sets), `Displacement`, `Opacity` (decals): import each with sRGB off (grey maps: `TC_Masks` or Grayscale), colour with sRGB on.
- **Virtual textures**: `Texture.h` line 1611 `VirtualTextureStreaming`. 4K tiling surfaces used over large landscape areas (road, verge) are good VT candidates (Runtime Virtual Textures blend the road onto the landscape); do not use VT for small repeating tiles you sample many times. VT must also be enabled in project settings (not checked here; read `r.VirtualTextures` under Project Settings > Rendering).
- **Tiling size** (world size per texture tile): Poly Haven API `dimensions` (mm) give the true size, e.g. asphalt_01 2.08 m. ambientCG gives none; calibrate by eye against the photo: tarmac aggregate in photos 31/32 is a few mm, and a 4K tile at 2 m world width gives about 0.5 mm per pixel.
- **De-tiling**: use two samples at different scales with a noise blend, and a macro-variation texture at 20-50 m; the M2 pictures show exactly the tiled look.
- **Decals** (road lines, grate, cracks, patches): material domain `MD_DeferredDecal` (`MaterialDomain.h`:17) on a `DecalComponent`/`DecalActor` (`DecalComponent.h`: "Decal material"). RoadLines006-011 carry an opacity map: plug it into the decal Opacity, colour into BaseColor, normal into Normal, roughness into Roughness; set the decal's blend mode to "DBuffer" for best blending with lighting (setting names: not verified in source here). Our road marks should still be generated as geometry or RVT decals by `sim_core` data: these textures supply the wear.
- **Meshes (FBX)**: Poly Haven models are FBX with 4K or 2K textures. In `FbxStaticMeshImportData.h` the import option `bBuildNanite` ("allows to render objects with Nanite ... Can only be used with simple opaque materials") exists: enable it for the high-poly rocks (rock_face_01/02, mountainside) and modular fence; keep alpha-tested items (fences, grates) non-Nanite or accept that Nanite needs opaque materials. Check scale: Poly Haven FBX are in metres (dimensions come in mm in the API).
- **Landscape displacement**: `FMeshDisplacementMap` in `EngineTypes.h`:3233 (texture, magnitude, centre) exists in the Nanite settings for tessellation/displacement; ambientCG `Displacement` maps can drive it for the worn asphalt and rock faces. (Nanite tessellation availability in 5.8: not verified further.)
- **HDRIs**: import `.hdr` as `TextureCube` (the editor converts equirect to cube when imported as a cubemap; compression `TC_HDR_Compressed` BC6H or `TC_HDR`, `TextureDefines.h`:399-403). Use with `HDRIBackdrop` for lookdev, or as the SkyLight cubemap with "SLS Specified Cubemap"; the production sky should be SkyAtmosphere + VolumetricCloud.
- **Signs**: import JPGs as sRGB `TC_Default`; EPS cannot be imported by Unreal (convert to SVG/PNG outside the engine at the size needed, with the Crown copyright statement in credits).

## 8. Recommended set per category (what to use, in order)

| Category | Use | Fallback / note |
|---|---|---|
| Road asphalt, clean | ambientCG **Road012A (4K, grey, neutral)** and **Road014A (4K, darker)**; **Asphalt024A** | Asphalt025C for fresh black |
| Chipped / B-road / lane | ambientCG **Asphalt032**, Asphalt023S/020L (not downloaded: lighter) | patched layer on top |
| Worn, patched, cracked | ambientCG **Road009C (4K)**, Road015C, Asphalt019, Asphalt016, Asphalt027C | decals from the crack sets, once masks extracted |
| Marking wear | ambientCG **RoadLines006-011** as paint texture | our own UK patterns (not free anywhere) |
| Kerbs | our Kit/Kerbs + ambientCG **Concrete042A / Concrete031** | granite: Rock/Concrete grey |
| Footway | Road014A / Asphalt025C at footway scale; ambientCG **PavingStones085/087/088/089**; **TactilePaving002** | |
| Drainage | ambientCG **Grate001 / Grate002**, Poly Haven **water_manhole_cover** | UK covers: retexture |
| Walls | Poly Haven **rustic_stone_wall_02 / rustic_stone_wall / rock_wall_08 / mossy_stone_wall / stone_wall**; **brick_wall_005 / 02** | modelled walls strongly recommended for close view |
| Rock cuttings | ambientCG **Rock030 / Rock028 / Rock022 / Rock023**, chalk **Rock026 / Rock018**; Poly Haven rock_face_01/02 meshes | |
| Ground / verge | ambientCG **Grass004, Grass007, Ground037, Ground085, Gravel041, Ground062S** | long grass needs vegetation geometry |
| Furniture | Poly Haven utility_box_02, manhole; the rest modelled by us | section 0 |
| Signs | GOV.UK DfT sign images (Crown copyright) | hardware modelled by us |
| Sky | Engine SkyAtmosphere + VolumetricCloud; HDRIs **farmland_overcast**, kloppenheim_07_puresky, farm_field_puresky for lookdev | |

