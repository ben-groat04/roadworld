# Civil Engine / Fab listing research (11 Oct 2026)

Tags: [read on page] = text seen in a fetched page or search-result summary of that page; [inferred] = my reasoning. Fetching was done through WebFetch and WebSearch, which return model-summarised page text, not raw pages.

## 0. Headline: the two links are two different products

| | Link 1 (Epic spotlight) | Link 2 (Fab listing) |
|---|---|---|
| Product | **Civil Engine** by Beyond CAD, Inc. (founder Sam Lytle) | **Road Creator Pro** by publisher "lyoshko" |
| Kind | Standalone Windows application built on Unreal 5, for rendering transportation visualisations | Unreal Engine editor plugin/Blueprint asset pack for building roads |
| Price | Planned $3,099 per year per user (2025 figure) | About $114.99 one-off (see 2.1) |

- The Fab ID `adc4696d-7ab7-4d79-9194-fd29fdc0edfa` is titled "Road Creator Pro". Source: search-result title for https://www.fab.com/listings/adc4696d-7ab7-4d79-9194-fd29fdc0edfa [read on page, search result]. It is not Civil Engine.
- Civil Engine is not on Fab as far as found; it is sold by Beyond CAD and distributed via the Epic Games Launcher or a zip. Source: https://beyondcad.com/faq/ , spotlight search summary [read on page].

### What I could NOT reach
- Both original URLs (spotlight and Fab) returned HTTP 403 to WebFetch, including `/en-US/` and `?lang=` variants. I only have what web-search summaries quoted from them. The spotlight text beyond the summary (quotes, any named techniques, studio talks) is unread. Fab price, engine-version table, licence text and the full changelog were not seen directly.
- The Road Creator Pro manual (`Road_Creator_Pro_Documentation.pdf`, Google Drive, linked from https://bit.ly/road-creator-pro-doc ) is not readable: the viewer page needs sign-in, and a direct download exceeds WebFetch's 10 MB limit. I did not download it to disk (downloading needs the owner's yes). So there is NO manual-level detail on road representation, junction construction, marking generation, or runtime behaviour. Everything about A below is thin.
- Video tutorials (https://youtu.be/SCfR9x4oCR8, https://youtu.be/NzP_lpUhGRc) and Discord (https://discord.gg/5W4Z5t9VDw): not opened.
- Civil Engine tutorials, University, Trello roadmap, update notes (the updates page was empty of content to the fetcher): not read.
- Pirate/mirror sites came up in search; I deliberately did not use them.

## 1. Civil Engine (spotlight product)

- Epic spotlight dated 22 May 2025: Beyond CAD (founded 2020) first made Beyond Typicals (UE app for transportation sections), then Civil Engine, "after over a year of development", to turn CAD drawings into 3D visualisations in hours. Uses Quixel Bridge/Fab assets, relies on Nanite for large scenes, moved from a custom launcher to the Epic Games Launcher. Source: https://www.unrealengine.com/spotlights/civil-engine-democratizes-3d-visualization-for-transportation-planning [read via search summary only].
- Architecture facts [read on page: https://beyondcad.com/civilengine/ and https://beyondcad.com/faq/]:
  - Does NOT create roads from alignments. It needs a ready 3D base model (SketchUp preferred, InfraWorks, OpenRoads Designer) imported as OBJ/FBX. IFC unsupported, no DEM/GIS priority, ground must come from the base model. No LandXML or OpenDRIVE mentioned.
  - A "path" system: paths are placed node by node on any surface and carry traffic, assets, guardrails/barriers/fences, pavement arrows, striping and markings; paths link with merge/diverge; auto-generated wear paths follow traffic movements; pavement marking textures (5 white, 5 yellow). Area tool scatters assets inside a node-defined area with spacing, offset and randomisation.
  - Intersection controllers up to 8 phases, dynamic signals; construction phasing (4D Gantt); photo compositing; output is stills/videos; Explore mode (walk/ride/drive).
  - Standalone app, not embeddable, "scene export to other applications" not included, licence is per user on two devices. Needs RTX 3070-class GPU; 13 GB download.
  - Older Beyond CAD (UE4) imported Vissim and InfraWorks and used Datasmith. Source: https://80.lv/articles/beyond-cad-a-powerful-3d-visualization-engine-for-civil-projects [read on page].
  - Beyond Typicals is section based: drag-and-drop typical cross-sections for straight segments of 50 to 1,000 ft, with bridges, slopes, utilities; custom Section Creator and Streetmix importer [read on page: faq].
- Pricing: Civil Engine $3,099/yr (2025), Beyond Typicals $2,099/yr, 30-day free trial, enterprise by quote [read on page: faq]. Capterra listed from US$309 per user (probably monthly) [read via search: https://www.capterra.com/p/10031553/civil-engine/reviews/].
- Verdict: no usable architecture documentation exists for road geometry, junction markings or terrain, because it consumes already-modelled geometry. Nothing to borrow for geometry. The only transferable ideas are the path-based placement of markings/barriers/wear, and the area scatter UX. [inferred]

## 2. Road Creator Pro (the Fab listing)

### 2.1 Identity, price, versions, licence
- Name Road Creator Pro, publisher lyoshko. Forum thread https://forums.unrealengine.com/t/lyoshko-road-creator-pro/2418882 [read on page]. Fab category page / search snippet gave $114.99 [read via search, unconfirmed on the listing itself]. A reseller showed 104.99 (maybe older).
- Engine versions: forum shows testing on UE 5.2 and 5.6; a third-party site says 5.2 to 5.6 [read via search, weak]. No source confirms UE 5.7 or 5.8. Our engine is 5.8.3, so compatibility is UNVERIFIED and likely needs porting. [inferred]
- Licence: standard Fab licence assumed; I did not see the licence text. DigitalProduction mentions "Personal" and "Professional" tiers by revenue threshold but figures were garbled (https://digitalproduction.com/2025/03/03/road-creator-pro-paving-the-way-in-unreal-engine-5/ ). Treat as unknown.
- Source access: Blueprint-heavy (the forum shows the Construction Script and Blueprint loops are discussed; a user "rewrote the system in C++ with subsystems"). So source is readable inside the asset, but Fab licences normally forbid redistributing it. [forum: read on page; licence: inferred]
- Last status: the thread says assets were "back online" on Fab on 20 Sep 2026 after being unavailable; reason not given [read on page: thread page 2].

### 2.2 Features (all from marketing/press, not the manual)
Sources: https://digitalproduction.com/2025/03/03/road-creator-pro-paving-the-way-in-unreal-engine-5/ , https://80.lv/articles/create-realistic-roads-highways-bridges-intersections-in-ue5-with-this-tool , forum thread. [read on page]
- Spline-based roads, 1 to 8 lanes; city street, gravel, highways, bridges, tunnels.
- Road parts adjustable in size/material/placement: guardrails, fences, barriers, sidewalks, lampposts, noise barriers, cat's eyes, shoulders, tactile paving.
- Prebuilt intersections (the forum says they are 90-degree crossings), roundabouts (a few sizes/centres), highway merges, with automatic snapping; markings, signs, stop lines, crossings.
- Bridges/overpasses/cable-stayed with adjustable support spacing, height, abutments; tunnels with portals, lights, fans.
- Procedural materials: cracks, patches, puddles, wear, edge ripple; customisable lane markings; retroreflective markings; decals (skid marks, potholes).
- Editing: snapping, split/merge roads, junction placement, slope and curvature checks against "international guidelines".
- Terrain: roads conform to Landscape; "Deform Landscape" button with adjustable deformation height, and (changelog March 2026) "Adaptive Landscape Deformation" mode that keeps a constant embankment slope [forum page 2 and Fab changelog via search]. Terrain painting. Whether shoulders blend or cut/fill is not confirmed.
- Props along splines via a "prop instancer"/barrier blueprints scattering lamp posts and fences.
- Performance: instanced meshes, LODs, optional merging into one mesh, Nanite or LODs, Lumen/Static/Mobile/Path Tracer compatible. Bake to static mesh via Merge Actors or manual section "Converting Road to Static Mesh" (page 25) [forum].
- Traffic: no own traffic; pairs with City Traffic Pro, which generates its own roads from selected RCP roads (CTP intersections support only two crossing roads) [forum page 1].
- 44.5 km of demo roads in three cities (EU, US, UK style).

### 2.3 Architecture as far as visible (A)
- Representation: Unreal spline components with spline-mesh pieces (the forum says "spline meshes are the main cost, hundreds of milliseconds") and instanced static meshes; roads are assembled from pre-modelled mesh modules (lane pieces, kerbs, intersection meshes, roundabout centre meshes) rather than a computed cross-section/lane-section network. [forum: read on page; "modular mesh kit" conclusion: inferred]
- Junctions: pre-made mesh intersections that snap; no generic polygon junction solver. Custom shapes require joining roads manually or a user-made workaround. Forum: roundabouts need manual circle of four points, "junctions may not look right on tight curves". [read on page]
- Markings: part of the pre-made meshes/materials plus decals; no evidence of computed markings at arbitrary junctions. [inferred]
- Terrain: edits the Landscape heightmap from a button in the editor (editor-time operation; Landscape sculpt is editor-only). [Deform Landscape: read on page; editor-only: inferred]
- Bridges/slopes/walls: bridge and tunnel are parametric mesh kits; no graded retaining structures found. [inferred]
- Vegetation clearing: not mentioned anywhere. [read: absence in all summaries]
- Runtime: Construction-Script based (rebuilds on edit). The forum discussion of "packaged builds" is about memory and baking; the developer recommends baking into static meshes for distribution. So it is an EDITOR-TIME authoring tool whose output is baked meshes. No runtime road-drawing documented. [forum: read on page; verdict: inferred]
- Performance flags from a user audit: hard mesh/material references, components not cleaned on rebuild, stutter dragging large networks. [forum page 2, unconfirmed against the asset]
- Unreal systems named: Landscape, spline mesh, ISM, Nanite, Lumen/path tracing, Merge Actors. PCG, RVT, Dynamic Mesh not mentioned for RCP (a different Fab asset "Easy Decal Roads RVT" exists). [inferred from absence]

## 3. B. Workflow/UX
- RCP: place spline points in the editor viewport, pick a road preset, parameters in the Details panel, snap ends to roads/junctions, split/merge, "Pick Parameters from" (copy from another road, Mar 2026), "Merge to one Road" checkbox, start/end offsets (Feb 2026), editor utility EUW_Road_Creator_Helper to convert an existing spline to a road [Fab changelog via search; forum]. Nothing better than our click-point clothoid tool with snapping; it is weaker (no clothoids, 90-degree intersections only).
- Civil Engine: node-by-node path placement on any surface, with per-path attributes; interactive but for dressing, not road creation.
- Worth stealing UX: "pick parameters from" (eyedropper road type/attributes), slope and curvature checks shown against standards (we have this as amber advisories), merge/split roads.

## 4. C. Ideas for us, by milestone
1. M3/M7: eyedropper "pick type from road" and "merge/split roads" commands (both tiny `cp::Command`s). [from RCP changelog]
2. M5: constant-slope "adaptive deformation" is what our grading already does (cut/fill to a fixed batter); compare only the naming. RCP's fixed-slope embankment is simpler than ours and has no tier/wall logic, so we are ahead. [inferred]
3. M6: path-attached props (lamp posts, barriers, fences) and area scatter with spacing/offset/randomisation parameters: matches our `street_lights` and `vegetation` layers; consider exposing spacing, offset and jitter as region data, and a "wear path" idea for markings (Civil Engine auto-generates wear along traffic movement; we have `wheel_boost`).
4. Markings: ten marking textures (5 white, 5 yellow) and a "wafer" marking in RCP suggest scuffed variants per paint; add a few paint-wear variants to the material rather than only a global strength. [inferred]
5. Runtime/baking lesson (M4/M8): RCP's own users hit Construction-Script rebuild cost, uncleaned components and hard references; our budgeted tile swap, ISM pools and soft loading plan already avoid these. Confirms the design, nothing to add.
6. Lumen surface cache for runtime meshes: neither product documents it. No help for the PMC problem. [read: absence]
7. Junction marking correctness and angular earthworks: no information in either product. RCP's intersections are fixed 90-degree meshes; our junction solver is more general. [inferred]

## 5. D. Would buying help?
- Road Creator Pro (about $115, 5.6 at best): as a reference implementation it is a Blueprint mesh kit with editor-time landscape deformation. It does not generate junction geometry, markings or earthworks, and it is not engine-agnostic. It cannot replace sim_core parts, because our rule is that game logic lives in a headless library and runs at runtime in a packaged game; RCP is editor-time authoring. Licence terms on reading/shipping were not seen; Fab licences generally allow shipping in a game but not redistributing the asset source [inferred]. Possible narrow value: its marking/sign/barrier meshes and materials as placeholder assets, subject to checking 5.8 compatibility. Recommendation: do not buy for architecture. Optional: buy only for mesh assets, after the owner checks the licence and 5.8 support on the listing. Owner decides.
- Civil Engine ($3,099/yr): a closed standalone render tool, input is finished OBJ/FBX, no scene export, no source. Cannot help. Recommendation: do not buy.
- Better comparables mentioned in search (not examined): MetaRoad Free (HD-map style editor, bakes meshes) https://www.fab.com/listings/a425c08b-d82a-4b42-ab94-101c3eb23f28 ; Dynamic Road System (Geometry Script, adapts to landscape) https://www.fab.com/listings/8afb963e-5179-4641-b9bc-703def0f6216 ; Advanced Spline Roads https://www.fab.com/listings/856b67dc-cea4-4f54-b93e-aa177348d1f8 ; Easy Decal Roads RVT https://www.fab.com/listings/7ceee069-9c09-4597-bdf4-e02b503c9cad . Of these, MetaRoad Free (free) and Dynamic Road System (Geometry Script, closest to computed geometry) are the ones worth a separate read-only research pass. [read: titles/snippets only]

## 6. E. Other
- Epic's own docs mention roads built with the Spline tool in UEFN: not relevant to us.
- City Traffic Pro (https://www.fab.com/listings/b4aedd9f-2ca9-499d-b283-0d9864520a0d) builds traffic roads from RCP roads; a hint for our later traffic milestone: derive the traffic graph from the road graph, which our lane graph already does.
- Not found: any mention of OpenDRIVE, LandXML or GIS import in either product.
