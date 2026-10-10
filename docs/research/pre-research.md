# City Painter: v0 pre-research findings

10 Oct 2026

Checks done before handing v0 to Claude Code, so its limited usage goes on building. Items marked unchecked still need verifying. This is not legal advice.

## Findings that change the plan

- **Avoid CGAL.** Most of its algorithm packages are GPL, including Straight_skeleton_2. Only the foundation packages are LGPL, and a commercial licence is sold by GeometryFactory. Epic's FAQ says Unreal Engine code cannot be combined with copyleft-licensed code, so a shipped game cannot casually use GPL CGAL. Use **Clipper2** (Boost Software Licence, permissive) for polygon clipping, offsetting and triangulation. Boost.Geometry is the other candidate (its licence is unchecked).
- **A straight skeleton for roofs** therefore needs a permissive alternative or a custom implementation. This is not needed for v0.
- **Unreal may cover vegetation.** Unreal 5.7 (released late 2025) marked PCG as production-ready, and it includes an experimental Procedural Vegetation Editor and experimental Nanite Foliage. This could reduce the need for Infinigen. Compare both when vegetation arrives. Whether a newer engine version exists is unchecked.

## Unreal licensing (checked against Epic's licence page)

- Free for the first $1 million of lifetime gross product revenue, then a 5% royalty on revenue above that.
- Epic Games Store sales are royalty-free.
- A Release Notification Form is required when you ship, then quarterly royalty reports.
- No combining engine code with copyleft code (see CGAL above).

## Real UK data for test fixtures

- **Environment Agency LIDAR Composite DTM, 1 m:** covers about 99% of England, delivered as GeoTIFF in 5 km tiles on the OS National Grid, under the Open Government Licence with an attribution statement. It covers England only. Vertical accuracy is about ±15 cm.
- **OpenStreetMap (ODbL):** the OSM licence FAQ says the ODbL restricts only public use, so internal test fixtures are fine. If a shipped game includes OSM-derived maps, they count as a produced work needing attribution, and recipients can request the derived database.
- **Ordnance Survey open data:** licence and coverage unchecked.

## Road markings: the official sources

- **Traffic Signs Manual Chapter 5: Road Markings** (Department for Transport, 2019 edition, free PDF): https://assets.publishing.service.gov.uk/media/5c4ace6ded915d38a0611abc/traffic-signs-manual-chapter-05.pdf. Topics include stop and give way markings, longitudinal lines, double white lines, major and minor junctions, roundabouts, signal-controlled junctions, grade-separated junctions, yellow box junctions, traffic islands, pedestrian crossings, waiting restrictions, arrows and lane destinations. The manual says to check gov.uk for the current edition.
- **Traffic Signs Regulations and General Directions 2016, Schedule 9, Part 6:** the regulatory road markings, on legislation.gov.uk (verify the current amended version).
- **Chapter 3 (Regulatory Signs)** covers stop and give way, cycle and bus markings, which Chapter 5 defers to.
- **Highway Code road markings page** on gov.uk is a quick reference.

## Still to do

- Marking rules: first extraction of Chapter 5 sections 1 to 5 is done in the doc City Painter: UK road marking rules (draft, sections 1 to 5), exported as docs/road-marking-rules.md. No further extraction is planned: it is a reference, and the person tunes exact numbers by eye.
- Check Ordnance Survey open data, Runtime Virtual Texturing status, Boost.Geometry's licence, and the current Unreal version.

## Unreal: MCP and runtime terrain

Researched 2026-10-10 by web search. Mostly third-party blogs; items marked UNVERIFIED were not confirmed against Epic docs.

### MCP servers for UE 5.8
- **Epic's Unreal MCP plugin (recommended).** It ships with UE 5.8 (released 2026-06-17) and is marked Experimental in the release notes: https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-5-8-release-notes . It runs an HTTP MCP server inside the editor at `http://127.0.0.1:8000/mcp`. Plugins: Unreal MCP, All Toolsets, Toolset Registry. Console commands: `ModelContextProtocol.StartServer` and `ModelContextProtocol.GenerateClientConfig ClaudeCode`. The second writes `.mcp.json`; the other arguments are Cursor, VSCode, Gemini, Codex, All. Sources: https://note.com/hacklog_stealth/n/n4ad0da7ef8c1?hl=en (2026-08-26), https://indieforgames.com/claude-unreal-engine-mcp/
  - Capabilities: hundreds of tools in 30+ toolsets, including actors and scene, Blueprints, assets, materials, meshes, Sequencer, Niagara, UMG, automation tests and editor scripting. By default it exposes `list_toolsets`, `describe_toolset` and `call_tool`.
  - UNVERIFIED: whether it can run arbitrary Python or console commands, take viewport screenshots, read the output log, or trigger a C++ build. No source listed them. Check with `list_toolsets`.
  - Hotfix 5.8.1 fixed MCP crashes and response framing: https://forums.unrealengine.com/t/5-8-1-hotfix-released/2738864
  - Limits: loopback only, no authentication, serial calls on the game thread, and new tools need an editor restart. Epic says "Localhost is not a trust boundary".
  - Licence: UNVERIFIED for the engine plugin, which falls under the UE EULA. The companion repo https://github.com/EpicGames/unreal-engine-skills-for-claude-code-plugin is MIT. It installs with `/plugin install unreal-engine-skills-for-claude-code@claude-plugins-official` and adds a SessionStart hook that needs bash.
- **Community alternatives, pre-5.8 and UNVERIFIED on 5.8:**
  - https://github.com/chongdashu/unreal-mcp: C++ plugin plus Python server on TCP 55557, experimental, targets 5.5.
  - https://github.com/mirno-ehf/ue5-mcp: Blueprint editing.
  - Unreal's built-in Remote Control API (HTTP on 30010).

### Runtime terrain in a packaged game
- **Landscape heightmap at runtime: not supported.** Edit layers and the landscape editing and import APIs are editor-side. The 4.27 docs for `LandscapeImportHeightmapFromRenderTarget` say it is editor-only: https://docs.unrealengine.com/4.27/en-US/API/Runtime/Landscape/ALandscapeProxy/LandscapeImportHeightmapFromRend-/index.html . Edit layers are understood to be baked before cooking (a forum poster's belief, not an Epic statement): https://forums.unrealengine.com/t/do-edit-layers-in-ue5-5-exist-at-runtime-or-must-they-be-collapsed-baked/2615848 . Reading heights at runtime works. An old Answers thread says runtime modification is "not supported": https://answers.unrealengine.com/questions/401283/view.html . I found no 5.7 or 5.8 change. The third-party Errant Landscape plugin claims runtime heightmap, weightmap and collision edits (https://documentation.errantphoton.com/landscape/runtime/introduction/). Dedicated servers strip heightmaps. Its licence and stability are UNVERIFIED.
- **Mesh Terrain (new in 5.8, Experimental).** A mesh-based terrain with layering, virtual texture, variable tessellation and Nanite, meant to remove the heightfield-only limit. Runtime editability is UNVERIFIED and it is experimental, so treat it as unsuitable for shipping on current evidence. Source: the 5.8 release notes above.
- **Virtual Heightfield Mesh + Runtime Virtual Texture:** can display height from an RVT, so a runtime-rendered height texture can drive it. This is from background knowledge, UNVERIFIED in this pass. Collision is not provided (it is a render-only mesh), and a separate collider is needed.
- **Custom clipmap mesh with WPO from a height texture:** fully runtime-editable by writing to a render target and sampling it in the material. From background knowledge, UNVERIFIED. Collision and physics have to come from a CPU copy of the heights, and Nanite does not support WPO well.
- **Dynamic mesh / Geometry Script (`UDynamicMeshComponent`):** a real runtime option, with collision. The README already notes a packaged-build Geometry Script crash reported for 5.8.3. That is UNVERIFIED and I did not find the source again. Check Epic's forums before relying on it.
- **Recommendation:** for runtime-editable terrain (cuttings, embankments), use a chunked dynamic mesh or a clipmap with WPO plus a CPU heightfield for collision and simulation. Keep Landscape only for static terrain.
