# Performance plan: Unreal 5.8, runtime-generated roads and terrain

Research note, 10 Oct 2026. Targets (planning-log s1-2): RTX 3060 class, 1440p/60 with upscaling, 20 km+ maps, free orbit camera, runtime-edited geometry from sim_core. Companion to `docs/unreal-photorealism.md` (feature verdicts) and `docs/vegetation-and-surface-realism.md`.

**Confidence key.** [V] verified: primary source (Epic docs, release notes, developer's own capture or patch notes) fetched 10 Oct 2026. [U] unverified: blog, forum, vendor claim, search snippet, or my inference. [E] estimate: my number, to be replaced by measurement. Many Epic doc pages returned empty bodies to the fetch tool (CSV Profiler, Dynamic Mesh Components, RVT detail), so profiling commands below are from long-standing UE5 practice and are [U] until run on 5.8. No source I found measures a 3060 for any pass; every ms figure in this file is [E].

Current code state: `CPRoadMeshActor` uses `UProceduralMeshComponent`, one per 256 m tile, one section per surface class. That is the baseline everything below is compared against.

## Decisions this implies now

1. **Mesh component: keep ProceduralMeshComponent only for the look test; plan a switch to Realtime Mesh Component (Core) or a thin custom component** once the day-one test (item 3 below) shows its draw path costs. Do not build features on PMC-specific API; wrap it behind one `ITileRenderer` interface in the plugin.
2. **Render sections per tile: at most 2 materials per tile.** Move surface class into vertex data (vertex colour A is currently reserved: use it, or UV1.y, for a surface id) and use one road master material with a texture array or atlas for asphalt, paving, kerb, verge. Markings: a second material. Cuts draws from ~6 sections to 2 per tile.
3. **Tile sizes:** keep 256 m as the sim and edit unit. Render component = 1 tile near; far field = 1 km super-tiles built from a coarse chord (0.5 m) LOD and a separate low-poly mesh. Do not draw individual 256 m tiles beyond ~1.5 km.
4. **Bake to texture at distance:** roads and markings beyond ~400 to 600 m are not geometry; they are painted into a terrain colour texture (RVT or own far-field texture). Needs the sim to expose a top-down raster of surface class and markings per tile. Decide this before the marking mesh code grows.
5. **Terrain is a custom height-texture clipmap, not Landscape and not Mesh Terrain.** 0.5 m only in sparse corridor tiles; clipmap rings elsewhere (arithmetic in s4).
6. **Lighting tier plan:** baseline = Lumen Lite or software Lumen for terrain/buildings with roads as non-contributing thin surfaces, plus VSM; road tiles are not required to appear in the GI scene. Hardware RT Lumen is a quality tier restricted to a ~200 to 400 m radius.
7. **Shadows:** only tiles within ~300 m cast into VSM (roads are flat; their shadows are nearly invisible); kerbs and light poles cast. Cars cast only inside ~150 m.
8. **Edits are time-sliced:** a tile rebuild must run off the game thread, double-buffered, and swap in one frame; target under 5 ms game-thread per swap.
9. **Run all benchmarks in a packaged Development build**, not PIE (s10). Editor-only code paths (distance field build, Nanite build) behave differently from packaged.

## 1. How comparable games handle it

- **Cities: Skylines II (Unity HDRP, custom BatchRendererGroup renderer).** Primary evidence is one developer's RenderDoc analysis of v1.0.11f1 at ~1000 inhabitants [V, own capture]: 6,705 draws per frame, 72% in shadow maps; shadows ~40 ms of a ~88 ms captured frame; four 2048 cascades, every object a caster at every setting, shadow quality setting not wired up; ~121M input vertices, ~36M rasterised triangles; citizens ~56K vertices each (teeth alone 6,108, 13 sets drawn with zero pixels); props with 17K to 100K+ vertices and no LODs; frustum culling only, no occlusion culling; 6.7 GB VRAM (3.9 GB textures). Fixes shipped: LOD independent of render resolution and spawn stutter fixes (1.0.11f1); shadow LOD calculation, small-after-large draw order, smaller character textures, fewer virtual-texture exhaustions from trees and alpha-clipped objects (1.0.12f1) [V, patch-note summaries via Shacknews/SixthAxis]; tree LOD quality work still being done Aug 2026 [V, Paradox post via search summary]. Lessons: (a) shadow casters dominate; cull by size and distance. (b) Vertex count per object must follow screen footprint. (c) Merging props into building meshes saved draws but killed per-prop culling. (d) Motion vectors must be right or DLSS/FSR cannot ship.
- **Cities: Skylines 1:** no primary performance analysis found. [U]
- **Manor Lords (UE5, solo dev):** Epic interview covers workflow only; January 2026 performance gains (2-5 to 30-40 fps on a 3,000-resident, 2,000-sheep save) came from CPU simulation work [V via news summary]. Lesson: with a big world, the sim, not the renderer, often limits. No rendering talk found.
- **Satisfactory (UE):** no talk on instancing or lightweight buildables found; the only claim (all buildings instanced and recolourable) is a staff LinkedIn entry [U]. UE5 move brought World Partition load-splitting and PSO caching [V via Shacknews]. Lesson: keep to a few mesh+material pairs and instance them.
- **Transport Fever 2, Anno 1800/117, Planet Coaster, Workers & Resources, SimCity 2013:** no rendering post-mortems found in this pass. Transport Fever's early release was criticised for missing LOD [U, forum comment]; Anno 117 has a GDC 2026 ray tracing talk (listing seen, not read) [U]. Treat as a gap.
- **UE runtime-terrain plugins:** only vendor listings found [U].

Net: the only hard evidence says "LOD, shadow casters, draw count". Design the sim output around those three.

## 2. Runtime mesh strategy in UE 5.8

| Option | Draw path | Lumen / DF / Nanite | Edit cost | Notes |
|---|---|---|---|---|
| ProceduralMeshComponent | One anecdote reports the dynamic base pass path (overdraw after the static pass) [U, forum]; one draw per section | none; supports RT | full section re-upload; CPU collision optional | Epic steers new work to Geometry Script [U] |
| DynamicMeshComponent | Same class of path; has fast-update modes [U] | Geometry Script guide: no Nanite, Lumen, mesh distance field, LOD, instancing; supports ray tracing [V, quoted in the vegetation doc] | cheap partial updates | Good for editor-style edits, heavier than needed |
| Custom `UPrimitiveComponent` + scene proxy | Static draw commands cached (MDC cache) if built with `DrawStaticElements` style; you own vertex buffers | none unless you add RT geometry | you control it: write buffers straight from sim `MeshBuffers` | Most work; plugin already C++ [U] |
| Realtime Mesh Component Core | Vendor says it can use the static path and handles LODs, collision [U] | Pro tier claims runtime Nanite, SDF and Lumen cards, 5.5 to 5.8 [U vendor] | designed for runtime updates | Check licence and whether Core is free; Pro adds risk |
| `UStaticMesh` built at runtime from `UStaticMeshDescription` | normal static mesh: cached MDC, instancing-eligible | works in packaged shipping builds per a community write-up [U]; Build is async; distance field queue exists in engine API (`FDistanceFieldAsyncQueue`, async, but `BlockUntilBuildComplete` can stall) [V, API page]; whether DF/card/Nanite data are generated in a packaged build is **not documented** (I infer the builders are editor-only) [U] | rebuild whole mesh; GPU upload once | Candidate for stable, rarely edited far tiles |

Recommendations [U]: start with a custom or RMC static path for road tiles, because roads are mostly static and edited rarely; static draw caching only pays if the tile does not change per frame. Test `UStaticMesh` runtime build as the second candidate for far super-tiles. Do not pay for Nanite on roads: no runtime route is verified, and flat low-poly roads gain little. Collision: use a separate coarse physics mesh or analytic height queries from sim_core; avoid per-triangle PMC collision cooking at edit time. Ray tracing: BLAS build per tile is a one-off GPU cost on edit; refit cost does not apply (static). RT-visible tiles only inside the radius (`r.RayTracing.Culling.Radius`, default 200 m [V, Lumen doc]).

Tile/section guidance: 256 m tile, 2 sections (road master, markings), 10 to 40K triangles near LOD. Use one component per tile with up to 3 LODs where the engine supports it; otherwise swap component meshes by distance.

## 3. LOD and distance for generated content

- Geometry LOD chain from `chord_tolerance`: 0.02 near (to ~150 m), 0.1 mid (to ~500 m), 0.5 far (to ~1.5 km) [E: distances]. Build all three per tile on a worker, upload the nearest first, refine later (placeholder LOD first).
- Markings: geometry strips near (to ~200 m), then baked into the surface texture. Sub-pixel lines shimmer under any upscaler, so beyond the width where a line is under ~1.5 px of internal resolution, fade the strip and show the texture (see photorealism doc, s2).
- Beyond ~600 m: no road geometry; the terrain clipmap's colour texture carries a baked road raster. Beyond ~3 km: terrain colour and aerial perspective only. [E: thresholds; tune in look test.]
- Culling: frustum plus distance is enough for roads. HZB occlusion is nearly useless from a top-down camera over a flat city, useful at street level among buildings; leave stock occlusion on and do not build your own. CS2's lack of occlusion culling hurt dense scenes [V], but its fix emphasis was LOD and shadows.
- Streaming and hysteresis: orbit cameras oscillate in distance; use two thresholds per LOD (enter at d, leave at 1.25 d), a minimum dwell of ~1 s, and a budget on tiles promoted per frame. Keep 20 km worth of far-field data resident (it is small; s4), stream only the 2 km near ring.

## 4. Terrain

Options: (a) custom component with clipmap rings and height textures; (b) the Virtual Heightfield Mesh plugin driven from an RVT; (c) Landscape with runtime render-target patching; (d) 5.8 Mesh Terrain (Experimental, editor-oriented, no runtime-edit evidence) [V for existence]. Virtual Heightfield Mesh is Experimental with early-UE5 rendering bug reports [U]; Landscape edit layers are editor-only [E from photorealism doc]. **Recommend (a)**: concentric 1024x1024 or 2048x2048 vertex grids per ring, vertex shader samples a height texture (R32F or R16 plus tile offset), snap rings to texel grid, geomorph in the last 20% of each ring to hide popping. Compute normals in the shader from neighbouring heights (cheap) or store a normal texture per ring updated on edit.

Memory arithmetic (20 km = 20,000 m):

| Grid | Samples | R16 | R32F |
|---|---|---|---|
| 0.5 m everywhere | 40,000^2 = 1.6 G | 3.2 GB | 6.4 GB: impossible |
| 2 m | 10,000^2 = 100 M | 200 MB | 400 MB |
| 4 m | 5,000^2 = 25 M | 50 MB | 100 MB |
| 8 m | 2,500^2 = 6.25 M | 12.5 MB | 25 MB |
| Clipmap, 6 rings of 1024^2 (0.5, 1, 2, 4, 8, 16 m) | 6.3 M | 12.6 MB | 25 MB |
| 0.5 m corridor tiles: 100 m wide x 1 km = 0.1 km^2 | 400K per km | 0.8 MB per km | 1.6 MB per km |

So: base heights at 4 m for the whole map (50 MB R16, but 16 bits over 2 km relief is 3 cm: use R32F or per-tile min/max), 0.5 m only in sparse corridor tiles and sculpt tiles on the CPU/sim side (1,000 km of road = ~800 MB R16, mostly cold, streamed), and GPU holds only the clipmap rings (~25 MB). Edit cost: a regrade writes a small texture region (partial `UpdateTexture2D`/`FUpdateTextureRegion2D`, or render-target draw), typically KB to MB; negligible if you update only touched texels. Collision: CPU-side height query from sim_core (already owns the data); no physics mesh needed for terrain. Colour: terrain material plus a virtual texture or a far-field colour texture (s3). Tessellation alternatives are unnecessary: a 0.5 m ring plus normal map is enough, and Nanite tessellation does not apply to a non-Nanite mesh.

## 5. Lighting and shadows with runtime geometry

- **Lumen software** needs mesh distance fields and cards; per Epic only Static Meshes, ISM, HISM and Landscape appear in the software Lumen scene [V, Lumen technical doc]. Procedural meshes are not discussed [V]. So runtime tiles do not contribute to software GI. Software RT range is 200 m default, up to 800 m, then only screen traces [V].
- **Hardware RT Lumen** traces real triangles, supports dynamic geometry, costs more; no 3060 ms figures found (one anecdote: ~10 fps worse than software on a 3060 at 1080p in Oblivion Remastered [U]). Far Field exists but needs World Partition HLOD1 [V], so it does not apply here. Quantification is a day-one measurement.
- **Lumen Lite** (Beta, `sg.GlobalIlluminationQuality 1`, `sg.ReflectionQuality 1`): ~2x faster than High [V, Looman 5.8 notes]. Epic says High targets 60 fps on PS5 and Epic scalability costs ~8 ms at 1080p internal on consoles [V]. Likely the baseline tier.
- **Roads need little GI**: thin, dark, flat, lit by sky and sun. Practical stack: sky light (real-time capture) plus GTAO plus Lumen on buildings/terrain; roads receive GI, they do not need to give it. A heightfield sky-occlusion texture baked on the CPU from the terrain (AO from horizon angles per 2 m texel) is cheap and fits our own heightfield; whether Lumen can trace a custom heightfield is not documented, assume not [U]. Capsule shadows add nothing here.
- **VSM**: moving or changed casters invalidate overlapping pages; WPO/PDO materials invalidate every frame; per-primitive "Shadow Cache Invalidation Behavior" (Auto/Always/Rigid/Static) suppresses it; non-Nanite geometry is much more expensive in VSM, give it LOD chains and use `r.Shadow.RadiusThreshold`; default clipmap levels 6 to 22 (64 cm to 40 km), `r.Shadow.Virtual.Clipmap.FirstLevel/LastLevel` trim them; page pool `r.Shadow.Virtual.MaxPhysicalPages`; overflow corrupts shadows [V, Epic VSM doc]. For an orbit camera: set LastLevel to cover ~3 to 5 km, accept fade beyond; if VSM is still above ~3 ms on a 3060 with non-Nanite roads, switch to CSM (3 cascades to 2 km) with distance fade as the low tier [E]. Contact shadows: cheap, keep for street level.
- MegaLights: fine at night, requires hardware RT scene; ray-traced shadows add no per-light cost; VSM per light is costly; no fixed light cap, constraint is per-pixel sample budget; Lumen HWRT shares scene overhead [V, MegaLights doc]. Street lights: emissive-only beyond ~150 m, real lights inside, narrow attenuation.

## 6. Instancing at scale

- ISM vs HISM: advice online says modern ISM culls per instance on the GPU Scene path and HISM only pays for large static sets; HISM add/remove at runtime is historically buggy [U, forums]. 5.8 adds optional hierarchical CPU instance culling for non-Nanite ISMs (`r.SceneCulling.HierarchicalCPUCulling`) and reserved GPU Scene resources to cut resize hitches [V, Looman]. Use ISM for kerb modules, lights, cars; per-instance custom data for car colour/wheel angle.
- Batch updates: use `BatchUpdateInstancesTransforms` and mark render state dirty once [V, API doc advice]. Thousands of moving instances per frame is workable on the GPU Scene path but each update uploads the dirty range; for cars, update only instances inside ~500 m and snap far cars to coarse positions at lower rate [E].
- Ceilings [E, to measure]: ~50K static ISM instances per component fine; ~5K to 10K moving instances per frame fine. Draw calls scale with distinct mesh+material, not instance count.
- Kerbs: prefer baked into the road tile mesh as geometry (sim already does) rather than instanced modules; use instancing for parapets and posts.

## 7. Vegetation and grass

- PCG GPU runtime scatter (5.8) is "comparable to landscape GPU grass" and runtime PCG scheduler game-thread cost fell up to 30% [V, 5.8 notes]. PCG partitions still depend on actor machinery [V, photorealism doc]. Custom-terrain scatter must take heights from our heightfield; use PCG with a sampler fed by the sim's tile data, or write a small compute scatter. Test early.
- Nanite foliage vs masked cards: Nanite instances handle thousands of trees cheaply, masked overdraw is the danger; 5.8 adds "Nanite Pixel Programmable Distance" [V]. Wind WPO invalidates VSM pages every frame [V]: set Cache Invalidation to Static for distant trees, disable WPO beyond ~100 m, and exclude small plants from shadow casting.
- Density LOD by camera height: grass only below ~30 m camera height; shrubs below ~150 m; trees as instances with impostor billboards beyond ~500 m [E].

## 8. CPU side

- Threading: run sim_core mesh builds in `UE::Tasks::Launch` / `ParallelFor` workers, one task per dirty tile; sim_core is deterministic and thread-safe per tile (it builds from a const graph) [U: verify immutability of `RoadGraph` during a build; take a snapshot or version]. Game thread only swaps pointers.
- Buffers: sim_core `MeshBuffers` are float SoA; allocate into the final upload layout directly (position 12 B, normal+tangent packed, UVs half, colour 4 B) to avoid a second copy. Pass ownership to the render thread via `TSharedPtr` and `ENQUEUE_RENDER_COMMAND` / `RHIAsyncCreateBuffer`; avoid `TArray` copies of 1M-vertex scratch.
- Hitch avoidance: rebuild order near-to-far, at most N tiles per frame, a time budget (~3 ms game thread), the old tile stays until the new one is ready, build coarse LOD first. Edit preview during drag uses a cheap proxy mesh.
- Shaders/PSOs: use one road master material with parameters (no runtime-created material instances per tile; use `UMaterialInstanceDynamic` pooled or MPC). PSO precache is default; 5.8 reduced permutations and fails gracefully when shader data is missing [V, Looman]; D3D12 allocators prebuild overflow pools off-thread [V]. Warm the road, terrain and car materials at load behind a loading screen.
- GC: components are UObjects; hundreds of tile components are fine, tens of thousands are not [E]. Pool tile components; a 20 km map at 256 m is 6,100 tiles, so far tiles must be merged or non-UObject (render-proxy-only) [E].

## 9. Upscaling and frame budget (1440p output, 60 fps, 3060)

Internal resolutions at 1440p output [established ratios]: Quality 67% = 1715x960; Balanced 58% = 1485x835; Performance 50% = 1280x720; Ultra Performance 33% = 960x540.

16.6 ms GPU budget at Quality (all [E], to replace with `stat gpu`):

| Pass | ms |
|---|---|
| Pre-pass + base pass (terrain, roads, buildings, vegetation) | 3.5 |
| Nanite | 1.0 |
| Shadows (VSM) | 2.5 |
| Lumen GI + reflections (Lite 1.5 to 2; High 3.5) | 2.5 |
| Sky, fog, volumetric clouds (low) | 1.2 |
| Translucency, water | 0.5 |
| Post (AO, bloom, DOF, tonemap) | 1.2 |
| Upscaler (TSR or DLSS) | 1.0 |
| Headroom (thermal, spikes) | 2.5 |
| **Total** | **15.9** |

CPU: game thread ~6 ms, render thread ~8 ms, both under 16.6 ms and parallel. Drop order when over budget: (1) cloud quality, (2) VSM to CSM, (3) Lumen High to Lite, (4) vegetation density and shadow casting, (5) internal resolution to Balanced then Performance, (6) cars cast no shadows, (7) reflection quality.

Upscalers: TSR built in. NVIDIA DLSS 4.5 plugin for 5.8 exists (v8.7.2, ~21 Jul 2026), with Super Resolution, DLAA, Frame Generation, Multi Frame Generation, Ray Reconstruction [V via search summaries of PCGH and NVIDIA forum]; DLSS Frame Generation needs RTX 40 series, Multi Frame Generation RTX 50 [U, general knowledge]. The 3060 cannot use NVIDIA frame generation; the 4060 dev laptop can, so never include FG in benchmark baselines. FSR plugin for 5.8 status conflicts between sources (Redstone plugin for 5.8 reported by one outlet; AMD page still at 5.7 per PCGH) [U]; XeSS 3.1 lists 5.8 support [U]. Motion vectors for replaced tiles: keep geometry static between edits and ensure `r.Velocity` output works for custom proxies (CS2 shipped without correct motion vectors and could not support DLSS/FSR [V]).

## 10. Measuring

Workflow [commands are long-standing UE5 practice, [U] on 5.8 until run]:
- Overview: `stat unit` (frame/game/draw/GPU), `stat unitgraph`, `stat fps`.
- GPU: `stat gpu`, one-shot `ProfileGPU`; `r.RDG.AsyncCompute 0` while profiling per pass [V, VSM doc].
- Draws/primitives: `stat rhi`, `stat scenerendering`, `stat initviews`; `r.ScreenPercentage 67` to fix internal resolution; `r.SetRes 2560x1440f`.
- Views: `viewmode` shader complexity/overdraw; `r.Shadow.Virtual.Visualize cache|mip|naniteoverdraw`, `ShowFlag.VisualizeShadowCasters`, `r.ShaderPrintEnable 1` + `r.Shadow.Virtual.ShowStats 2` [V, VSM doc]; `trace.enable counters, vsm` or `CsvCategory VSM enable` [V]; Lumen view modes Lumen Overview and Surface Cache [V].
- Insights: launch `-trace=default,cpu,gpu,frame,bookmark,task,memory,counters -tracefile=<path>.utrace`; open in UnrealInsights for timing, memory, task graph [trace channel option syntax V via search snippets, exact channel set U].
- CSV: `csvprofile start` / `csvprofile stop`, then PerfReportTool on the .csv for HTML/graphs [U, via secondary guides]. 5.8 notes mention Horde "Automated Performance Tests (APT)" and the `AutomatedPerfTesting` plugin has `TryStartCSVProfiler` [V, API listing].
- External: RenderDoc (CS2 analysis used it [V]), NVIDIA Nsight Graphics, PIX; RenderDoc inflates times, use for draw structure not ms.
- Repeatable benchmark: a fixed camera spline (Level Sequence or a plugin actor), same seed save, warm shaders for 30 s, capture 1,000 frames after a 5 s settle, 3 runs, median. Unattended: launch the packaged exe with `-ExecCmds="csvprofile start; ..."` plus a quit timer, or an automation test via `-ExecCmds="Automation RunTests <name>; Quit"` [U]; an AI coordinator reads the CSV/summary file and a one-line JSON the camera actor writes at the end. Editor Python can call `unreal.SystemLibrary.execute_console_command` and `AutomationLibrary` screenshot helpers [U].
- Emulate a 3060 on a 4060 laptop honestly: the 4060 laptop is roughly comparable in raster speed to a desktop 3060 but has 8 GB vs 12 GB and a power limit [U, inference]. Steps: AC power, "best performance" mode, force the NVIDIA GPU for `UnrealEditor.exe`/game exe in Windows Graphics settings (and `-graphicsadapter=<n>` in the command line); log GPU clocks/temp with `nvidia-smi --query-gpu=clocks.gr,temperature.gpu,power.draw --format=csv -l 1`; discard runs where clocks drop. Treat results as 3060-class only with a 10 to 20% margin [E]. 8 GB VRAM is stricter than the 3060's 12 GB, so VRAM behaviour is a conservative test. Do not test DLSS FG as a 3060 feature.

## 11. Budgets for sim_core output [E, design ceilings until measured]

| Item | Ceiling |
|---|---|
| Road triangles per visible km, LOD0 (<=150 m, tol 0.02) | 20K |
| LOD1 (to 500 m, tol 0.1) | 6K |
| LOD2 (to 1.5 km, tol 0.5) | 1.5K |
| Road triangles total on screen | 400K |
| Draw calls per frame (main view, excluding shadow) | 1,500 (CS2 had 6,705 with 72% in shadows [V]) |
| Visible near tiles (256 m, within 1 km) | ~60; far super-tiles 40 |
| Materials per tile | 2 |
| Shadow-casting draws | 1,000 |
| Instances on screen: cars | 5K full, 20K coarse; trees 50K; lights 200 real, rest emissive |
| Texture memory | 3 GB at 8 GB cards, 5 GB at 12 GB |
| Terrain height textures on GPU | 25 MB; colour RVT/far-field 256 to 512 MB |
| VSM pool | 4,096 pages ≈ 256 MB (128x128 x 4 B x 4,096) |
| Lumen + RT scene | 0.8 to 1.5 GB |
| Render targets at 1440p | 0.6 GB |
| VRAM total | 7 GB used on 8 GB card (leave 1 GB), 10.5 GB on 12 GB |

Reasoning: draw calls and shadow casters are the only quantities with shipped-game evidence; triangle ceilings follow from the existing mesh density (a 7 m road at 0.02 tolerance is a few thousand triangles per km on straights, tens of thousands in curves; verify with `cp_export_mesh`).

## Measure on day one (priority order)

1. Packaged Development build with a fixed camera path; baseline `stat unit`, `stat gpu`, `stat rhi` on the current PMC tile scene.
2. Same scene: draw calls and base-pass ms with 6 sections vs 2 sections per tile (item 2 of decisions).
3. PMC vs RMC Core vs `UStaticMesh` runtime build for one tile: base-pass ms, draw calls, upload time; check whether DF/Lumen cards appear in a packaged build (Lumen "Surface Cache" view).
4. Lumen: software High vs Lumen Lite vs HWRT on the road scene, `stat gpu` (set `sg.GlobalIlluminationQuality`, `sg.ReflectionQuality`); record BLAS build time on a tile edit.
5. VSM vs CSM: `stat gpu` shadow ms; `r.Shadow.Virtual.Visualize cache` after a tile edit; test road tiles as non-casters.
6. Edit hitch: edit a road, record the longest frame; `-trace=default,cpu,task` Insights view.
7. Upscalers: TSR vs DLSS Quality at 67%, marking shimmer, motion vectors on swapped tiles.
8. Terrain clipmap prototype: ring count, frame ms, height update cost with a partial texture write.
9. Orbit sweep street to 3 km: `r.ScreenPercentage 67`, draw count and tile count versus altitude.
10. 5K ISM cars with per-instance data: `BatchUpdateInstancesTransforms` cost per frame.
11. VRAM: `stat rhi`/`stat memory` and Insights memory trace on an 8 GB card at 20 km camera.
12. Sustained 10-minute run on the laptop with clock/temperature logging to bound throttling.

## Open questions only measurement can answer

- Does the packaged build ever generate distance fields or Lumen cards for runtime `UStaticMesh`? If yes, software Lumen could cover roads.
- HWRT Lumen cost and BLAS rebuild cost on a 3060/4060 with ~60 non-Nanite road tiles.
- Is VSM or CSM cheaper for non-Nanite, flat road geometry with an orbit camera to 5 km?
- Real cost of PMC's draw path vs a static-path component; does it matter at 2 sections per tile?
- Thresholds for baking roads and markings to texture (visible seams, shimmer).
- Whether Lumen Lite is stable enough (Beta) on thin street canyons.
- Whether a 4060 laptop result transfers to a 3060 within 15%.
- Game-thread cost of 6,100 tile components at 20 km and the pooling threshold.
- Whether custom-terrain GPU scatter (PCG or compute) holds frame time with VSM and WPO foliage.

## Sources (accessed 10 Oct 2026)

- Epic, Virtual Shadow Maps: https://dev.epicgames.com/documentation/en-us/unreal-engine/virtual-shadow-maps-in-unreal-engine [V]
- Epic, Lumen technical details: https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-technical-details-in-unreal-engine [V]
- Epic, MegaLights: https://dev.epicgames.com/documentation/en-us/unreal-engine/megalights-in-unreal-engine [V]
- Epic, Runtime Virtual Texturing overview (little detail): https://dev.epicgames.com/documentation/en-us/unreal-engine/virtual-texturing-in-unreal-engine [V, thin]
- Epic, UE 5.8 release notes: https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-5-8-release-notes [V, ~100K of 357K chars read]
- Tom Looman, 5.8 performance highlights: https://tomlooman.com/unreal-engine-5-8-performance-highlights/ [V secondary]
- Paavo Huhtala, CS2 RenderDoc analysis: https://blog.paavo.me/cities-skylines-2-performance/ and HotHardware summary https://hothardware.com/news/cities-skylines-2-autopsy [V own capture]
- CS2 patches: https://www.shacknews.com/article/137554/cities-skylines-2-patch-notes-oct-26 , https://shacknews.com/article/137653/cities-skylines-2-hotfix-1012f1-patch-notes , https://www.paradoxinteractive.com/games/cities-skylines-ii/news/autumn-breeze [summaries via search]
- Manor Lords optimisation news: https://ixbt.games/en/news/2026/01/12/sozdatel-manor-lords-v-20-raz-povysil-fps-optimizaciiu-smogli-ulucsit-blagodaria-kosmarnomu-soxraneniiu-ot-fanaty-igry.html [U]
- Satisfactory UE5 move: https://shacknews.com/article/136465/satisfactory-update-8-interview-unreal-engine-5 [U]
- Realtime Mesh Component Pro listing (vendor): https://www.fab.com/listings/c04e6977-a3bd-4e24-b0a2-0b696bee90da [U]
- Runtime static mesh generation: https://zomgmoz.tv/unreal/Generating-static-meshes-at-runtime-using-UStaticMeshDescription ; API pages for `FDistanceFieldAsyncQueue` and `UStaticMesh::Build` on dev.epicgames.com [U/V partial]
- Virtual Heightfield Mesh API: https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/VirtualHeightfieldMesh/UVirtualHeightfieldMeshComponent [V existence]
- ISM batch update API: https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UInstancedStaticMeshComponent/BatchUpdateInstancesTransforms ; forum threads on HISM/ISM [U]
- Trace/Insights: https://dev.epicgames.com/documentation/unreal-engine/unreal-insights-reference-in-unreal-engine-5 ; CSV profiler guides via https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/AutomatedPerfTesting/UAutomatedPerfTestControllerBase/TryStartCSVProfi- [partial]
- DLSS/FSR/XeSS status: https://forums.developer.nvidia.com/t/nvidia-dlss-unreal-engine-plugin-archive/377564 , https://www.purepc.pl/nowy-plugin-amd-fsr-dla-unreal-engine-5-8-wydany-wsrod-nowosci-m-in-fsr-4-1-1-oraz-fsr-ray-regeneration-1-2-0 , https://www.pcgameshardware.de/Grafikkarten-Grafikkarte-97980/News/XeSS-Plugin-3-1-Unreal-Engine-5-8-Support-1550221/ [U, search summaries]
- Lumen RTX 3060 anecdote: https://steamcommunity.com/app/2623190/discussions/0/604155112791766760 [U]
