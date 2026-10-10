# Unreal 5.8 photorealism: what fits City Painter

Research note, 10 Oct 2026. Targets (planning-log s1-2): RTX 3060 class, 1440p/60 with upscaling, 20 km+ maps, free orbit camera (street to high aerial), runtime-edited roads and terrain, procedural buildings, dense traffic, vegetation later.

**Confidence key.** [5.8 verified] = stated in the 5.8 release notes or press coverage fetched on 10 Oct 2026. [established] = long-standing UE5 behaviour from Epic docs / prior releases (5.5-5.7); not re-verified against 5.8 notes in this pass. [unverified] = vendor claim or gap; test before relying. The 5.8 release notes page is ~357k chars and I read about 300k; Nanite, VSM and Lumen detail in 5.8 is thin there. Cost figures for the RTX 3060 are my estimates, not measurements: the look test must replace them.

Context: UE 5.8 shipped 17 Jun 2026 and is reported as the last UE5 release (80.lv, GameFromScratch). It is an optimisation and stabilisation release. Pin 5.8 for v0.

## 1. Feature verdicts

### Lumen GI and reflections
- **Realism:** dynamic bounce light, sky occlusion, reflections. The biggest single "looks real" lever for streets (shaded canyons, coloured bounce from brick).
- **5.8:** new **Lumen Lite** (Beta): medium GI using irradiance fields with probe occlusion, reported ~2x faster than Lumen High, default for handhelds; enable with `sg.GlobalIlluminationQuality 1` / `sg.ReflectionQuality 1`. Height fog in reflections now on by default; surface cache min card res 4 to 2; surface cache opacity removed (~16 MB saved). [5.8 verified]
- **Fit:** Software RT (default) traces mesh distance fields plus a card-based surface cache. Both are built per static mesh at import/cook. Runtime-generated meshes (roads, earthworks, buildings) do **not** get distance fields or cards automatically on stock ProceduralMesh / DynamicMesh components [established; Epic docs silent, plugin vendors claim otherwise, unverified]. Result: runtime geometry is invisible to software Lumen GI or lit wrongly. Hardware RT Lumen traces real triangles, so dynamic geometry works, but costs more and needs RT-capable GPU (3060 can, at a price). Aerial range: Lumen Scene draw distance is limited (~200 m default per clipmap level); at aerial zoom it fades to sky/reflection captures. That is acceptable for a top-down view.
- **Cost, 3060 @1440p (upscaled from ~960p-1080p):** software High ~3-5 ms; Lumen Lite maybe 1.5-3 ms; HWRT Lumen 6-10 ms+ in dense scenes (estimate).
- **Maturity:** Lumen production; Lumen Lite Beta; HWRT Lumen production but heavy.
- **Verdict:** **adopt for v0** with software RT and Lumen Lite as the A/B pair. Test runtime road/building meshes as the first question (see s3). Keep HWRT as a quality-tier option, not baseline.

### Nanite (incl. tessellation, foliage)
- **Realism:** pixel-sized geometry, no pop, cheap detail on buildings, kerbs, rocks, trees. Also feeds VSM efficiently.
- **5.8:** tessellation can be disabled inside VSM via `r.Shadow.Virtual.Nanite.AllowTessellationDirectional/Local`; skinned meshes get WPO controls; foliage types get "Nanite Pixel Programmable Distance". Nanite foliage and tessellation/displacement existed since 5.4-5.6 (Nanite Foliage was Experimental in 5.6/5.7; state in 5.8 not confirmed). [5.8 verified for the listed items; rest established]
- **Fit:** Nanite data is built offline (editor/cook) when a UStaticMesh is built. A mesh generated at runtime cannot take the normal Nanite build path. Options: (a) leave runtime meshes non-Nanite (ordinary LOD, culling, instancing); (b) build chunked meshes in the editor/Unreal build step for **static** content (module kit, props, trees) and keep **only** player-edited roads/earthworks/terrain as non-Nanite; (c) a third-party runtime Nanite builder exists (Realtime Mesh Component Pro claims runtime Nanite, distance fields and Lumen cards for UE 5.5-5.8; vendor claim, unverified, licence and risk need review). Nanite does not support translucency, and masked/WPO materials cost more. Dense instancing: Nanite ISMs/HISMs are the cheapest way to draw thousands of trees/houses; cars are fine as Nanite static meshes (wheels as separate meshes).
- **Cost:** Nanite itself is ~1-3 ms on a 3060 for city scenes; overdraw with masked foliage is the danger.
- **Maturity:** production for static meshes; foliage/tessellation less so (check at 5.8).
- **Verdict:** **adopt for v0** for buildings modules, props, rocks. **Avoid** depending on Nanite for runtime road/terrain meshes; go to option (c) only after the look test shows a real need.

### Virtual Shadow Maps
- **Realism:** crisp, stable shadows from sub-metre to km; essential with Nanite and Lumen sun shadows.
- **5.8:** invalidation throttle `r.Shadow.Virtual.DeferredInvalidationBudget`; experimental "Prefiltered Distant" path for far casters (`r.Shadow.Virtual.PrefilteredDistant.ProjectEnable`); ubershader permutation cuts compile cost. [5.8 verified]
- **Fit:** runtime-edited geometry invalidates its VSM pages on edit; ok for occasional edits, costly if thousands of moving instances (traffic) are casters. Non-Nanite geometry in VSM is slower (per-draw). Aerial zoom: huge cascades, pages budget limits matter.
- **Cost:** 2-5 ms on a 3060 in dense city; the likely largest shadow cost. Consider cascaded shadow maps (CSM) as fallback tier. [estimate]
- **Maturity:** production.
- **Verdict:** **adopt for v0**, with a CSM fallback and a "cars don't cast VSM at distance" rule.

### TSR / DLSS / FSR / frame generation
- **Realism/perf:** upscaling is mandatory for 1440p/60 on a 3060. TSR is built in; DLSS (NVIDIA plugin, Super Resolution and Ray Reconstruction) and FSR (AMD plugin) give better image quality/cost trade-offs. [established]
- **5.8:** TSR async pass overlaps later passes; early translucency velocity moved after cloud reconstruction (`r.Translucency.EarlyVelocityPass 2`); CombineLUTs on async compute. [5.8 verified]
- **Fit:** thin geometry (wires, fences, road markings, lane lines at distance) shimmers under any upscaler; check markings. Procedural geometry needs correct motion vectors (static meshes ok; moved runtime meshes need previous-transform data). Frame generation (DLSS 3 FG, FSR 3 FG) needs RTX 40-series for DLSS FG; a 3060 cannot use DLSS FG. FSR3 FG works on 3060 but adds latency; **avoid as baseline** and expose as an option.
- **Cost:** TSR at 50-67% screen percentage ~1-1.5 ms; DLSS Quality ~0.5-1 ms (estimate).
- **Maturity:** production.
- **Verdict:** **adopt for v0**: TSR baseline, DLSS Super Resolution test, FSR as fallback. Frame generation later/optional.

### Runtime Virtual Texturing (RVT)
- **Realism:** blends road surfaces into terrain (and terrain to road) via shared virtual texture; bakes decals, dirt and wear once.
- **Fit:** RVT works with Landscape and with meshes that write to it. Runtime road meshes can output to an RVT volume; edits re-render affected pages. This is the cleanest route to the planning-log requirement "terrain hidden under the road corridor, seam never visible". 20 km maps need RVT volumes per world-partition region or a coarse low-res volume; budget the pool carefully. [established; 5.8 Mesh Terrain also advertises virtual textures]
- **Cost:** pool memory (e.g. 100-300 MB) plus ~0.5-1 ms for page updates when editing; sampling cost per terrain pixel. [estimate]
- **Maturity:** production (long-lived), though SVT/RVT have quirks with World Partition.
- **Verdict:** **adopt for v0** on terrain + road blend; test one 1 km RVT volume.

### Decals and mesh decals (road markings, cracks, wear)
- **Realism:** markings, patches, cracks, oil stains (oil stains are not visible in any reference photo opened; unverified), manholes break the "clean mesh" look.
- **Fit:** two approaches. (1) **Generated marking meshes** (thin strips at a small offset, or UV-mapped onto the road mesh using a marking atlas): deterministic, works with Nanite-less runtime roads, cheap, easy to LOD/cull; z-fighting solved with depth bias / polygon offset in material. (2) **Deferred decals** projected on roads: flexible for cracks/patches but cost scales with overlap and number; poor with Nanite VSM interplay; lots of decals are expensive. Recommended: markings as geometry (the sim already generates them from the lane graph), wear/cracks as a **material layer + RVT** and a few decals for hero spots. Mesh decals exist for Nanite-less overlays. 5.8 adds deferred decals on mobile only; no desktop change noted. [5.8 verified for mobile]
- **Cost:** low for geometry markings; deferred decals ~0.3-1 ms for a few hundred visible.
- **Maturity:** production.
- **Verdict:** **adopt for v0** (geometry markings + material wear). Decals only for exceptions.

### Material layering / Substrate
- **Realism:** Substrate gives layered, physically-based multi-lobe materials (clear coat, thin film, fuzz, SSS). Useful for car paint, wet roads, glass.
- **5.8:** reduced permutations; X-Rite AxF import; Substrate NPR/toon experimental. [5.8 verified]. Substrate left Experimental for Beta in 5.5 and was labelled production in 5.7 (my recollection; verify).
- **Fit:** works with procedural meshes as it is a material feature. Cost: heavier shader, bigger GBuffer; Substrate "Adaptive GBuffer" keeps cost down but is still above legacy materials on a 3060. Most city surfaces (brick, tarmac, render) need no Substrate. Wet road look can be done with Material Layers / parameter blending in legacy.
- **Cost:** +5-15% base pass for Substrate materials (estimate).
- **Maturity:** Beta/Production (confirm).
- **Verdict:** **later** (car paint, glass, wet roads). **Avoid** for v0 bulk surfaces. Use Material Layers/Layer Blend for v0.

### Landscape and terrain material techniques
- **Techniques:** auto-material by slope/height/curvature (landscape layer blend or world-space normal / height masks in the material), macro variation (large-scale low-frequency colour/roughness noise, ideally from an RVT or a macro texture), anti-tiling (stochastic / hex-tiling or texture bombing, Distance-blended triplanar for cliffs), height-based blend with parallax/displacement. [established]
- **Fit:** the planning log wants runtime sculpting and earthwork meshes. Landscape edits at runtime via the Landscape API are not a stock gameplay-time feature (Landscape Edit Layers are editor-only [established]). So: own the heightfield in the sim and render it as generated mesh tiles (or runtime-updated heightmap textures) with the auto-material on a custom material. Landscape's own layer system would then be unused.
- **5.8 Mesh Terrain (Experimental):** mesh-based terrain with overhangs and tunnels, non-destructive modifiers, PCG interop, World Partition/OFPA streaming, uses layering, virtual textures, variable tessellation, Nanite. Editor-authoring oriented; no runtime-edit evidence found. [5.8 verified for existence; fit unverified]
- **Cost:** a 4-6 layer auto-material with macro/anti-tile ~1.5-3 ms at 1440p (estimate).
- **Maturity:** Landscape production; Mesh Terrain experimental.
- **Verdict:** **adopt for v0** a custom terrain material on own mesh tiles (slope/height/macro/anti-tile). **Avoid** Mesh Terrain as a dependency; revisit if Epic documents runtime edits.

### Sky Atmosphere, Volumetric Clouds, Exponential Height Fog
- **Realism:** physically based sky, aerial perspective (key for 20 km views), cloud shadows, haze. Cheap and dramatic.
- **5.8:** new Fog Screen Space Scattering (Experimental) on EHF; MegaLights gains cloud shadows. Volumetric clouds and FSSS don't combine. [5.8 verified]
- **Fit:** independent of world geometry, fully compatible with runtime content, floating origin/large worlds fine. Aerial perspective is what makes an aerial city view read as large. Volumetric Fog (froxel) is scene-scale limited; keep distance short.
- **Cost:** Sky Atmosphere ~0.3-0.6 ms; Volumetric Clouds 1-2.5 ms at 1440p (quality-scaled, reconstruction); height fog trivial; volumetric fog ~0.5-1 ms. [estimate]
- **Maturity:** production (FSSS experimental).
- **Verdict:** **adopt for v0**. Clouds low quality or reduced; skip FSSS.

### Water (Water plugin vs custom)
- **Fit:** the Water plugin (rivers, lakes, ocean) is spline- and landscape-driven: water bodies carve into the Landscape via edit layers and read landscape height. With our own terrain meshes this carving does not apply [established, and my reading of the plugin; verify]. Water plugin still renders Single Layer Water material (Lumen reflections, translucent depth fog). Recommended: a custom water material (Single Layer Water shading model) on generated flat/mesh surfaces per body with the stored level (planning log: static levels, level per body), shoreline foam from depth difference or distance-to-terrain. Water plugin 5.8 changes seen only on mobile (higher-quality water). [5.8 verified for mobile]
- **Cost:** Single Layer Water ~1-2 ms for large visible areas; ocean-scale normal maps cheap.
- **Maturity:** Water plugin Beta for many versions; Single Layer Water production.
- **Verdict:** **adopt custom water** in v0 look test (one lake, one river bend); **avoid** the Water plugin unless terrain stays Landscape.

### PCG (scattering; runtime / GPU PCG)
- **Realism:** rules-based scattering of grass, shrubs, trees, litter, hedges, street furniture.
- **5.8:** manual editing of PCG outputs with data overrides; arrays/structs/maps as attributes; embedded subgraphs; **GPU runtime scatter comparable to landscape GPU grass**; actor-less runtime generation; runtime scheduler cuts game-thread cost up to 30%. Mesh Terrain interop. [5.8 verified]
- **Fit:** runtime generation consumes spatial data from the sim (parcel polygons, road splines) as PCG inputs; edits re-trigger regeneration of affected partitions only. Hierarchical generation (HiGen) matches 20 km. Our own sim already owns trees; use PCG as the instancing layer, not as the source of truth. Note PCG partitioning is still tied to APartitionActor (release notes). Vegetation is deferred in the plan, so only a minimum test is needed.
- **Cost:** GPU scatter makes per-frame cost small; shadow and overdraw of foliage dominate.
- **Maturity:** PCG production for editor; runtime GPU generation newer (check labels).
- **Verdict:** **later** for vegetation; **adopt in v0 look test** only for ground clutter/grass on terrain tiles.

### Procedural Vegetation Editor (PVE)
- **Realism:** grows biologically plausible Nanite-ready trees/shrubs from parameters; 5.8 adds growth nodes, seed generation, skeletal extraction, foliage grafting, trunk material compositor. [5.8 verified: Experimental]
- **Fit:** produces static meshes at author time (not runtime); suits a tree library of 20-50 species/variants scattered by PCG. Experimental: pipeline may change, Nanite foliage cost needs checking.
- **Cost:** per-tree Nanite foliage cost; with LODs (impostors/billboards for far trees) needed for 20 km views.
- **Maturity:** Experimental.
- **Verdict:** **later** (vegetation milestone). In v0 use a handful of Fab trees to test cost.

### MegaLights / many lights
- **Realism:** stochastic direct lighting with hundreds of shadowed lights (street lamps, headlights, windows at night).
- **5.8:** **Production Ready**; reduced noise, 60 fps focus, subsurface transmission, froxel translucency, IES for volumetrics/translucency, lighting channels, cloud shadows, light finder / debug views; early light culling cuts sampling ~20% (Firefly), ~0.3-1 ms console savings. [5.8 verified]
- **Fit:** works with Nanite/VSM; dynamic geometry traces against the Nanite/ray-tracing scene, so shadow casting of runtime non-Nanite meshes needs hardware RT or VSM-based shadowing (MegaLights default uses VSM-less ray tracing; verify which scene it sees for runtime meshes). Hardware RT strongly preferred; a 3060 can run it but at a cost.
- **Cost:** ~2-4 ms on a 3060 at 1440p with mid counts, rising with lights and noise settings (estimate). Heavy for baseline.
- **Maturity:** Production Ready.
- **Verdict:** **later** (night). Not in daytime v0 test; add a night test once frame budget is known.

### Mass / instanced rendering for traffic
- **Fit:** the sim is separate and deterministic; the render side needs only instance transforms. Use ISM/HISM or Instanced Skinned Mesh with custom data (colour, wheel angle) driven by a compact array from the sim; avoid per-car Actors. Mass Entity gives an ECS with representation LOD (low-detail ISM at range, actors near), but duplicates the sim, so use only its **MassRepresentation/ISM batching**, or just use plain ISMs fed from our sim. 5.8: Mass Signals in core, off-thread entity creation, sparse fragments, new scheduler. [5.8 verified]
- **Cost:** ~10k ISM instances is fine; draw calls scale with distinct meshes/materials, not instance count. Nanite for cars with GPU-driven rendering is cheap; lights/glass are the cost.
- **Maturity:** ISM production; Mass production-ish (check; was Beta/Experimental historically).
- **Verdict:** **adopt for v0** plain ISM/Nanite instances from the sim buffer; **avoid** Mass as a dependency.

### World Partition + HLOD for generated content
- **Fit:** World Partition streams actors in cells from authored map files. Generated content has no authored actors; options: (a) a single persistent "streamer" subsystem that spawns/destroys generated tile actors by region (matches sim load/unload), HLOD omitted; (b) runtime HLOD-like impostors built by us (merged low-poly meshes per tile, cached on edit). Epic's HLOD build is an **editor/cook** step: it will not run on runtime edits [established]. 5.8 HLOD UX: batched deletion, standalone HLODs, "Build HLOD for Selected Region"; HLOD camera movement in the editor sped up ~136x on test maps. [5.8 verified]
- **Verdict:** **adopt for v0**: tile-streaming of generated actors with our own coarse LOD and large-world coordinates (LWC); **avoid** relying on stock HLOD for runtime-generated content. Still use World Partition for the static base map (terrain tiles baseline, authored sets).

### Post-process camera realism and "model city" look
- **Realism:** auto-exposure (histogram, with a fixed EV range to stop hunting between street and aerial), filmic tonemapper / ACES or the newer Unreal tonemapper, bloom, vignette, subtle chromatic aberration, film grain. Depth of field: Cinematic DOF with tilt-shift look (focus plane controlled by orbit distance, wide aperture) at aerial zoom gives the miniature effect cheaply. Also Accumulation DOF (5.8) is for offline/photo only. [5.8 verified for accumulation DoF]
- **Fit:** driven by camera distance and altitude: ramp DOF strength and saturation from street (off) to aerial (on). Cheap.
- **Cost:** DOF 0.5-1.5 ms; rest negligible.
- **Maturity:** production.
- **Verdict:** **adopt for v0** exposure + tonemap + tilt-shift DOF ramp tied to orbit altitude.

### Photo mode
- **Fit:** the plan puts street-level/photo cameras after v1. Photo mode can raise quality for stills: higher screen percentage, Movie Render Graph (Production Ready in 5.8) or high-res screenshot, Accumulation DOF, path-tracer option for stills (heavy, HWRT needed). No effect on baseline frame cost.
- **Verdict:** **later**.

## 2. What breaks with runtime-generated geometry, and workarounds

| Issue | Effect | Workaround |
|---|---|---|
| No mesh distance field / Lumen cards for runtime meshes | Software Lumen GI and reflections miss them or light them badly; distance-field shadows/AO absent | Use hardware RT Lumen; or bake generated tile meshes into UStaticMesh via editor-time pipeline for static parts; or test a runtime-DF plugin (unverified); keep road meshes thin and rely on sky light |
| Nanite build is offline | Runtime meshes render non-Nanite: no virtualised geometry, normal LOD needed | Keep Nanite for the static kit; chunk runtime meshes into sensible tiles with own LODs; evaluate a runtime Nanite plugin only if proven necessary |
| VSM page invalidation | Each edit invalidates pages under the edited tile | Edit in small tiles; batch edits; throttle with `DeferredInvalidationBudget` |
| HLOD / WP cell data is baked | New content never enters HLOD; stale proxies | Own streaming of generated tiles + own coarse LOD; regenerate per-tile impostors off-thread |
| Landscape edit layers editor-only; Water plugin tied to Landscape | No runtime sculpt through stock Landscape; water carving unavailable | Own terrain mesh tiles + custom water material |
| Motion vectors for moved/re-created meshes | TSR/DLSS ghosting after edits | Keep previous-frame transforms; static after generation |
| Thin markings under upscaling | Shimmering lane lines | Geometry markings with proper width, test TSR vs DLSS, avoid sub-pixel width at aerial range |
| Z-fighting of markings and decals | Flicker at distance | Depth bias in material, offset by LOD, or burn into road texture/RVT at far range |
| Draw-call and CPU cost of many distinct generated meshes | Game thread / render thread bound | Merge per tile; use ISM for repeats; limit unique materials |
| Shader and PSO compile hitches on first use | Stutter | PSO precaching (default in 5.x); warm common materials at load |

## 3. v0 look test checklist (prioritised, 12 items)
1. Pin UE 5.8; build a 1 km x 1 km test scene with generated road, kerb, earthwork, terrain tiles, 20 houses, 30 cars; fixed street and aerial camera bookmarks; record GPU ms per pass (`stat gpu`, Unreal Insights).
2. Lumen: software vs Lumen Lite vs HWRT on **runtime-generated** meshes; confirm whether they light and reflect; decide DF/HWRT strategy.
3. Upscaling: TSR vs DLSS Quality vs FSR at 1440p output; check shimmer on markings, wires and fences.
4. VSM vs CSM cost with Nanite houses plus non-Nanite runtime roads; test an edit hitch.
5. Terrain material: slope/height auto-layers, macro variation, anti-tiling, on own mesh tiles.
6. RVT road-terrain blend and wear layer; verify no seam at earthwork and corridor.
7. Road markings: geometry strips vs decals; test z-fighting and distant LOD.
8. Sky Atmosphere + aerial perspective + clouds at lowest acceptable quality; exposure range from street to 3 km altitude.
9. Tilt-shift DOF and saturation ramp tied to orbit altitude.
10. Water: custom single-layer water on one lake and one river; shoreline blend with terrain mesh.
11. Instanced traffic: 5k ISM cars with Nanite and per-instance colour; cost with and without shadows.
12. Vegetation probe: 5 Fab trees via PCG GPU scatter on one tile, Nanite foliage vs masked; record overdraw cost; night probe with MegaLights (street lamps) only if budget remains.

## 4. Risks
- 5.8 is the last UE5 release; Epic focus moves to UE6 (reported). Pin 5.8, avoid depending on experimental features (Mesh Terrain, PVE, FSSS) for core systems.
- Runtime geometry vs Lumen/Nanite is the main architectural risk; vendor plugins offering runtime DF/Nanite are unverified and add licence/maintenance risk.
- 3060 budget: Lumen + VSM + clouds + vegetation + traffic shadows can exceed 16.6 ms; plan a scalability ladder (Lumen Lite, CSM, lower cloud quality, DLSS Performance).
- 20 km RVT and VSM page memory on 12 GB (or 8 GB) VRAM cards.
- Stale figures: costs in this doc are estimates; Nanite/VSM 5.8 specifics came from sparse sources.
- Stock HLOD does not apply to generated content; our own LOD system is a real workstream.

## 5. Sources
- UE 5.8 release notes: https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-5-8-release-notes
- UE 5.8 announcement: https://www.unrealengine.com/en-US/news/unreal-engine-5-8-is-now-available (403 to fetch; seen in search snippets)
- 80.lv, 5.8 out today (17 Jun 2026): https://80.lv/articles/unreal-engine-5-8-is-out-today-with-big-optimization-improvements-and-mesh-terrain
- Tom Looman, 5.8 performance highlights: https://tomlooman.com/unreal-engine-5-8-performance-highlights/
- GameFromScratch, 5.8 released: https://gamefromscratch.com/unreal-engine-5-8-released/
- Digital Production, 5.8 Preview (14 May 2026): https://digitalproduction.com/2026/05/14/unreal-engine-5-8-preview-rolls-in/
- Epic Lumen docs: https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-global-illumination-and-reflections-in-unreal-engine
- Realtime Mesh Component Pro (vendor claims, unverified): https://www.fab.com/listings/c04e6977-a3bd-4e24-b0a2-0b696bee90da
- Established-behaviour items (Nanite, VSM, RVT, Water, Substrate, HLOD docs): https://dev.epicgames.com/documentation/unreal-engine/nanite-virtualized-geometry-in-unreal-engine and sibling pages on the same site; not re-fetched in this pass.
