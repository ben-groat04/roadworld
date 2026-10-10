# UE 5.8 feature sweep: what our docs missed or got wrong

Research note, 10 Oct 2026. Marks: [V] verified from a page I read (URL in Sources), [U] unverified or secondary (search snippet, vendor claim), [E] my estimate. Limits: the 5.8 release notes are ~357k chars and the fetch tool summarised about 300k of them; most Rendering/Nanite/Substrate/ZoneGraph/Python detail was NOT in what came back, so absence below means "not found", not "absent". The two other agents cover the PCG city sample and Mesh Terrain; I only mention where they connect.

## 1. Findings, ranked roughly by decision impact

### A. Runtime geometry: stock path still has no Nanite/Lumen cards; paid plugin does (updated 10 Oct 2026 after reading the vendor pages)
- **Price and licence [V triaxis.games/realtime-mesh pricing section]:** Core free, MIT ("Free for any project - commercial OK, no royalties"; source on GitHub). **Pro $49 one-time, sold on Fab.** Studio $249.99 one-time, "Everything in Pro", for studios with > $100K annual revenue, "Standard Fab license terms". FAQ: Pro is a one-time purchase that includes updates within its major version. Not stated on the pages I read: seats, Pro royalty, Pro source access, refunds. The Fab listings returned HTTP 403, so the Fab price itself is unread [U: a search summary said $49.99 on Fab; confirm at checkout]. The legacy Epic Marketplace page is stale.
- **Core vs Pro, Lumen cards and distance fields [V]:** the pricing section lists under Pro: "Lumen support (Card generation)" and "Distance Field support"; Studio is "Everything in Pro". Core is not credited with either. The site's feature section ("Efficient runtime Lumen card generation and accurate signed distance fields", "dynamic meshes light correctly under Lumen GI and hardware ray tracing") is a product-wide list; the pricing list is the tier split. Caveats: the changelog (v5.4.0, "Runs on unmodified UE 5.5-5.8") says "Lumen card generator: efficient runtime card generation so dynamic meshes light correctly under Lumen GI" and "Signed Distance Field generator: accurate runtime SDFs for both Lumen GI and hardware ray tracing" without naming a tier; its only Pro mention is v5.2.0 "Cleans up several parts of the core to lay foundation for pro". The GitHub README (Core) lists "GPU + CPU mesh->SDF generation with Lumen card support" and "a runtime Nanite builder" in its feature text, with Pro "available on Fab". The docs page `/realtime-mesh/docs/rendering/lumen-cards/` documents the API (`URealtimeMeshSDFLibrary::GenerateAndApplyCardsAsync`, `RealtimeMesh::SDF::GenerateCardRepresentationGPU/CPU`) and names no edition. The FAQ lists Pro extras as `RealtimeMeshDynamic` (DynamicMesh/Geometry Script interop), `RealtimeMeshComposable` and first-party support. A GitHub issue (#296, Feb 2025) answer "From Discord. Core version and GitHub don't have difference" only says GitHub Core equals Fab Core. **Conclusion: the pricing list says cards and DF are Pro; the Core README text is ambiguous. Still unresolved without inspecting the repo: grep it for `URealtimeMeshSDFLibrary`.** Runtime Nanite is in the features and the v5.4.0 changelog ("Runtime Nanite: generate Nanite-ready meshes from runtime / procedural data") but in no tier list; tier [U].
- **Lumen cards doc [V]:** "Generate Mesh Distance Fields enabled - Lumen consumes both the distance field and the cards"; "A card representation alone, with no distance field, is not enough for Lumen to light the mesh"; the GPU card path needs a real RHI (not -NullRHI).
- **Static draw path, LODs, collision, async [V]:** LODs and collision are Core ("LODs, sections and section groups, materials", "collision"); async collision updates are Core; static and dynamic section draw modes appear in the FAQ without a tier. Open GitHub issue #294 "Dynamic rendering path seems not to be working" [U: title only].
- **Free alternatives, evidence only:**
  1. *DynamicMeshComponent (stock, 5.5+)* [V forum, GabUECV, 26 Jul 2024, pre-release notes]: "Draw Path: Static Draw | Dynamic Draw", "Distance Field Mode: None | Async CPU Distance Field"; "In 'Dynamic Draw' mode, Lumen only works with Hardware raytracing in Hit Lighting mode", distance field not used then. This is the stock hardware-RT route and costs nothing. Caveat [V forum, Elioss44 Dec 2024; unrealenchilada Mar 2025]: with Generate Distance Fields on, packaged builds crash on 5.5.x ("crashing in FDistanceFieldSceneData::AsyncUpdate function when dereferencing a null pointer"); Elioss44 (Jun 2025, unverified, no Epic source) says DF support was deprecated in 5.6; Crazy_Mike (Jan 2026, own report) says re-adding it in a 5.7 subclass using only AlwaysLoadedMip fixed the packaged crash. No Epic staff reply in the thread. So hardware-RT Hit Lighting is the stock path; the software-Lumen DF path on DynamicMesh is unreliable.
  2. *Runtime `UStaticMesh`* [V gradientspace, UE 4.26 era, old]: `BuildFromMeshDescriptions()` "is not the same as the Editor-only ::Build() path" and "skips various complex steps that are too slow to do at runtime (eg build Distance Field Lighting data)". Static meshes get Lumen cards at cook time; I found no evidence that a packaged build generates distance fields or cards for a runtime-built UStaticMesh, and no 5.8 doc changing that [U]. Needs a packaged-build measurement before it is ruled in or out.
  3. *ProceduralMeshComponent:* a search summary cited a 2022 write-up that PMC has no runtime distance field generation [U, page not read]; hardware RT is the only route.
  4. *Geometry Script:* the Epic guide page returned no usable text in my fetch, so the earlier claim (dynamic meshes support ray tracing but not Nanite or Lumen cards) stays [U]. A forum post (BINKLINGS, 12 Aug 2025) says only static meshes can enable Nanite; no answer was visible.
  5. Another paid runtime option: "Lumen with Runtime Meshes" on Fab (AZLumenRuntime, per search summary; page 403, [U]); reportedly needs a hardware-RT GPU unless engine source is changed.
- **RMC on 5.8 in shipped or large projects, known problems:** none found. The vendor claims 5.5-5.8 on unmodified engines; I found no shipped-game report, no 5.8-specific issue, and no Lumen or Nanite bug among the GitHub issue titles seen [U]. Absence of reports is weak evidence. Packaged-vs-editor Lumen differences are a general risk (an unrelated forum report of emissive lighting working only in editor), so test packaged.
- **Why it matters:** the biggest open risk in `unreal-photorealism.md` s2 (software Lumen blind to runtime meshes). Free Core likely does NOT fix software Lumen; the free fix is hardware-RT Lumen, which matches the person's wish for a hardware-RT top tier.
- **Cost/risk:** Pro $49 one-time (Studio $249.99 only above $100K revenue); third-party maintenance; Core free.
- **Verdict:** Core: **trial** as ProceduralMeshComponent replacement [E]. Test order: (1) hardware-RT Lumen on DynamicMesh/PMC runtime tiles; (2) packaged-build runtime `UStaticMesh` DF/card check; (3) only then buy Pro if software Lumen on the 3060-class target is still needed. Person's decision 10 Oct: paying is "depends how much", not ruled out.
- No 5.8 notes item found for a new Epic runtime-static-mesh or runtime-Nanite path [U]. Nanite tessellation/displacement (programmable, material-driven) is the only official "modify Nanite at runtime" route, and only for existing Nanite meshes [U].

### B. Epic ships an MCP server with the engine (and our repo already assumes it)
See section 3. Short: Epic's "Unreal MCP" + "All Toolsets" is first-party, Experimental, in 5.8 [V]. The doc/HANDOFF setup is correct.

### C. Upscalers: plugins exist for 5.8; FSR 4 now runs ML path on RDNA3; our machine's GPU is a 4060
- AMD FSR plugin updated for 5.8: FSR 4.1.1, ML upscaling now on RX 7000, FSR3-style analytical fallback on older GPUs, FG 4.0.1, Ray Regeneration 1.2.0 [V gpuopen/gamegpu snippets]. A PCGH report said AMD's download page still showed 4.0.3a (5.2-5.7) in early Aug [U]; verify the package.
- NVIDIA DLSS plugin for 5.8 released July, version 8.7.2 (21 Jul 2026) [U, PCGH snippet]; which DLSS generation it exposes is unconfirmed. Intel XeSS 3.1 supports 5.8 [U].
- **Correction to `unreal-photorealism.md`:** it says "a 3060 cannot use DLSS FG". True, but the dev machine is an RTX 4060 (HANDOFF), which can; test FG there, but the 3060 target must not rely on it. TSR also gained Thin Geometry optimisations [V Looman], relevant to lane markings; Dynamic Resolution is now on for PC DX12/Vulkan [V Looman].
- **Verdict:** TSR baseline **adopt**; DLSS plugin **trial** week 1; FSR 4 **trial**; frame gen **later** (option only). No licence cost for the plugins (free downloads) [E].

### D. Release status and UE6
- Epic's roadmap post: no further official UE5 release planned after 5.8, a 5.9 kept in reserve; UE6 merges UE5 with UEFN technology, Verse for gameplay [U: search summaries; the Epic page returned 403]. A tweet claims 5.9 confirmed at Unreal Fest Seoul with "more MCP/AI integrations" [U, unverified, treat as rumour]. A late-2027 UE6 early-access date appears in one secondary write-up only [U].
- **Why it matters:** supports "pin 5.8"; keep the engine-agnostic `sim_core` precisely because UE6 C++ plugin compatibility is unknown. No decision change, but strengthens the architecture rule.

### E. Fast Geometry Streaming (Experimental)
- Epic's streaming path for static, non-gameplay assets in huge worlds, the Witcher 4 demo tech; 5.8 adds decal and light-component support [V Looman + search]. A crash report exists on 5.7.4 [U].
- **Fit:** targets baked/authored static content, not runtime-generated meshes. **Verdict: avoid** for generated tiles; **later** for the static kit. Connects to the World Partition decision (own tile streamer stands).

### F. Movie Render Graph is Production Ready in 5.8 [V]
- Use for repeatable reference renders and the "bang on the photos" comparison, with Accumulation DoF and a path-traced option for ground truth (needs HWRT; heavy on 4060). **Adopt** (week 1). Add to `unreal-photorealism.md` s3 photo-mode: it is not "later", it is the look-test measurement tool.

### G. Insights and diagnostics added in 5.8 [V Looman]
- Spatial Profiler (WorldStreaming channel) for streaming cells; UObject Count counter; trace annotations saved to a sidecar.ini; `snapshothitches -start` captures a trace plus screenshot on hitches; more RVT trace detail (`RenderFinalize`). **Adopt**: hitch snapshots are the right tool for edit hitches (VSM invalidation, RVT updates).

### H. Lumen, VSM, MegaLights details beyond the existing doc [V Looman]
- `r.Lumen.HeightFog 1` default; Lumen Lite is Beta, ~2x High, via `sg.GlobalIlluminationQuality 1` / `sg.ReflectionQuality 1`. Suggests removing `r.Lumen.ScreenProbeGather.IntegrateDownsampleFactor 2` from High (noise).
- VSM: `r.Shadow.Virtual.DeferredInvalidationBudget` (default infinite; set it) and `r.Nanite.VSMInvalidateOnLODDelta`.
- MegaLights: production; early culling ~20% fewer samples; `r.MegaLights.Supported 0` removes shader compile cost if unused: **set it to 0 for day-only v0 on a 16 GB machine**, saves compile time [E].
- Substrate: only a stencil-classification perf tweak is noted [V]; NPR/toon is Experimental [V]. A preview write-up calls Substrate "production-ready" but the official notes do not confirm [U]. Existing verdict (later) stands.

### I. PCG runtime (connects to the PCG agent)
- Runtime generation scheduler up to 30% cheaper on the game thread, new `pcg.RuntimeGeneration.TimeBetweenRuntimeGenSchedulerTicks` [V Looman]. PCG still depends on partition actors per our doc; leave to the PCG agent.

### J. Mass overhaul
- Mass Signals in core, entity creation off the game thread, sparse fragments, reworked scheduler [V Looman]; no maturity label found. ZoneGraph and Chaos Vehicles: nothing found for 5.8. Our "plain ISM from sim, avoid Mass" verdict stands; revisit only for pedestrians/crowd LOD. A MetaHuman crowd workflow built on Mass exists (Experimental) [U]; irrelevant to v0.

### K. Water, Niagara weather, Chaos Vehicles
- Water: only mobile changes found [V]. No 5.8 change to alter the custom-water verdict. Nothing found on Niagara weather. Not researched further.

### L. Build and editor tooling
- Compile-time item found: clang LTO link time reduced [V]; irrelevant on MSVC. Python gains Optional property support [V]. High-res screenshot tool has an "Open Screenshot Folder" button [V] (minor). No screenshot-comparison/automation change found in what I read [U].
- For 16 GB RAM: the dependable reducers are ours (`-MaxParallelActions`, DDC on C:, KNOWN_RISKS), not new engine features. `r.MegaLights.Supported 0` and fewer shader permutations (Substrate off) help shader compile [E].

## 2. Driving the editor from Claude: MCP options for 5.8

| | Epic Unreal MCP | VibeUE | UE-MCP (db-lyon) | UnrealMCP++ |
|---|---|---|---|---|
| Maker | Epic [V] | kevinpbuckley (GitHub); page names no author [V] | db-lyon; independent, not Epic [V] | EdwardKenwayl, 3 commits, 0 stars [V] |
| Price/licence | Ships with engine [V] | Free, MIT; donation link [V] | Free, MIT [V] | Not stated [V] |
| Install | Edit > Plugins: **Unreal MCP + All Toolsets** (Toolset Registry auto), restart; Auto Start Server is OFF by default [V] | Needs Epic's MCP + Editor Tools; git clone into `Plugins/`, run `BuildAndLaunchGame.ps1` (compiles C++; slow on our machine), enable, restart [V] | `npx ue-mcp init`; Node 18+; deploys a bridge plugin [V] | Copy to `Plugins/`, compile; needs Python Editor Script plugin [V] |
| Versions | 5.8 [V] | 5.8+ [V] | 5.4-5.8; wraps Epic's 830 tools on 5.8 [V] | 5.8, Windows [V] |
| Transport | HTTP+SSE at `http://127.0.0.1:8000/mcp`; no stdio/WebSocket [V] | Rides Epic's endpoint [V] | stdio to Node, then WebSocket to editor [V] | Rides Epic's endpoint [V] |
| Can do | SceneTools, ActorTools, MaterialInstanceTools, ObjectTools; spawn actors, lighting, material instances, automation tests, Slate inspection. Docs do NOT say whether Python exec, screenshots, logs or builds exist [V] | 943 tools/33 toolsets: Blueprints, materials, landscape, foliage, level actors, Niagara, profiling, Fab import, `execute_python_code` [V/U]; 86 lazy skills | Blueprints, materials with HLSL and preview PNG, PCG graph create/wire/execute, Python, console, PIE, screenshots, headless build, Live Coding [V] | Blueprint graph read/write, properties, Landscape, World Partition, spline roads, HISM, console/CVars, CsvProfile [V] |
| Maturity | Experimental; APIs may change [V] | 731 stars, 1,353 commits, no releases, 3 open issues [V]; young | Unknown track record [U] | Experimental, minimal [V] |
| Safety | No auth, loopback only, rejects non-loopback Origin, tool calls serial on game thread [V] | No auth, same-machine only; arbitrary `unreal.*` Python; writes wrapped in undo transactions, refused during PIE [V] | Write guards, rollback, source-control gating, audit [V] | Back up assets before graph/landscape writes [V] |

**Notes.** Epic's server uses Tool Search by default: clients first see three meta-tools (`list_toolsets`, `describe_toolset`, `call_tool`) [V]. `ModelContextProtocol.GenerateClientConfig ClaudeCode` writes `.mcp.json`; ours matches the default endpoint [V]. Live Coding does not add new UFUNCTIONs, so adding C++ tools needs an editor restart [V]. Python toolsets are just `unreal.ToolsetDefinition` classes under any plugin's `Content/Python` [V], so our `unreal/tools/py` can be exposed as our own toolset at no cost.

**Recommendation.** (1) Use Epic's plugin as the base: free, first-party, nothing to compile, already configured. (2) Add **VibeUE** only if week-1 use shows the stock toolsets too thin for materials/levels/PCG; it layers on the same endpoint, so running both is sensible (same server, extra tools), but it adds a long C++ build on a 16 GB machine and a large tool surface that fattens context [E]. (3) Prefer writing a small project toolset over our existing Python before adding a third-party server. (4) Avoid running UE-MCP alongside: a second server and Node dependency for overlapping tools. Cost: all free. Safety: all execute arbitrary editor commands with no auth; keep loopback, work on a clean git state, commit before sessions that mutate assets.

## 3. Top 10 changes to our plan (value over effort)
1. Keep Epic MCP as the only default; defer VibeUE; wrap `unreal/tools/py` as a project toolset. Amends HANDOFF s1 and the Unreal tooling README.
2. Run the Movie Render Graph reference render in week 1 as the look-test instrument. Amends `unreal-photorealism.md` s3 (photo mode "later" is wrong).
3. Make "software Lumen vs HWRT on runtime meshes" test #1, with RMC Core as a trial replacement for ProceduralMeshComponent and RMC Pro as the gated, paid escalation. Amends photorealism s2 and the runtime-mesh decision in planning-log.
4. Correct the DLSS FG note (4060 can, 3060 cannot) and add DLSS and FSR 4 plugin trials. Amends photorealism s1 upscaling.
5. Add `snapshothitches` and Spatial Profiler to the measurement kit. Amends `performance-plan.md`.
6. Set `r.MegaLights.Supported 0`, `DeferredInvalidationBudget`, Lumen Lite A/B in the default scalability ladder. Amends performance-plan.
7. Mark Fast Geometry Streaming "avoid for generated content". Amends photorealism s1 (World Partition).
8. Record "5.8 last UE5 release; 5.9 reserve; UE6 unknown" as a reason for the sim_core boundary. Amends planning-log risk list.
9. Treat the Mesh Terrain / PCG interplay as the other agents' call; do not decide here.
10. Add a "cost watch" row: only RMC Pro ($49 one-time) is a possible paid item.

## 4. First week in the editor
- Enable Unreal MCP + All Toolsets, Auto Start Server; run `GenerateClientConfig`; confirm tool calls from Claude (`list_toolsets`).
- `stat gpu`/Insights on the 1 km scene: software Lumen vs Lumen Lite vs HWRT with runtime road/earthwork meshes; check card and distance-field visibility with `r.Lumen.Visualize`-style views.
- TSR vs DLSS vs FSR 4 on lane markings and wires; Dynamic Resolution on.
- VSM edit hitch with budget on/off; RVT page update cost on road edit.
- Movie Render Graph reference frames (street, aerial) and a path-traced still for comparison.
- Shader-compile time and RAM peak with MegaLights unsupported and Substrate off.
- Confirm VS 2026 MSVC acceptance by UBT 5.8 (already in KNOWN_RISKS).

## 5. Open questions for the person (max five)
1. ANSWERED 10 Oct: paying for RMC Pro ($49 one-time per TriAxis site) is "depends how much", decided only after the free paths are measured.
2. ANSWERED 10 Oct: a hardware-RT top tier is wanted if achievable; the baseline must still run on the RTX 3060-class target without it.
3. Should night/street-light scenes (MegaLights) be in v0 or after?
4. Do you want a photoreal "photo mode" (path traced stills) as a product feature, or only as our internal reference tool?
5. ANSWERED 10 Oct: AI write access to editor assets via the Unreal MCP is approved (git is the rollback).

## Sources
- Epic, Unreal MCP in Unreal Editor: https://dev.epicgames.com/documentation/unreal-engine/unreal-mcp-in-unreal-editor?lang=en-US
- UE 5.8 release notes: https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-5-8-release-notes
- Tom Looman, 5.8 performance: https://tomlooman.com/unreal-engine-5-8-performance-highlights/
- VibeUE site and repo: https://www.vibeue.com/ , https://github.com/kevinpbuckley/VibeUE
- UE-MCP: https://ue-mcp.com/ ; UnrealMCP++: https://github.com/EdwardKenwayl/UnrealMCPPlus ; overview: https://explainx.ai/blog/unreal-engine-5-8-claude-codex-mcp-ai-integration-2026
- RMC: https://github.com/TriAxis-Games/RealtimeMeshComponent , https://triaxis.games/realtime-mesh/ , https://triaxis.games/realtime-mesh/faq/ , https://triaxis.games/realtime-mesh/changelog/ , https://triaxis.games/realtime-mesh/docs/rendering/lumen-cards/ , https://github.com/TriAxis-Games/RealtimeMeshComponent/issues/296 , Fab Pro listing (403 on fetch): https://www.fab.com/listings/c04e6977-a3bd-4e24-b0a2-0b696bee90da
- DynamicMeshComponent Lumen/DF: https://forums.unrealengine.com/t/looks-like-lumen-support-for-dynamic-meshes-is-coming-possibly-in-ue-5-5/1951349 , https://forums.unrealengine.com/t/is-the-dynamicmeshcomponents-distance-field-generation-option-editor-only/2244548 ; runtime UStaticMesh build: http://www.gradientspace.com/tutorials/2020/10/23/runtime-mesh-generation-in-ue426
- FSR plugin: https://gpuopen.com/learn/amd-fsr-plugin-updated-for-unreal-engine-58/ ; XeSS/DLSS report: https://www.pcgameshardware.de/Grafikkarten-Grafikkarte-97980/News/XeSS-Plugin-3-1-Unreal-Engine-5-8-Support-1550221/
- UE6 road (403 to fetch, snippets only): https://www.unrealengine.com/news/the-road-to-ue-6 ; State of Unreal: https://www.unrealengine.com/news/state-of-unreal-2026-top-news-from-the-show
- Geometry Scripting guide: https://dev.epicgames.com/documentation/unreal-engine/geometry-scripting-users-guide-in-unreal-engine
- Nanite roadmap: https://portal.productboard.com/epicgames/1-unreal-engine-public-roadmap/c/1155-nanite
