# Vegetation sources 2: oak, hedgerow, verge for Unreal 5.8 (24 Oct 2026)

Extends `docs/research/free-assets-vegetation.md` (11 Oct) and the pvetrees1 VERDICT. Nothing downloaded, nothing run in the editor. Plugin facts are from `C:\UE_5.8\Engine\Plugins\Experimental\ProceduralVegetationEditor` (PVE) source and content. Web facts are from search snippets and fetched pages dated as stated; fab.com itself returns HTTP 403 to fetch (twice more today), so Fab listing text is second-hand (Quixel news pages, search snippets).

## 0. Headline

1. The earlier survey is out of date on one point that matters most: **Quixel Megaplants now has a free English Oak (Quercus robur), European Hornbeam, Goat Willow, Silver Birch, Elder, European Spindle, Baltic Pine (Scots pine is Pinus sylvestris; Baltic pine is probably that species or close, UNCONFIRMED), Norway Spruce, Common Hazel, Beech, Aspen, Stinging Nettle** and more, "free to use under the Fab Standard License", converted for PVE in UE 5.8. No hawthorn, blackthorn, cow parsley, bramble or ash seen.
2. A PVE species is NOT authored from the shipped JSON files (they are baked skeletons). It is authored from the Grower node parameters, saved as a Grower Preset asset, in the editor UI.
3. Needs an Epic account (free) for Fab. No other source gives a UK oak.

## 1. Can a new PVE species be authored from a JSON like the shipped species folders?

Answer: **not by editing those JSONs for growth; yes as an import route for a skeleton you build elsewhere; the growth-parameter route is the Grower node.**

Evidence (all from the plugin source):
- The species JSONs (`Content/SampleAssets/Tree_Common_Hazel_01/Instances/Broadleaf_Hazel_01.json`, 20 MB each; also Beech 4, Aspen 4, Norway Maple 4) hold `globalAttributes` (grower parameters as flat arrays, e.g. `axialElongation`, `phyllotaxy`, `randomSeed`, `gravitationalForce`, `maxBranchNumber`), `points` (7,655 positions in the hazel file) and `primitives` (381 branches with `parents`, `children`, `branchNumber`). So each file is a complete GROWN skeleton, not a recipe. Parsed with node, keys listed above.
- `LoadGrowthDataJsonToCollection` (`Private/Helpers/PVJSONHelper.cpp` line 577) requires `points.positions`, `points.attributes.budDirection.values`, `primitives.points`, `primitives.attributes.parents|children|branchNumber.values`. Positions are metres, Y/Z swapped, times 100.
- `UProceduralVegetationGrowthDataAsset` (`Private/DataAssets/ProceduralVegetationGrowthDataAsset.cpp`) has a `JsonDirectoryPath` and an `UpdateDataAsset` button that imports every `*.json` in a folder as named variants. The `GrowthDataLoader` node (`Private/Nodes/PVGrowthDataLoader.h`) outputs one pin per variant. So a hand-written or script-generated JSON in that schema could in principle be loaded (UNTESTED: the format has many more attributes than the five required; a minimal file might load but mesh badly). A separate `LoadMegaPlantsJsonToCollection` reads the Megaplants format.
- The Grower node (`Private/Nodes/PVGrowerSettings.h`) has `Preset` (a `UProceduralVegetationGrowerPreset` asset) plus `FPVGrowerParams` (`Private/DataTypes/PVGrowerParams.h`, 135 UPROPERTYs): growth cycles (1 to 45), random seed, trunk/branch phyllotaxy, trunk growth, phototropism, gravity, age and light senescence, bifurcation (trunk and branch), directional jitter, foliage. Tooltip: "Assigning a preset copies its grower parameters into Settings." `SaveGrowerPreset` node (`PVSaveGrowerPresetSettings.h`) writes a preset asset. This is how an English oak or hawthorn preset is authored: tune the Grower parameters in the graph (wide spreading crown = low apical dominance, strong lateral elongation, gravity), save a Grower Preset. **Done in the editor UI by a human or UI automation; no script API found.** Presets are `.uasset`, not JSON.
- **Extract from Mesh** node (`PVExtractFromMeshSettings`): tooltip "Derive a plant skeleton from a static mesh. Traces the geometry of a Static Mesh asset and extracts a branch skeleton ... Material filters let you keep only the bark/structural portions while ignoring leaves ... Useful for converting hand-modeled trees into procedural skeletons". There is also Extract from Image. Real route to a specific UK oak shape: model or generate a trunk-and-branch mesh in Blender, extract, let PVE foliage and mesh it. Quality untested.
- The old `Importer` node is deprecated and produces nothing. `FoliagePalette` node accepts any static or skeletal mesh as foliage, so an oak-leaf twig mesh/material could replace the sample leaf on any tree.
- Export remains UI-only (pvetrees1: `FPVEditor::OnExport` opens a dialog), so every species variant must be exported in the editor ahead of time.

Verdict: authoring English oak from scratch in PVE is possible but is hand tuning with no scriptable path; **taking Megaplants English Oak (which ships as PVE presets) is far cheaper** and gives real oak parameters.

## 2. Free photoscanned / high-quality UK assets for UE 5.8 Nanite

### 2a. Quixel Megaplants on Fab (best)

Sources (Quixel news, fetched today):
- https://quixel.com/news/quixel-on-fab-new-megascans-and-megaplants (8 Apr 2026): free Megaplants "free to use under the Fab Standard License": **English Oak**, Greasewood, **European Hornbeam**, Yoshino Cherry, Giant Bamboo, **Goat Willow**, **Baltic Pine**, Baltic Pine Saplings; previously released ones updated January 2026.
- https://quixel.com/news/discover-the-latest-quixel-megascans-and-free-megaplants (16 Jul 2026): **European Spindle**, European Aspen Saplings, **Silver Birch**, Japanese Medlar, Norway Maple Saplings, European Beech Saplings, Japanese Cypress, **Norway Spruce**, **Elder**. Text: existing Megaplants "converted for PVE in UE 5.8"; "Going forward, Megaplants released will be compatible with UE 5.8 and above only."
- https://quixel.com/news/check-out-these-recently-released-quixel-megascans-and-megaplants (9 Oct 2026): Huckleberry Oak, Black Poplar, Bilberry, Ginkgo, Dead Pine, Dry Common Reed, Wood Anemone, **Stinging Nettle**, Lingonberry, Small Balsam, Common Heather, Common Bulrush; "fully Nanite-compatible".
- Seller page https://www.fab.com/sellers/Quixel%20Megaplants (search snippet shows English Oak at "Free").
- Earlier list (11 Oct survey): Norway Maple Forest, Common Hazel, European Aspen, European Beech, Black Alder, Elderberry. Search snippets also showed Rowan Saplings and Goat Willow listings.
- Contents per listing (search snippet): editable PVE presets, branch foliage meshes, textures, material instances, Nanite foliage, Dynamic Wind. Setup: enable PVE plugin, turn on "Nanite Foliage (Experimental)".
- Licence: Quixel says "free to use under the Fab Standard License" (fab.com/eula, not fetchable). Fab's summary (dev.epicgames.com/documentation/en-us/fab/licenses-and-pricing-in-fab, via search): commercial or private use, modify, commercially distribute projects including them, no credit needed; may not resell or redistribute the asset standalone; "for convenience only and is not legally binding". Personal/Professional tiers differ by revenue (over USD 100,000 per 12 months = Professional), same rights.
- **Account: Epic Games account (free) to add to library.** No payment while price is Free. I could not read the live price of each; confirm at add time.
- **Risk to flag**: listing text (snippet) says Megaplants and PVE are Experimental and "projects with Experimental features should not be shipped". Fine for prototyping; a shipping decision later.
- Not found anywhere in Megaplants: **hawthorn, blackthorn, ash, field maple, sycamore, bramble, cow parsley, gorse, Scots pine by name**.

### 2b. Megascans (premium, mostly paid since 2025)
Free window ended 2024 (https://80.lv/articles/megascans-no-longer-free-after-2024/, https://www.cgchannel.com/2024/10/epic-games-has-made-megascans-free-to-all-but-only-until-the-end-of-2024/); Quixel pages call 2026 Megascans packs "premium content" (e.g. "European Broadleaf Forest", 24 Mar 2026; "Temperate Biome Megadrop", 29 Sep 2026). Pricing not stated; legacy-library claims (quixel.com / Bridge) keep Epic Content Licence. Contents of European Broadleaf Forest and whether it has oak/hawthorn: UNVERIFIED (could be the one paid candidate for UK hedge/field species; check on Fab and flag cost to owner).

### 2c. CC0 / other (unchanged from 11 Oct unless noted)
- Poly Haven: no oak, hawthorn, hedge or cow parsley found (search today and 11 Oct). Still have pine_tree_01, fir_tree_01, island_tree_01..03, tree_small_02, nettle_plant, grass_medium_01/02, weed_plant_02, shrub_01..04 (CC0, https://polyhaven.com/license).
- Mantissa birch (CC0) already downloaded; Megaplants Silver Birch now supersedes it for wind and Nanite Foliage.
- ambientCG Grass005 ground, LeafSet029 ivy cards, Foliage005 grass blades (CC0).
- Hawthorn/bramble/cow parsley exist only in paid Daz3D packs (Hedgerow Kit Bundle, https://www.daz3d.com/hedgerow-kit-bundle; not game-licence checked): not recommended.
- Sketchfab CC-BY hawthorn (10k faces), Scots pine (91k), gorse: need login and attribution, quality unseen.

## 3. Scripted generation fallback in Blender (not downloaded or run)

- **Sapling Tree Gen**: bundled add-on, GPL (https://docs.blender.org/manual/en/4.1/addons/add_curve/sapling.html: "License: GPL ... bundled with Blender"). Presets saved as .py; no oak preset confirmed. Output is curve-based branches with leaf cards or instanced leaves; looks stylised, needs heavy work for photoreal. Scriptable with `execute_blender_code` (the Blender MCP exists in this session).
- **Modular Tree** (https://extensions.blender.org/add-ons/modular-tree/): add-on GPL-3.0-or-later, core library MIT; Blender 4.3/4.5 LTS+. Has **Oak** tree preset and Oak/Maple/Birch/Willow/Pine leaf presets, procedurally generated leaves with veins or a custom leaf mesh, **Pivot Painter 2.0 export (two EXR textures + UV2) for Unreal Engine 5**, "only Unreal Engine 5 export is fully tested". Page does not state a licence for generated output (generated meshes are normally not covered by add-on licence, but UNVERIFIED). Best scripted fallback; leaves are real geometry, so Nanite foliage-style import (static mesh, Nanite on, WPO pivot-painter wind) is plausible. Quality vs the oak photos: unjudged.
- The Grove: paid (EUR 89+); excluded.
- Blender-built oak skeleton + PVE Extract from Mesh (section 1) is a hybrid worth one test.
- Nanite note (from pvetrees1 facts): leaves only exist with `r.Nanite.Foliage=1`; static meshes imported from Blender can instead use ordinary masked cards or real geometry.

## 4. Recommendation per need

| Need | Source | Cost | Account | Effort to get into game |
|---|---|---|---|---|
| Oak (field and hedgerow) | Fab Megaplants **English Oak** (free, PVE presets) | free | Epic account, owner adds to library | Add to library, copy to project, enable PVE plugin, open presets, export variants in editor (UI-only), place via skinned instancing. Medium; first export test needed. Fallback: Modular Tree Oak + Pivot Painter. |
| Ash | none free found. Stand-in: PVE sample maple or Megaplants Elder/Hornbeam; or tune a Grower preset | free | none beyond above | Hand tune; low confidence. Ask Megaplants again later (new releases ongoing). |
| Hawthorn hedge / field hedge section | no source. Build clipped hedge from Megaplants **Hornbeam / Spindle / Elder / Hazel** bushes via PVE (clip or scale to a hedge block) plus ambientCG leaf cards; one mesh per hedge piece | free | Epic | Highest effort and the biggest gap; Grower preset tuning or Blender hedge block with Extract from Mesh. Possible paid shortcut: Megascans "European Broadleaf Forest" if it contains hedge species (UNVERIFIED, cost unknown; owner decision). |
| Scots pine | Megaplants **Baltic Pine** (species to confirm) else Poly Haven pine_tree_01 (CC0, already on disk, 17M tris, no wind) | free | Epic / none | Baltic Pine: same as oak. Pine_tree_01 already local. |
| Silver birch, spruce, hornbeam, willow | Megaplants Silver Birch, Norway Spruce, Hornbeam, Goat Willow | free | Epic | Same pipeline. |
| Nettle | Megaplants Stinging Nettle (or Poly Haven nettle_plant, CC0, on disk) | free | Epic / none | Low. |
| Verge grass | ambientCG Grass005 ground + Poly Haven grass tufts | free | none | Low (already on disk). |
| Cow parsley, bramble | none free. Build cards in Blender from ambientCG LeafSet/Foliage or Modular Tree custom leaf; or wait for Megaplants | free | none | Medium. |

Sequence: (1) owner adds English Oak, Hornbeam, Elder, Spindle, Hazel, Baltic Pine, Silver Birch, Nettle, Beech and Rowan Saplings from the seller page to the Epic library (one session, free); (2) a builder copies them to the scratch project and exports one variant each in the PVE editor (single Unreal lane; export needs UI or desktop automation, as pvetrees1 found); (3) judge against reference photos 66, 67, 68, 80; (4) only then decide on hedge strategy and any paid pack.

## Not verified
- Live Fab prices and exact contents of each listing (403).
- Whether Baltic Pine is Scots pine.
- Whether a minimal hand-written growth JSON loads.
- Image quality of any item (nothing rendered).
