# Epic's PCG City Sample (UE 5.8): what it is and what it means for City Painter

Research note, 10 Oct 2026. Extends the PCG verdicts in `unreal-photorealism.md` and `performance-plan.md`; does not redo them.

**Confidence key.** [V] = read on the cited page (URL in Sources). [U] = secondary or search-snippet only. [E] = my estimate. Fab (403) and the unrealengine.com announcement (403) could not be fetched; facts from them are [U]. Release notes were read for ~300k of 357k chars; the PCG section was only partly seen.

## 1. What the sample is

The person is right. The candidate is the **City Sample, updated for UE 5.8**, now with PCG-built levels. Candidates:

| Candidate | Matches "new PCG city"? |
|---|---|
| City Sample 5.8, new `CitySamplePCG` plugin + levels | **Yes.** Free; same project as the Matrix Awakens sample. |
| Original City Sample (Houdini-built, kept in the same project) | No, old. Needs Houdini. [U] |
| Community road/street PCG (PCGEx tutorials, Fab generators) | No; third party. [U] |

- **Version/where:** UE 5.8; free via Fab, the Editor Home panel or the launcher. [U: search summary of Epic announcement; Fab 403]
- **Size:** not found. An older forum thread mentions ~90 GB of cached data for the original sample [U]. Treat as large [E].
- **Licence:** Fab lists the Standard License; the ZerotoBeast article says check terms per asset [U]. Earlier Epic samples are usable in UE products [V in `vegetation-and-surface-realism.md` line 53]. Not confirmed for this one. Reusing its graphs in a shipped Unreal game is likely fine, but the Quixel/Megaplants content has its own terms [E].
- **Hardware guidance:** SSD, 12-core CPU, 64 GB RAM, 8 GB+ GPU for the sample as a whole [U].
- **Contents [V]:** `L_CitySamplePCG_demo` (World Partition, 18 interdependent graphs, run in order: terrain, districts, roads, highways, lots, ground, parking, buildings, rooftops, hero tower, park, city edge, forest), `CitySample_generator_steps`, and `City_Circular_1km_2_arteries_slopes`. No Mass traffic or pedestrians in the new demo [U].

## 2. How it makes buildings, roads, lots

- **Buildings [V]:** a primitive `Assign City Sample Building` takes **footprint splines**, extrudes each to a height between min/max (or `@data.MaxHeight` if present), then applies a **shape grammar** chosen from a style string (27 styles: Chicago, New York, San Francisco; empty string = any). Each building is its **own actor**.
- **Shape grammar [V]:** data assets of modules (meshes) combined by a grammar string (`A*`, `[A,P]:2`, weights), placed along a spline per cross-section slot. Seeded per subdivision node. Epic's page does not state that identical input gives identical output, and gives no module size/pivot rules.
- **Roads and lots [V]:** districts, blocks and a first road network come from spline guides (terrain, city shape, arteries, highways); roads solve intersections, level the terrain, and use shape-grammar definitions for road, sidewalk and highway. The road footprint feeds the **lot** graph, which makes building footprints per district category.
- **Skipping the road part:** the page does not describe external road/parcel input [V]. Inference [E]: the building stage only needs footprint splines + attributes, so lots could be replaced by parcel polygons from `sim_core`, but the lot graph's districts and style data would need rewiring. Test, not assume.

## 3. Runtime or editor-time?

- The City Sample page describes **editor-time** generation: pause button, cleanup command, full rebuild "within a few minutes", ~2,600 actors at 128 m partitions, streaming off by default [V]. Changing a low-numbered graph rebuilds all later ones [V].
- PCG itself supports **runtime generation** (editor, PIE, standalone) driven by generation sources, with Hierarchical Generation needing Partitioned Generation [V, generation modes page].
- 5.8 notes [V, partial read, labels not shown]: GPU runtime scatter near landscape-grass speed; actor/component-less runtime generation; scheduler cuts game-thread cost up to 30%; complex attributes (arrays, structs, sets, maps as attributes). Partitioning is still tied to `APartitionActor` [V].
- **Not found:** any Epic statement that the shape-grammar building graph runs at runtime in a packaged game, or any custom-data how-to. Custom C++ spatial data/nodes are possible per the API reference [U], with no tutorial found. Editor vs runtime output mismatch is anecdotal [U].
- **Seeding:** Generate Seed and per-point seeds exist [U]; blogs report unseeded/time-based nodes breaking determinism [U]. For byte-identical reload, PCG would need a determinism test in our CI [E]; `sim_core` already guarantees this headless.

## 4. Performance evidence

No benchmark found for City Sample on a 3060 or any mid-range GPU, nor packaged-build numbers [V: absence after searching; ZerotoBeast also states no benchmark]. One forum anecdote: Nanite PCG forest at ~25 fps early in optimisation [U]. Each building being an actor (~2,600 actors) conflicts with our ISM-first rule in `performance-plan.md` [E]. Epic describes it as a lab, not a finished game [U].

## 5. Official AI/MCP toolset

Yes, **experimental**. The Unreal MCP plugin plus **PCGToolset** (Skill_PCGGraphGeneration, Shape Grammar Definition, Mesh Partition, Instancing on Mesh Actor, Biome Core skills) lets an LLM create and wire PCG graphs and inspect output via Dataview; Epic says use caution when shipping with it [V]. The MCP server has no authentication [U]. Our `unreal-mcp` connection failed (ECONNREFUSED) in this session, so nothing was tried live.

## 6. Recommendation

| Option | Verdict |
|---|---|
| (a) `sim_core` assembler emits module instance lists | **Primary.** Deterministic, headless-testable, undoable commands, same pattern as street lights; matches runtime-edit requirement. |
| (b) PCG grammar from parcels + style | **Not now.** Editor-time sample, actor-per-building, unproven runtime/regeneration on player edits, determinism unproven, and style/state would live in two places. |
| (c) Hybrid: sim decides state and massing; PCG dresses | **Adopt later**, for gardens, hedges, clutter and vegetation; consistent with the existing PCG verdict. |

**Borrow even without PCG buildings:** the shape-grammar string idea (`A*`, weights, per-slot rules) as a data-defined style format for our assembler; the min/max height + style-string input contract; grammar-driven sidewalk/road/fence assets; the lot-to-footprint pipeline and per-district-category lot sizes as a check on our parcel work; the 18-step dependency ordering.

**One-day trial (after the v0 look test, before the Buildings milestone):** install 5.8 City Sample, open `L_CitySamplePCG_demo`; (1) write 5 hand-made parcel polygons as a JSON/spline set, feed them to the building primitive and confirm it accepts non-sample footprints; (2) switch the graph to Generate at Runtime in a packaged Development build, edit one footprint, time the regeneration; (3) count actors/draw calls and frame time on the 3060; (4) run twice and diff output for determinism. Pass criteria: runtime regen under ~1 s per building, ISM output, identical reruns [E]. Fail on any, stay with (a).

## Sources
- City Sample PCG docs: https://dev.epicgames.com/documentation/unreal-engine/city-sample-pcg-for-unreal-engine
- Shape grammar docs: https://dev.epicgames.com/documentation/unreal-engine/using-shape-grammar-with-pcg-in-unreal-engine
- PCG generation modes: https://dev.epicgames.com/documentation/unreal-engine/using-pcg-generation-modes-in-unreal-engine
- PCG + LLM / Unreal MCP: https://dev.epicgames.com/documentation/unreal-engine/working-with-pcg-and-llms-using-unreal-mcp-in-unreal-engine
- UE 5.8 release notes: https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-5-8-release-notes
- Epic announcement (403, snippet only): https://www.unrealengine.com/learning/city-sample-gets-a-major-update-with-pcg-and-unreal-mcp-workflows
- Fab listing (403): https://www.fab.com/listings/4898e707-7855-404b-af0e-a505ee690e68
- ZerotoBeast review: https://zerotobeast.com/unreal-engine-5-8-city-sample-a-free-pcg-worldbuilding-lab/
- Tom Looman 5.8 performance: https://tomlooman.com/unreal-engine-5-8-performance-highlights/
- PCG forum perf thread: https://forums.unrealengine.com/t/pcg-very-low-fps-optimization-tutorial-advice/1826480
