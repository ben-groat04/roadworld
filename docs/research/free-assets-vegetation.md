# Free vegetation assets: research, downloads, ranking (11 Oct 2026)

Brief: owner, 11 Oct: "yes search far and wide for the best free assets you can. for everything. ... that vegetation is super low quality assets, thats the main issue". Free only, no accounts created, nothing run from downloaded files. Photos used as the target: `docs/reference/street/` 58-89 (opened for the composites: 63, 67, 68, 69, 72, 75, 81, 85, 86, 88, 89).

## 0. Read first: not got, not verified, no good free option

Not got / not verified
- **Blender MCP was not usable**: the `mcp__blender__*` tools are attached to a different program (scene info returned a CAD design with "Plug" bodies), so I did not import or render through them and touched nothing. Composites use (a) the sources' own preview renders (Poly Haven's official renders of the very asset I downloaded) and (b) Blender 5.2 headless (`C:\Program Files\Blender Foundation\Blender 5.2\blender.exe -b`, my own process) only for stacking images. No asset was rendered by me in Blender except where section 3 says so.
- **Unreal-side behaviour is read from source, not run**: nothing was imported into the editor (Unreal is a single lane; not my brief). Import/LOD/wind notes in section 5 cite `C:\UE_5.8` files.
- **The Procedural Vegetation Editor (PVE) sample trees on disk were not looked at**: they are `.uasset` and only the editor can show them. No preview image of them was available to me. Their quality is therefore UNVERIFIED by me. Fab listing text says they are Nanite Foliage, Experimental.
- **Fab, Megascans, Megaplants, Sketchfab, BlenderKit, botaniq-sample all need an account or survey**: not fetched. Price/licence on Fab could not be read (fab.com returns 403 to fetch; third-party pages only). Whether each Megaplants item is currently free is UNCONFIRMED (80.lv, 13 Nov 2025, headline says "free" for the first five; prices of the others not seen).
- **Sizes/timing**: this machine downloaded at roughly 0.2 to 0.5 MB/s. See section 2 for what finished and what is still running in the background.
- Epic's content licence page returned 403, so the "UE-only" status of PVE sample content and Megascans is from third-party text (Fab listing text: Megascans trees "licensed for use only with Unreal Engine-based products"); fine for this project (Unreal), but check before any non-UE use.

Plant types with NO good free option found
- **Hawthorn / blackthorn / trimmed lane hedge** (the single most important roadside item): no free photoscan or SpeedTree-quality hedge exists that I could find. Free hedges seen: OpenGameArt "Free Hedges" (Yughues, 2014, CC0, 11.8 MB, old low-detail), Free3D beech/dogwood hedges (royalty free, non-game-ready, 100k to 500k tris). Best route: build the hedge from good leaf/twig material (Poly Haven twig atlases inside `tree_small_02`, ambientCG LeafSet/Foliage, the PVE shrub preset) as a Nanite hedge mesh in-engine; or Megaplants (account).
- **Oak, ash, sycamore, field maple** (the common UK broadleaves): no free photoscanned UK species. Closest: PVE sample species (Norway maple, beech, hazel, aspen: UNVERIFIED), Poly Haven island_tree_01 to 03 and tree_small_02 (real scans but non-UK species, see composites), Mantissa generic broadleaf pack (no textures; not downloaded, see 2).
- **Cow parsley, bramble, gorse, ivy plants, rough seed-head grass**: nothing meshed and free. Closest: ambientCG LeafSet029 (scanned ivy leaves, build cards) and Foliage005 (grass blades) for cards; Poly Haven nettle_plant, grass_medium_01/02, weed_plant_02; Fab Megaplants nettle/elderberry (account). Gorse: none (only Sketchfab CC-BY "Gorse sprig in flower", 1.6M tris, account).
- **Mown-verge grass as a mesh**: Poly Haven grass tufts are round clumps (see composite); a continuous verge needs a ground material (ambientCG Grass005, CC0) plus scattered tufts.

## 1. Ranked catalogue (by closeness to the photos)

Licence quotes are from the source's own page, fetched 11 Oct 2026. "Acct" = account needed to download.

| Rank | Asset / source | Plant types | Licence (clause) | Acct | Formats / detail | Verdict vs photos |
|---|---|---|---|---|---|---|
| 1 | **Poly Haven `pine_tree_01`** (3 pines, photoscan textures, geonodes bake) https://polyhaven.com/a/pine_tree_01 | Scots-pine-like conifer | CC0: "You can use our assets for any purpose." / "You do not need to give credit." (https://polyhaven.com/license) | no | FBX 649 MB + 2K PNG, 17.4M tris (per earlier MANIFEST), also blend/gltf/usd | Mid-distance good (composite pine_tree_01 vs 75/89): right form, open flat-topped crowns. Foliage too dark/olive vs 75; bark lacks the orange upper trunk. Not good at close view without Nanite+retex. |
| 2 | **Poly Haven `fir_tree_01`** https://polyhaven.com/a/fir_tree_01 | spruce / conifer belt | CC0 (same) | no | FBX 249 MB + 2K PNG textures, 7.85M tris | Mid to far good vs 86/81: dense dark green, right tone; slightly too tidy and tall-thin for a plantation edge. |
| 3 | **Poly Haven `island_tree_01..03`, `tree_small_02`** | broadleaf (olive/island species, not UK) | CC0 | no | FBX, tri counts high (tree_small_02 2.06M) | Mid-distance only. Real leaf scans; trunks twisted and convincing (island_tree_01) but wrong species silhouette vs oak 67. Usable as a field/hedgerow tree when scaled and recoloured; poor as oak. |
| 4 | **Epic PVE sample species** on disk: Beech, Norway Maple, Common Hazel, Quaking Aspen, plus presets Leaf_Tree_01-04, Pine_01, Sapling_01, Shrub_01, Spruce_01-02 and Deciduous_Shrub_01 | maple, beech, hazel (hedge-layer shrub), aspen, spruce, pine, shrub | Epic engine content (EULA, UE only); not text-verified | no (already in `C:\UE_5.8`) | UE assets, PVE graph presets, Nanite Foliage + Dynamic Wind export | Not verified visually (see 0). Species match UK broadleaf better than anything above (beech, maple, hazel). Likely the best broadleaf source. First task: open `Maps/Asset_Zoo.umap`. |
| 2b | **Mantissa CG Birch pack** https://mantissa.xyz/free.html (DOWNLOADED, my own Blender render) | silver birch (5 trees, ~14.5 m, 1.78 to 1.82M faces each, real per-leaf geometry, 2K scanned bark, 512 px leaf maps, 3 leaf variants) | page: "Public Domain (CC0) License"; zip ships `LICENSE-CC0.txt` | no | FBX (+ jpg/tif textures), 887 MB zip | Best broadleaf I could verify myself (composite vs 84/31, no birch photo exists). Pendulous silver birch form, right for roadside/belt edge at mid-distance. Faults: exposed root flare must be buried; materials arrive without textures (assign by the ID list); leaf texture is a small JPG (no alpha: leaves are geometry); flat-lit in my render so colour is unverified. |
| 6 | **ambientCG Grass005 / Grass004** | mown verge ground | CC0: "All ambientCG assets are provided under the Creative Commons CC0 1.0 Universal License." (https://docs.ambientcg.com/license/) | no | 2K JPG PBR | Good tileable mown turf albedo; no black gaps; fixes the audit's "scale pattern" fault when used as ground material (composite). Close view needs scattered tufts too. |
| 7 | **ambientCG LeafSet029 (ivy), Foliage005 (grass blades)** | ivy cards, grass blade cards | CC0 (same) | no | 2K JPG + opacity + normal | Real scanned leaves on a flat backdrop with opacity map: right for building ivy and grass cards. Not a mesh. |
| 8 | **Poly Haven `grass_medium_01/02`, `nettle_plant`, `weed_plant_02`, `shrub_01-04`, `dandelion_01`, `celandine_01`, `periwinkle_plant`** | verge grass, nettle, scrub | CC0 | no | FBX 2K | Good plant detail, wrong layout: tufts are round balls (composite). Nettle plant good at close view. Use as scatter instances, not as the verge. |
| 9 | Mantissa Fir (free 3K, 1.3 GB, CC0), Spruce (2.2 GB, no textures), Generic tree pack (1.2 GB, no textures), Cherry, Japanese maple | conifer / generic / non-UK | CC0 per page | no | FBX | Not downloaded (see 2): fir duplicates Poly Haven; spruce is over the 2 GB per-asset limit; generic has no textures and species unknown. |
| 10 | Fab **Quixel Megaplants**: Norway Maple Forest, Common Hazel, European Aspen, European Beech, Black Alder, European Hornbeam (hedging tree), Elderberry, Stinging Nettle, Bilberry, Black Poplar, Wood Anemone, Dry Common Reed | tree species + nettle + elderberry | Fab listing text: Megaplants "Experimental", PVE presets, Nanite Foliage, Dynamic Wind. Price/licence not readable (403) | **yes (Epic account)** | UE only (PVE presets, branch meshes, textures) | Top quality if it is the same pipeline as item 4. Owner step in section 4. |
| 11 | Fab **Megascans Trees** (Black Alder 22 assets, Norway Maple 15, European Hornbeam 27: 10 forest, 4 field, 8 saplings, 5 seedlings) | trees | Fab listing text: "licensed for use only with Unreal Engine-based products". Free status unconfirmed | **yes** | UE only | Photoscanned, LODs, would beat anything free elsewhere. |
| 12 | Free3D / OpenGameArt hedges | hedge | Free3D "Royalty Free License"; OGA CC0 | Free3D yes (login); OGA no | 100k to 500k tris / unknown | Not downloaded: Free3D needs login; OGA pack is 2014 low detail. Unverified. |
| 13 | Sketchfab CC-BY, e.g. "Scots Pine Trees" (RBG_illustrations, 91k faces), "Gorse sprig in flower" (1.6M faces) | pine, gorse | per-model CC-BY | **yes** (download login) | glTF | Not fetched. URLs in section 4. |
| 14 | Blender tools: Modular Tree extension (https://extensions.blender.org/add-ons/modular-tree/, GPL-3.0-or-later add-on; generated trees are not covered by the add-on licence per my reading, UNVERIFIED), Blender "Sapling Tree Gen" | procedural trees | add-on GPL | no | n/a | Add-ons are code: not downloaded. Output is generic; not photoscan. The Grove has no free version (paid, EUR 89+). botaniq free sample needs a survey; BlenderKit free tier needs login for downloads. |
| - | ambientCG 3D models | none | n/a | n/a | n/a | ambientCG has no vegetation meshes (queries tree, hedge, bush returned only materials such as Bark, LeafSet). |

## 2. What was downloaded

Root: `C:\Users\bgbg0\Documents\Programming\roadworld\unreal\CityPainter\SourceArt\Meshes\Free\` (untracked, nothing staged or committed; licence text in `LICENCE.txt` beside each asset).

All complete (Poly Haven files checked byte-for-byte against the API sizes; Mantissa zip tested with `unzip -t`: no errors). Total about 2.9 GB. No archives were executed; only FBX/PNG/JPG/TIF/blend/usdc/tres texts were unpacked.

| Path (under `...\Free\`) | Size | Licence | Notes |
|---|---|---|---|
| `PolyHaven\pine_tree_01\` | 815 MB | CC0 | FBX 649 MB + 2K PNGs (bark, trunk_a/b/c, twig); 3 pines; 17.4M tris |
| `PolyHaven\fir_tree_01\` | 314 MB | CC0 | FBX 249 MB; 3 firs; 7.85M tris |
| `PolyHaven\fir_sapling_medium\` | 120 MB | CC0 | spruce/fir sapling |
| `PolyHaven\pine_sapling_medium\` | 269 MB | CC0 | pine sapling |
| `PolyHaven\island_tree_01..03\` | 123 / 107 / 135 MB | CC0 | broadleaf scans |
| `PolyHaven\grass_medium_01\`, `periwinkle_plant\` | 14 / 28 MB | CC0 | (grass_medium_02, nettle, shrubs etc. already in `..\PolyHaven\`) |
| `Mantissa\birch\` | 932 MB | CC0 (`LICENSE-CC0.txt` in folder) | 5 FBX birches + Textures + ID List |
| `ambientCG\Grass004, Grass005, LeafSet029, Foliage005\` | 88 MB | CC0 | 2K JPG PBR sets |

Each folder has a `LICENCE.txt` with URL and date. Nothing was `git add`ed; the folder is untracked (and this tree is LFS-tracked, so the main session decides).

Skipped on purpose: Mantissa Fir 1.3 GB (duplicates Poly Haven fir), Generic pack 1.2 GB (no textures, species unknown), Spruce 2.2 GB (over the 2 GB single-asset bound; URL https://ftp.mantissa.xyz/resources/trees/mantissa_spruce_trees_pack.zip), Cherry and Japanese maple (not UK). Total used is far under the 15 GB bound.

## 3. Composites (private, `docs/reference/_compare/free-assets-veg/`)

Left tile = the asset's own Poly Haven preview (their render, transparent background shown blue) or the ambientCG colour map; then photos.

| File | Asset | Photos opened | Verdict |
|---|---|---|---|
| `pine_tree_01-vs-photo75-89.png` | pine_tree_01 | 75, 89 | Good at distance (>30 m). Too dark/olive; crown too sparse vs 89. |
| `fir_tree_01-vs-photo86-81.png` | fir_tree_01 | 86, 81 | Good at distance; fine for belts and plantation edges. |
| `island_tree_01-vs-photo67-88.png` | island_tree_01 | 67, 88 | Mid-distance only; not an oak. Foliage detail is a real step up from the r2 rosette cards. |
| `tree_small_02+island_tree_03-vs-photo68.png` | tree_small_02, island_tree_03 | 68 | tree_small_02 is a slim light-leaf sapling (good for staked street/island saplings); island_tree_03 sits on a scan base and is a shrub tree: poor. |
| `grass_medium_01+02-vs-photo85-63.png` | grass tufts | 85, 63 | Round tufts: right grass detail, wrong layout. Scatter only. |
| `nettle+shrubs-vs-photo72-69.png` | nettle_plant, shrub_03, shrub_01 | 72, 69 | Single stems: good as understory instances; no gorse mass. |
| `ambientcg-grass005+leafset029+foliage005-vs-photo85-69.png` | ambientCG | 85, 69 | Grass005 good turf; LeafSet029 matches ivy in 69. |

## 4. Needs the owner's account (ranked by gain)

1. **Quixel Megaplants on Fab** (Epic account; check price on each page). Seller page: https://www.fab.com/sellers/Quixel%20Megaplants . Click Add to My Library on: Norway Maple Forest, Common Hazel, European Beech, European Aspen, European Hornbeam, Elderberry, Stinging Nettle (listing https://www.fab.com/listings/7b5e8db0-e9fa-499b-a16f-3bb3e130e92e), Bilberry, Black Poplar. Beech sapling listing https://www.fab.com/listings/6867d008-fc3a-404e-9e53-087eccdcd722 . Article: https://80.lv/articles/create-lush-nanite-foliage-ready-forests-with-this-free-quixel-asset-pack . Gain: highest (same Nanite Foliage pipeline; UK-relevant species; hornbeam/hazel/elderberry feed the hedgerow).
2. **Megascans Trees on Fab**: Black Alder, Norway Maple, European Hornbeam packs (search "Megascans Trees" on fab.com; confirm price shows Free). Gain: high for mid-field trees if free.
3. **Free rural house and pine forest (Laglanz)** per Jettelly article https://jettelly.com/blog/free-modular-house-pine-forest-environment-for-unreal-engine : free Fab pack with pine forest and PCG graph (UNVERIFIED quality).
4. **Sketchfab CC-BY** (login to download, attribution needed): Scots Pine Trees https://sketchfab.com/3d-models/none-bae9fb0f30b24231bc5b1caadbfaa4c3 ; Gorse sprig in flower https://sketchfab.com/3d-models/none-dbda8b84799b4512a4cda828c795b3aa ; Hawthorn Tree (10k faces) https://sketchfab.com/3d-models/none-32f484d39fe849f9b29f381f257ae25d . Gain: low to medium; unchecked quality.
5. botaniq free sample (survey, 38 assets), BlenderKit free tier (login): unknown relevance; low.

## 5. Recommended set and Unreal 5.8 route

| Plant type | Recommend | How |
|---|---|---|
| Conifer belts, plantation edges | Poly Haven fir_tree_01 | Distance and mid view only; Nanite on import. |
| Scots pine, roundabout pines | Poly Haven pine_tree_01 | Same; retint upper crown, add orange bark if needed. |
| Broadleaf (oak/ash/sycamore stand-ins) | PVE presets Leaf_Tree_01-04 and sample maple/beech/aspen (on disk), then Megaplants | Generate in editor via PVE; judge against 67/84/88 first (UNVERIFIED). Poly Haven island_tree_01 as a fallback mid-distance field tree. |
| Hedge layer | Build in-engine from PVE Shrub preset + hazel + ambientCG leaf cards; Megaplants hornbeam/hazel | No free hedge asset. |
| Birch | Mantissa birch | per section 3 status. |
| Mown verge | ambientCG Grass005 ground + Poly Haven tufts scattered | Material + foliage/PCG scatter. |
| Rough verge, nettle, cow parsley | Poly Haven nettle_plant, grass_medium_02; cow parsley has none (build from cards) | Scatter. |
| Ivy | ambientCG LeafSet029 as cards | Build in Blender, 4 cm leaves per audit item 5. |
| Gorse | none free; Sketchfab CC-BY (account) | |

Unreal facts, checked in `C:\UE_5.8`:
- Nanite Foliage is behind `r.Nanite.Foliage` (default 0, ReadOnly) in `Engine/Source/Runtime/Engine/Private/Rendering/NaniteResources.cpp` (line 119); the project setting is "Nanite Foliage (Experimental)" in `Engine/Classes/Engine/RendererSettings.h` (line 1550). It needs an editor restart and must be enabled in the project's rendering settings. Experimental.
- PVE: `Engine/Plugins/Experimental/ProceduralVegetationEditor` (`.uplugin`: IsExperimentalVersion true, EnabledByDefault false; "Create Nanite Foliage ready vegetation directly in the engine"). Presets export with `bCreateNaniteFoliage = true` and Voxelize shape preservation (`ProceduralVegetationPreset.h` lines 60-64); wind uses the Dynamic Wind plugin via `UPVWindSettings` (`PVWindSettings.h`), with sample settings under `Content/SampleAssets/WindSettings` (Tree, Sapling, GroundScatter). Sample content, ~560 MB: `Content/SampleAssets` (`Maps/Asset_Zoo.umap`, `StarterContent/Presets/PVE_Preset_*.uasset`).
- FBX import (Poly Haven, Mantissa): static mesh with Nanite enabled; for 8 to 17M triangle meshes expect long Nanite build times. Poly Haven normals are OpenGL (flip green); FBX material slots need re-linking to `textures/`; leaves need alpha masking (see `PolyHaven/MANIFEST.md`). Non-Nanite alpha-masked cards need hand LODs; no wind comes with FBX: use the engine SpeedTree/Pivot-Painter-free option of a WPO material, or convert via PVE.
