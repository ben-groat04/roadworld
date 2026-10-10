# Vegetation and surface realism: Nanite, Infinigen and the "sterile" look

Research note, 10 Oct 2026. Question from the developer: "feels like a Nanite job?" and "what about Infinigen?". Tags: [V] = read in a primary source (official docs, GitHub, vendor listing) this pass; [U] = unverified (search snippet, forum, inference). Cost and effort figures are my estimates.

## Short answers
- **Nanite:** only partly. Nanite cannot be built for runtime-generated road/terrain meshes with stock Unreal [V], so it will not fix the road surface. It is right for static modular pieces and vegetation instanced along sim output. The "sterile" look on roads is mostly a material and decal problem, which works on runtime meshes.
- **Infinigen:** worth one cheap experiment for rocks and shrubs, not a dependency. Windows is experimental, trees do not export cleanly to FBX/OBJ, and the docs mention neither Unreal nor game-ready output.

## A. Infinigen
- **Licence:** BSD 3-Clause, Princeton University 2023 [V, LICENSE]. No third-party notes in the file; I did not audit dependencies (Blender is GPL, but generated assets are your output; get the licence of any bundled texture or asset library checked before shipping) [U].
- **State:** latest GitHub release is 2.0.0a3 (alpha), "Add new roomtypes, assets, house tours"; page showed "08 Oct" with no year [V]. Product lines: Nature, Indoors, Articulated. Terrain types added: canyons, mountains, volcanoes [V]. Needs Python 3.11, ProcFunc 0.37, Shapely 2.x [V].
- **Single assets:** `python -m infinigen_examples.generate_individual_assets -f <Factory> -n N --save_blend`, e.g. `BushFactory`, `ChunkyRock`; lists of nature meshes and materials exist in `tests/` [V]. Timings are not documented [V]. Trees, flowers and grass factories exist in the repo [U, not itemised here].
- **Windows:** "Minimal install (objects and materials)" is Experimental on native Windows and WSL2; terrain is "No" on native Windows, Experimental on WSL2 ("feasible but untested and undocumented") [V]. Blender version, RAM, GPU and disk are not stated in the install page [V]. Expect WSL2 for anything serious [U].
- **Export:** `.blend` to OBJ, FBX, STL, PLY, USDC; bakes only Albedo, Roughness, Normal, Metallic at `-r` resolution (slow, lossy); no Unreal mention [V]. Trees use instancing for leaves and branches; whole-tree OBJ export is "generally unsupported"; only .blend and .usdc keep instances, everything else realises every leaf [V]. FBX can fail on invalid UVs [V]. No LOD, wind data or cards are documented [V by absence].
- **Unreal path:** nothing official. My inference: USDC or realised FBX, decimate, rebuild foliage as cards or low-poly clusters by hand. A forum report notes thin branches/leaves vanishing when a USD/FBX tree is converted to Nanite [U].
- **Effort for 10 species + rocks + 3 ground materials:** 2 to 4 weeks, most of it export clean-up, LODs and leaf/wind setup, and likely a WSL2/Blender build [U]. Rocks and ground materials are the cheap wins (single meshes, simple bakes); trees are the expensive part.

## B. Alternatives (verdict for this project)
- **Fab / Megascans:** free window ended end of 2024; most is paid, some free, claimed assets are yours forever; needs an Epic account and payment for paid items [U: 80.lv, CG Channel; Fab licence page returned 403]. Fab Standard licence covers commercial use [U]. Verdict: **pragmatic default** for UK-ish trees, hedges, ground clutter; budget a few tens of pounds.
- **Procedural Vegetation Editor (5.8):** Experimental, Nanite-ready trees, new growth/graft nodes [V, Epic 5.8 announcement via snippet]; a forum report says PVE meshes crash packaged 5.8 builds [U]. Verdict: **try, but export test must include a packaged build**.
- **Nanite Foliage:** 5.7 Experimental (assemblies, skinning, voxel) [U]; 5.8 status not found; Epic's page is "Nanite Foliage in Unreal Engine" (only a contents list was readable). Nanite itself supports opaque and masked, WPO (limited), ISM/HISM, foliage painter, landscape grass; 16M instance cap [V]. Verdict: **use opaque/masked Nanite instances; treat foliage-specific features as optional**.
- **PCG GPU scatter:** 5.8 adds GPU runtime scatter comparable to landscape grass [V, in `unreal-photorealism.md`]. Verdict: **adopt** for grass and clutter.
- **SpeedTree:** Indie subscription from about $19/month, revenue cap $100K per Unity/SpeedTree pages (one source says $200K), works with Unreal, no SDK in Indie [U]. Verdict: **good if you need many tuned species; check cap**.
- **Poly Haven (CC0):** a few trees/plants exist; counts and poly counts not verified [U]. Verdict: **fine for rocks and ground textures**, not a tree library.

## C. Is it a Nanite job for roads?
- **Runtime meshes:** Epic's Geometry Script guide says DynamicMeshComponent supports raytracing but "Nanite and Lumen are not" [V]. No official runtime Nanite build API found. Realtime Mesh Component Pro claims runtime Nanite on 5.5-5.8 [U: vendor listing; price and licence not readable, 403].
- **Tessellation/displacement:** Nanite Tessellation and Static Displacement need Nanite meshes [V]; so not for non-Nanite runtime meshes (no evidence otherwise). Alternatives for relief: parallax occlusion mapping and pixel depth offset in the material [established, U for 5.8]; fine normal detail with triplanar/world-aligned textures.
- **Where Nanite fits:** kerbstones, gratings, bollards, barrier and parapet modules, copings, rocks, trees as static meshes instanced by sim transforms in ISM/HISM [V: Nanite supports ISM/HISM, spline meshes]. Limits: 16M instances, WPO clamped, masked costs overdraw, mesh decals unsupported on Nanite [V].
- **What actually kills sterility** (all work on runtime meshes unless noted): layered materials with macro variation and anti-tiling; world-aligned blending; RVT to blend road edges into terrain (Landscape-centred in the docs; non-Landscape meshes can write to RVT if static and inside a volume [V]); non-Nanite mesh decals and deferred decals for patches, cracks, stains; vertex-colour or mask-texture wheel-track wear; worn marking materials; wetness/puddles; edge grass cards and gutter debris as instances. Epic samples (City Sample, Electric Dreams, Valley of the Ancient) demonstrate these; I did not fetch them, so which sample shows which technique is [U].

## D. Recommendation
1. **Road/kerb/wall surfaces:** fix the material first (macro variation, wear masks from sim data in vertex colour, decals, wet roughness). Kerbs, gratings, copings as Nanite instanced modules. No runtime Nanite plugin yet.
2. **Grass and ground cover:** PCG GPU scatter with Fab grass/clutter; edge cards along verges.
3. **Hedges and trees:** Fab or Megascans plus PVE trial; instanced Nanite where opaque. Packaged-build test mandatory.
4. **Rocks and cutting faces:** Nanite rock kit from Fab/Poly Haven; triplanar rock material on the cutting meshes. Infinigen `ChunkyRock` is the cheap experiment.
- **Infinigen experiment (half a day):** minimal install in WSL2, generate 5 `ChunkyRock` and 5 `BushFactory` assets with `--export fbx -r 1024`, import to UE, compare against a Fab rock/shrub under the same lighting for triangle count, texture quality and look. Reject if bushes need heavy manual repair.
- **Needs account/payment/large install:** Epic account for Fab; paid Megascans packs; SpeedTree subscription; WSL2 + Blender + Python 3.11 (several GB, size not documented); RMC Pro licence if ever needed.

## E. Wider survey (what else gets there faster)
Verdicts are for runtime-generated roads/terrain, solo dev, UE 5.8, RTX 3060 at 1440p/60 upscaled. Account = Epic account.
- **Epic samples** (City Sample, Electric Dreams, Valley of the Ancient): free, licensed "UE-only content", commercial use inside Unreal products allowed [V for City Sample/Electric Dreams labels: Marketplace pages]. Electric Dreams has PCG levels incl. a spline example, ditch assembly and forest, plus a Substrate shader ball [V, 2023 listing]. City Sample is the best source of road/pavement materials, decals and PCG-style street dressing [U: contents not itemised here]. Valley of the Ancient is a 2x2 km Megascans environment (sky, post-process, vegetation) [V]. Verdict: **mine on day one** for sky/post-process and PCG graphs; needs Epic account and a large download (tens of GB) [U]. Lyra and 5.8 feature samples: not researched.
- **Fab:** bi-monthly free drops continue (Aug 2026 rounds seen) and claimed items are kept [U: gamedev.net/GameFromScratch]. Megascans mostly paid since 2025 [U]. Ultra Dynamic Sky/Weather exists; price not found [U]. Procedural Road Toolkit exists (spline roads, junctions, terrain conforming) but no mention of wear or kerbs; sample textures test-only [V, listing text]. No UK-specific street pack or road-decal pack found [U]. Verdict: **buy decals (patches, cracks, manholes, stains) and a UK-ish pavement pack when the material test shows a gap**; needs payment.
- **Lighting:** Lumen Lite (5.8, Epic claims up to 2x faster than High) [V via Guru3D]; settings `sg.GlobalIlluminationQuality 1` [V, earlier note]. Overcast look: low-saturation sky, heavy cloud cover, weak sun, height fog, local exposure; recipe not verified [U]. Verdict: **A/B Lumen Lite vs High on day one** (S).
- **Runtime meshes vs Lumen:** `UDynamicMeshComponent` lacks Nanite, Lumen, Mesh Distance Field, LODs and instancing, but supports ray tracing [V, Geometry Script guide]. So hardware RT Lumen is the stock route; cost on 3060/4060 unmeasured [U]. Other routes: bake tiles to UStaticMesh (editor only unless runtime conversion works, undocumented) [U], or Realtime Mesh Component Pro (claims Nanite, DF, cards; vendor, 403) [U]. Verdict: **test HWRT Lumen on a road tile first** (S).
- **Upscaling:** NVIDIA DLSS 4.5 UE 5.8 plugin v8.7.2 reported; Super Resolution and Ray Reconstruction work on RTX 30 series, Frame Generation needs RTX 40+, Multi Frame Generation RTX 50 [U: forum + press]. 20/30-series may pay a cost for the newer transformer model; Model K (DLSS 4) is the suggested fallback [U]. AMD FSR plugin for 5.8 ships FSR 4.1.1 (ML on RDNA 3/4, analytic fallback on older) [V, GPUOpen]. Budget: 3060 at 16.6 ms, upscaler about 1 ms [U estimate]. Verdict: **TSR default, DLSS plugin on day one** (S); free, driver and plugin download.
- **Surface detail on runtime meshes:** RVT works for non-Landscape static meshes inside a volume [V]; mesh decals are unsupported on Nanite [V] but fine on non-Nanite runtime meshes. Vertex-colour wear masks, parallax/pixel depth offset, stochastic anti-tiling and Substrate wet roads are standard material techniques [established, not re-verified]. Verdict: **vertex-colour wear + anti-tiling material is the biggest cheap win** (M).
- **Procedural tooling:** PCG fed from C++ splines/points is plausible, GPU runtime scatter in 5.8 [V earlier note]; supplying external data not verified [U]. Component choice: UProceduralMeshComponent (simple), DynamicMeshComponent (richer, no Lumen), RMC Pro (Nanite claims); known crash lists not found [U]. Houdini Engine/Blender GN for static kit pieces: not researched; Blender is free. RoadRunner exports FBX/OpenDRIVE and has a UE plugin, UE5 support unclear in docs (Feb 2025 thread) [U]; learn its junction blends and decal-based wear, do not adopt.
- **AI/ML:** AI PBR generators (Substance Sampler/Firefly credits, 3D AI Studio, Meshy) are usable for base materials, vendor claims, licences vary [U]; Poly Haven CC0 explicitly allows any use [V, FAQ via snippet]. RealityScan (free under $1M) turns the owner's photos into textured meshes for kerbs/walls [U]; Gaussian splat import to Unreal not documented [U]. Verdict: **photo-scan 3 kerb/wall samples** (M); AI textures only as placeholders.
- **Other leverage:** weather/wetness driven by a single material parameter collection; reference-photo side-by-side reviews against `docs/reference/gsv/`.

### Cost and licence (owner: cheap or free, personal for now)
- **Free, no catch:** engine built-ins (Lumen, RVT, PCG, decals, TSR); Epic samples (UE-only licence: usable in commercial Unreal products, so no later block unless you leave Unreal) [V]; Poly Haven CC0 [V]; ambientCG is also listed as CC0 [U, snippet]; Blender, Infinigen (BSD-3) [V]; DLSS/FSR plugins (free downloads; NVIDIA/AMD terms not read) [U]; RealityScan free under $1M revenue, which matches the engine royalty line [U].
- **Watch for later commercial release:** FreePBR is personal-use only (commercial about $16) [U]; Fab "Personal" or free-for-learning tiers and any non-Standard licence can block shipping, so read each Fab listing's licence line before relying on it [U]; Firefly/AI-generated texture terms unchecked [U]; SpeedTree Indie is capped at about $100K revenue [U].
- **Paid, only where it replaces lots of work:** SpeedTree Indie from about $19/month (free alternative: PVE plus Fab free drops) [U]; Fab decal/pavement packs, price per listing unverified (free alternative: Poly Haven/ambientCG textures plus hand-made decal textures); Realtime Mesh Component Pro, price unverified (free alternative: stay on DynamicMeshComponent plus hardware RT Lumen); Ultra Dynamic Sky, price unverified (free alternative: engine Sky Atmosphere and Volumetric Clouds).

### What to try first (ranked: free first, then payoff)
1. Road material: macro variation, anti-tiling, vertex-colour wear masks, wet roughness, using CC0 textures. Free. High, S-M, day one.
2. Sky, post-process and exposure copied from Valley of the Ancient or City Sample. Free. High, S, day one.
3. Hardware RT Lumen vs Lumen Lite on a runtime road tile; record ms. Free. Decides architecture, S, day one.
4. Mesh decals for patches, cracks and manholes on runtime road tiles (self-made or CC0 textures). Free. High, S, day one.
5. TSR vs DLSS 4.5 vs FSR plugins on 3060/4060. Free. Medium, S, day one.
6. RVT road-edge blend into terrain. Free. High, M.
7. PCG GPU grass and clutter on verges, with free Fab drops and Poly Haven/PVE trees. Free. High, M.
8. Nanite instanced kerb, grating and parapet modules from sim transforms (Blender-made). Free. Medium-high, M.
9. Infinigen rock/bush test (A). Free. Low-medium, M.
10. RealityScan of own kerb/wall photos. Free. Medium, M-L. Paid decal or tree packs only if 1-8 still look flat.

## Sources (accessed 10 Oct 2026)
- https://www.guru3d.com/story/unreal-engine-58-debuts-lumen-lite-and-productionready-megalights/
- https://dev.epicgames.com/documentation/en-us/unreal-engine/geometry-scripting-users-guide-in-unreal-engine
- https://api.unrealengine.com/marketplace/en-US/product/electric-dreams-env
- https://api.unrealengine.com/marketplace/en-US/product/city-sample
- https://dev.epicgames.com/documentation/en-us/unreal-engine/valley-of-the-ancient-sample-game-for-unreal-engine
- https://gpuopen.com/learn/amd-fsr-plugin-updated-for-unreal-engine-58/
- https://forums.developer.nvidia.com/t/nvidia-dlss-unreal-engine-plugin-archive/377564
- https://hwbusters.com/news/nvidia-dlss-4-5-super-resolution/
- https://gamedev.net/news/5027-august-2026-free-gamedev-asset-round-up-2/
- https://docs.polyhaven.com/en/faq
- https://radiancefields.com/platforms/realityscan
- https://www.3daistudio.com/blog/best-ai-texture-and-pbr-generators-2026
- https://au.mathworks.com/matlabcentral/answers/2173877-roadrunner-unreal-engine-5-export
- https://www.fab.com/listings/34605f9d-e5e8-4a71-a64f-5bf230da274c (Procedural Road Toolkit)

## Sources, sections A to D (accessed 10 Oct 2026)
- https://github.com/princeton-vl/infinigen (README, releases; GitHub intermittently 503)
- https://raw.githubusercontent.com/princeton-vl/infinigen/main/LICENSE
- https://infinigen.cs.princeton.edu/docs/latest/Installation.html
- https://infinigen.cs.princeton.edu/docs/latest/ExportingToExternalFileFormats.html
- https://infinigen.cs.princeton.edu/docs/latest/GeneratingIndividualAssets.html
- https://dev.epicgames.com/documentation/en-us/unreal-engine/nanite-virtualized-geometry-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/runtime-virtual-texturing-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/geometry-scripting-users-guide-in-unreal-engine
- https://www.unrealengine.com/en-US/news/unreal-engine-5-8-is-now-available (via search snippet)
- https://forums.unrealengine.com/t/procedural-vegetation-nanite-foliage-meshes-crash-packaged-project/2730886
- https://80.lv/articles/megascans-no-longer-free-after-2024/
- https://store.speedtree.com/?p=810750
- https://www.fab.com/listings/c04e6977-a3bd-4e24-b0a2-0b696bee90da (403; claims from search snippet)
