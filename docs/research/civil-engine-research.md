# Civil Engine / Fab listing research (11 Oct 2026)

Tags: [read on page] = text seen in a fetched page or search-result summary of that page; [inferred] = my reasoning. Fetching was done through WebFetch and WebSearch, which return model-summarised page text, not raw pages.

> **Update (second pass, built-in browser):** the Fab page and the 89-page manual were read in full; section 7 below ("Road Creator Pro: read from the listing and manual") supersedes every [inferred] claim about Road Creator Pro in sections 2 to 5. Corrections: price is GBP 104.26 (Personal) or GBP 190.42 (Professional), incl. VAT, not $114.99; engine versions are NOT shown on the listing without a licence/sign-in (UE 5.6 mentioned by the developer in the forum); the road is NOT only baked meshes: it is a Blueprint Construction Script tool, and per the manual the construction script never runs in a packaged game (so roads are authored in the editor).

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

## 7. Road Creator Pro: read from the listing and manual

Sources: Fab listing https://www.fab.com/listings/adc4696d-7ab7-4d79-9194-fd29fdc0edfa (read in the built-in browser, 11 Oct 2026, tabs Overview, Reviews, FAQs); manual `Road_Creator_Pro_Documentation.pdf`, 89 pages (https://drive.google.com/file/d/1NYQA6k-qZHusso1npgAfEgGpnieeDRxt/preview), read via the viewer's text layer for all 89 pages, plus screenshots of pp. 38, 41, 54. Tags: [listing] = read on the Fab page; [manual p.N] = read in the manual; [inferred].

### 7.1 Listing facts
- Publisher lyoshko, published 22 Feb 2025, last update 22 Sep 2026, "Standard License", format Unreal Engine, Not Mature, "Allows usage with AI: No" [listing].
- Price: Personal GBP 104.26 incl. 17.37 VAT (individual or small team under $100k revenue/funding in last 12 months); Professional GBP 190.42 (studios or entities over $100k) [listing, licence selector]. Updates free for owners [listing FAQ].
- Engine versions: not displayed (no sign-in). The developer tested 5.2 and 5.6 [forum thread, earlier read]. UE 5.8 compatibility: unverified.
- Rating 5.0, 40 ratings, 12 written reviews. Only critical review (4 stars): highway interchanges / on-off ramps are hard; junction pieces placed on a slightly slanted road leave gaps; junctions move when the level reloads. Developer answer: leaving BP_Junction_Placer in the level re-runs its construction script, convert it to a static mesh; "good updates on interchanges in future" [listing Reviews]. Another reviewer: "best landscape deformation options I have seen" [listing].
- FAQ: no extra plugins; no OSM auto-import (a map backdrop only); migrate from other roads via EUW_Road_Creator_Helper (any spline blueprint or Landscape Splines); packaged size under 400 MB, project about 90 MB; texture reuse; hand-made LODs; traffic via City Traffic Pro [listing FAQ]. The "28 comments" tab links out to the forum thread already read.
- Blueprints included: Road Creator, Pedestrian Path, Retaining Wall, Lamp Post, Sidewalk, Aviation Obstruction Light, Day Night Cycle, Destination Sign, Road Sign, Expansion Joint, Junction Placer, Tape Measure, Spotlight, Traffic Light, 20 Prop Instancers [listing].

### 7.2 Road representation and parameters
- Main actor `BP_Road_Creator`: one Blueprint = one road type + one set of parts; different parameters need a second blueprint (split/merge tools) [manual p.3]. Road Type fixes lane count (1 to 8 lanes per listing), bridge, tunnel or gravel [p.3].
- Straight mode (instanced meshes only) or Spline mode (spline-mesh components); Align Road To Landscape per point; Fast Editing Mode; Closed Loop [p.3].
- Road meshes are pre-modelled: straight pieces 12 m long, default lane width 4 m, 400 cm snap recommended, hand-made LODs, collision ready, usable as Landscape Splines [p.84]. So lane width is a mesh property; width changes come from spline point Y scale (single road: road width; two roads: gap width), "Road Part Width Multiplier" (road only) and "Overall Road Width Multiplier" (everything) [p.5, p.11]. "Two Roads" + "Two Roads Gap" + "Wider Gap At The End" make dual carriageways with a central gap, also a sidewalk in the centre [p.5, p.7].
- Lane transitions 2 to 3 and 3 to 2+2 are pre-made transition meshes (SM_Transition_2_to_3, 3_to_2+2 with bridge and guardrail variants, 4_to_3+1_Bridge) [pp.86 to 88]; "New Lane" meshes in 12/24/48 m lengths for adding a lane at a junction, with guardrail +2 m/+4 m width-transition meshes [p.67].
- Markings are NOT geometry: all road meshes use one asphalt material and lane lines are drawn in the material from mesh UVs, so lateral position, dashing and double lines are material parameters [p.32, p.37]. Material markings = up to 5 lane lines plus edge lines left/right and edge chevrons; "Lane Dividers are not present on all meshes" [p.37]. Per line: enable, lateral offset (Edge Lane Offset, Lane Divider offsets), dashed, double, dash gap and tiling globally and per line [pp.37 to 38]; width, Marking Bump, Marking Cracks, Retro-Reflective [p.39]; colours with a second colour selectable per line (Two Colors Markings) for country differences [p.39]. Drag-and-drop `BP_Decal` for stop lines, crossings, arrows, wafer (8 x 8 m tiling) and contours: line mode, spline-line mode, 23 marking textures, 22 speed-limit textures (more added later) [pp.43 to 44].
- Asphalt material checklist [pp.32 to 36, 39]: 30+ instances of one master; Wet; Wheel Lines (longitudinal wheel tracks per lane); Cracks; Debris; Micro Normal (close-up, distance-gated, off at low scalability); Puddles (roadside and scattered, work best with Wet and sidewalk); Repaired Lane (one or two new/shiny or coloured lanes, e.g. bus/bike); Hot Road Mirage; marking bump, cracks, retro-reflective, per-line colour; later "better blending of road markings with asphalt cracks" [p.88]. Edge Ripple: uneven asphalt edge via separate ripple/corner/end meshes [p.11]. Patches (10 decals) and Skid Marks (4 decals) scattered by density, randomise seed, decal height cm [p.13]. Curb material: grey concrete, paintable, one or two colours [p.40]. Potholes (parallax) and manholes are manual decals [pp.46, 48].

### 7.3 Junctions, roundabouts, merges
- Junctions are pre-made meshes with sockets ("Road_Connector_1"): drag the mesh, drag a road end within Search Distance (default 250 cm) and it snaps and adjusts tangents; refused if the angle is 90 degrees or more [pp.19, 25, 54]. Add Mesh at Start/End buttons spawn a mesh aligned to the road end [p.25].
- Other ways: place a junction mesh at the side of an existing road (BP_Junction_Placer snaps to the nearest road and its side, takes the road's asphalt material, has vertical/lateral offset, pitch, mirror; meant to be converted to a static mesh; its construction script "will never execute in packaged game") [p.26]; or, with sidewalks, use a plain "No Markings" plane as junction base [p.55]. Use 400 cm snap, slip lanes as separate meshes [p.55].
- Intersection markings: the meshes carry lane lines; stop lines, crossings and other markings are decals added by hand [p.56]. Screenshot p.54: a four-way mesh with fixed rounded corners, lane edge lines continuing as curves around the corner, centre lines stopping short; no stop lines until decals are placed.
- Roundabouts: three families: Big (narrow variant for 4-lane roads, wide variant for two 2-lane roads or one 2+2), Small (ready 4-entrance mesh for 2-lane roads; or constructible: centre mesh plus separate road meshes at any angle, 2-lane or 1-lane) and 50 m (5 meshes, any of 2+2 Wide, 4-lane, 2-lane at any angle); 7 sidewalk meshes to match [pp.41 to 42]. Screenshot p.41 shows flat pale slabs with white lines.
- Highway merges/interchanges: transition meshes or SM_Junction_New_Road; "Pre-made interchanges" and "Junction of any angle" are still on the roadmap [pp.66 to 67, 89]. Forum and review confirm interchanges and non-90-degree junctions are weak.
- Traffic lights: BP_Traffic_Lights with one-/two-way sequences, entries A to D, EU/US sequences, left-hand-drive option, manual set events [pp.57 to 59].

### 7.4 Terrain, bridges, retaining
- Deform Landscape writes into a Landscape Edit Layer (default name "Layer"; Spline-type edit layers have an engine bug that loses the deformation; blending of several non-spline layers is "not correct", use one) and optionally paints a paint layer [pp.30 to 31]. This is an editor-time operation on the Landscape.
- Parameters: Height Offset cm (about 0 for gravel, -5 to 10 for asphalt; bigger = smoother blend, smaller = no landscape poking through at corners), Additional Width, Spline Width Adjust, Raise/Lower Heights toggles, Start/End Offset, Pick Mesh to Conform (conform landscape to junction/roundabout meshes), Preview (red lowered, green raised) [pp.30 to 31].
- Two modes [p.31]: Adaptive Deform (a slope is set; deformation width grows when the road is high above terrain, giving a fixed-slope embankment) and Default UE mode (constant Side Falloff in metres, slope varies from flat to near vertical). Known artefact: normal lines on the landscape, fixed by a minimum-intensity brush stroke [p.31].
- Bridge: type selection; supports with spacing/offset/scale (two rows for 2+2); abutment mesh SM_Abutment placed by hand; deform the landscape only after splitting so the bridge span is not graded [pp.12, 22]. Tunnel: portals automatic, 0/4/8 fans, lights, landscape visibility painted by hand to open the tunnel [p.22]. Cable-stayed: manual pylon (125 m) and cable meshes [p.23]. Retaining wall is a child blueprint with defaults, no parameters documented [p.51]. No cut-slope rock face or tiered earthworks: ours is far ahead.

### 7.5 Checks and guidance
- "Visualize Slopes And Curves": slope % (green under 1%, yellow 5%, red over 10%), vertical curve radius Rv, horizontal radius Rh, and a recommended maximum speed from Rh "according to typical international guidelines" (shown in the variable tooltip, not in the manual); speed shown only when Rh is under 2000 m (above that "--", over 250 km/h); speed colours red under 45, yellow under 80, green over 80 km/h [p.20]. No named standard and no numeric table in the manual: the exact guideline is unread.
- Clearance table (vertical / one-lane width): small residential 3.5 to 4.0 m / 3.0 to 3.5 m; urban 4.2 to 4.5 / 3.5 to 4.0; highway 5 / 4.5; minimum car passage 2.2 / 2.5 [p.27]. Worth comparing to our region-data clearances.

### 7.6 Parts, props, lights
- Parts are separate enable-able lists in one blueprint: guardrails (spline "quality" version or 4 m instanced short pieces, broken percentage), sidewalks left/right/centre with height, fences, concrete barriers, gravel shoulder (parallax, tint, width), reflector poles (offset/spacing so they avoid junctions), cat's eyes, noise barrier, anti-glare screen, trash with inside/outside distribution distances, utility poles (one row per road, negative offset for the other side, align to terrain or all objects), custom parts array, Start End Offsets limiting where meshes are made [pp.6 to 18].
- Lampposts: single on left/right, double in centre, separate L/R offsets, spot lights without shadows, attenuation radius 2900 default, light billboards for distance, Detach Lampposts, Lampposts Start End Offset, lights switched through an interface by the day-night blueprint [pp.21, 75, 82, 88].
- Props instancers: ISM rows or spline-following, align spline points and meshes to landscape [pp.72, 88]. Trees along the road, bus stops, roadside ditch, cul-de-sac, refuge island and interchanges are roadmap items [p.89].

### 7.7 Runtime versus editor, performance
- Roads are built in the editor by Construction Scripts; they are static content in a packaged game ("Blueprint construction script will never execute in packaged game" [p.26]). Distribution tips: Merge Actors to a static mesh with "Prepare For Merge Actors" (disables material-parameter features like Wet and colours; patches/skids not merged), detach lampposts first to keep lights controllable [p.29]. There is no runtime road drawing or runtime terrain deformation documented. Verdict: editor-only authoring; not usable as a runtime dependency.
- Performance notes: spline meshes are slow to build in UE 5.1+; guardrails are the heaviest; Straight mode uses only ISMs; lamppost spot lights cost at night; Nanite can slightly hurt marking accuracy so Nanite can be disabled for road meshes; `r.Nanite.MaxPixelsPerEdge 0.5` improves far junction quality [pp.6, 82]. Material Low/Medium scalability levels. Their road meshes are static or spline meshes (so Lumen has a surface cache for them), which our runtime PMC meshes lack [inferred].

### 7.8 British demo city
- "Rainchester: British City. Left-hand drive and wet roads." Three islands (Altenburg European, Pinedale American, Rainchester British), 6 km level, 44.5 km of roads, European/American markings, signs, lights [p.83]. UK-specific changes stated: left-hand drive (traffic-light pole option, p.57), wet material. No UK marking rules documented; colours are a per-line parameter. Ours: `data/regions/uk` carries real marking rules.

### 7.9 Comparison with our junctions (screenshots pp.41, 54 only; I did not open our `after/` renders in this pass)
- Theirs: fixed corner-radius meshes, 90-degree four-way and 3-way variants plus special pieces; lines run through the mesh as part of one material; stop lines and crossings are manual decals; roundabouts are fixed slabs. They do not join up in general: a review reports gaps on slanted roads and weak interchanges. Ours are computed per junction angle from region data, so structurally we are ahead; their strengths are polish (edge ripple, wear, cracks blended with markings). [inferred from two screenshots plus text]

### 7.10 Concrete ideas, with pages
1. Material-driven markings (pp.32, 37 to 39): one road material, markings from UVs and parameters (offset, dash gap/tiling, double, bump, cracks, retro-reflective, second colour per line). Makes wear and colour region data; possible for our PMC meshes if we emit lane-space UVs. M2/M6.
2. Adaptive embankment slope versus fixed falloff (p.31) validates our slope-based grading; their Preview (red lowered, green raised) is a good M5 ghost: show cut and fill before commit.
3. Height Offset per surface type (p.30: about 0 for gravel, -5 to 10 cm for asphalt) and Spline Width Adjust: surface-dependent subgrade offsets in region data. M5.
4. "Pick Mesh to Conform" (p.30): grade terrain to a junction/roundabout footprint; matches our `grading_footprint` hook. M5/M6.
5. Part Start/End offsets and per-part enable (pp.6 to 18); reflector poles avoid junctions via offset (p.10): suppress lights and scatter near junction mouths. M6.
6. Junction placer lesson (p.26): do not keep a rebuild-on-load actor in the level; matches our baked tile design.
7. Asphalt parameter checklist (pp.32 to 36): wet, wheel lines, cracks, debris, micro normal, puddles (roadside and scattered), repaired/coloured lane, edge ripple, patches, skid marks, marking bump/cracks/retro-reflective.
8. Clearance table (p.27) and slope/curve visualisation with colour thresholds (p.20: green under 1%, yellow 5%, red over 10%; speed from radius): parallel to our amber advisories.

### 7.11 Still unverified
- Video tutorial (2:15; no description or transcript exposed to the page text) and showcase video; road-sign manual (not opened); the tooltip "international guidelines" table; Discord; Fab engine-version list; whether UE 5.8 works; licence text beyond "Standard License" and the Personal/Professional split.
- Screenshots opened: manual pp.38, 41, 54 only; the text layer was read for all 89 pages. The marking example pictures (pp.33 to 39) were not individually judged.

### 7.12 Revised recommendation
Do not buy as a dependency or architecture reference: it is an editor-time mesh-kit tool (construction script never runs in a packaged game, p.26), with pre-made junctions, no cut/fill or wall logic and no runtime terrain editing. The manual itself is free and already holds the useful ideas. Optional: placeholder props (75 signs, barriers, lampposts) for GBP 104.26 Personal, after the owner checks UE 5.8 compatibility and the licence for shipping. Owner decides.
