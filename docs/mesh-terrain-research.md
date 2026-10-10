# Mesh Terrain (UE 5.8, Experimental): does the "avoid" verdict stand?

Researched 10 Oct 2026. Extends `docs/unreal-photorealism.md` (Landscape and terrain material techniques). Tags: [V] verified from a source read, [U] unverified/secondary, [E] estimate.

**Verdict: the earlier verdict stands, now with hard evidence. Option (a): keep our own mesh tiles/clipmap; borrow ideas only (c). Do not feed Mesh Terrain from sim_core.**

## 1. What it is
- Not one plugin but a family under `Engine/Plugins/Experimental/`: `MeshPartition` (core), `MeshTerrainMode` (editing UI), `PCGMeshPartitionInterop`, `PCGPrimitives_MeshPartitionInterop`, `MeshPartitionWater`. [V: `D:\UE_5.8\Engine\Plugins\Experimental\*`]
- Every `.uplugin` has `IsExperimentalVersion: true`, `EnabledByDefault: false`. [V]
- `MeshPartition.uplugin` modules: `MeshPartition` **Runtime**, `MeshPartitionCompute` **Runtime**, `MeshPartitionEditor` **Editor**, `MeshPartitionModelingToolset` **Editor**, `MeshPartitionEditorUI` **Editor**. `MeshTerrainMode`: one **Editor** module. [V]
- Description: "non-destructive modifier editing, and platform-adaptive runtime representations". [V]
- Epic's docs say to be cautious when shipping with it. Roadmap: only "Experimental" in the 5.8 announcement; no promised date found. [V docs; U roadmap] No Epic statement on runtime editing found.

## 2. Runtime editing: no
Evidence that authoring and compilation are editor-only and the packaged game consumes baked output:
- All modifier classes (Patch, Spline, Boolean, Noise, Remesh, TexturePatch, `MeshPartitionHeightmapImporter`, `ModifierGraphCache`, task graph) live in the **Editor** module `MeshPartitionEditor`. The Runtime module has no modifier implementations, only the `UMeshProjectModifier`/`UInstancedProjectionModifier` friend declarations. [V: `MeshPartition/Source/MeshPartitionEditor/Public/...`, `MeshPartition/Public/Modifiers/MeshPartitionProjectModifierTypes.h`]
- `IMeshPartitionCompilerInterface` comment: "the compiler is in the editor package, but the placeholder needs to invoke it in runtime code"; runtime only has `BuildPlaceholderCompiledSection`. The placeholder flag is documented as "created during PIE to be a placeholder for a missing compiled section, that needs to be built on the fly", i.e. a PIE aid. [V: `MeshPartitionCompilerInterface.h`, `MeshPartitionCompiledSection.h`]
- `UMeshPartitionCollisionComponent::RebuildPhysicsData`, `RebuildIfNeeded`, `AsyncBodySetup` sit under `WITH_EDITORONLY_DATA`. [V: `MeshPartitionCollisionComponent.h`]
- Epic docs: compiled sections are "runtime-only data", preview sections "editor-only". [V: dev.epicgames.com docs page]
- Compiled sections carry hashes of modifiers/packages for invalidation: a cook-time cache design, not an interactive one. [V `MeshPartitionCompiledSection.h`; E interpretation]
- Forum: someone could not get a Mesh Terrain modifier to work in a running game; the only reply was an untested suggestion; no Epic answer. [V thread, Aug 2026]
- Async rebuild cost at runtime: not applicable, no such API. [V by absence in public headers]

## 3. Data model
- Inputs: modifier components attached to any actor (Patch, Spline, Boolean, Noise, Remesh, Mesh Project, Texture Patch, Lattice, Sculpt layers), a `MeshPartitionDefinition` recipe asset, "Weight Channels" (vertex attributes baked to a texture array, writable by modifiers or PCG), priority layers. [V headers + docs]
- Heightmap import exists as an editor class. Blueprint interfaces `IModifierBlueprintInterface` / `ISplineModifierBlueprintInterface` exist for placement. [V]
- Pushing our heightfield plus tunnel cut-outs in: only at edit time (import to a baked partition). That would duplicate sim_core's ownership of terrain and break undo/commands. [E]
- Overhangs/tunnels: terrain is real mesh, so Boolean/Mesh modifiers can cut them (docs: "overhangs, tunnels, sheer cliff walls"). [V docs]

## 4. Rendering
- Output is `UMeshPartitionStaticMeshComponent : UStaticMeshComponent` per compiled section (Nanite, VSM, Lumen follow the normal static-mesh path); collision is a trimesh component. [V headers; Nanite on by default U]
- Streaming: compiled sections are World Partition actors with streaming bounds, grid `CellSize`, HLOD layer support. [V `MeshPartitionGridSettings.h`, `MeshPartitionCompiledSection.h`]
- Virtual textures: compiled sections hold `URuntimeVirtualTexture` lists, `UMaterialCacheVirtualTexture` caches and VT fallback meshes. Epic has a dedicated "RVT and Mesh Terrain" page. [V header; page not read]
- Material workflow: weight channels baked to a 2D texture array, sampled by a Mesh Terrain material (dedicated docs page, not read). [V docs listing; U details]
- Roads: spline modifiers carve and blend; RVT blend for surface. [V modifier names; U workflow]

## 5. PCG
`PCGMeshPartitionInterop` (Runtime module + Editor module, Experimental) adds surface-sampling/projection elements (`FPCGProjectionSpawnerElement` is a friend of projection modifiers); PCG can inject weight channels. [V uplugin, headers; U that sampling works against runtime-built data]

## 6. User-reported problems
- Seams with tessellation plus a Nanite material; helped by switching Channel UV Layout Method to Plane/Volume Encoded; seen in PIE only. [U single forum report: forums.unrealengine.com/t/mesh-terrain-mesh-partition-5-8/2730153]
- Material glitches, VSM lag; "glitchy, not production ready". [U secondary blogs]
- No evidence of anyone shipping with it. [U]

## Decision for City Painter
- **(a) is correct.** Our terrain is edited every few seconds with undo, tunnels cut from the sim's own bore, 20 km. Mesh Terrain is a baked, editor-authored, World Partition asset pipeline. It cannot be a render target for live sim_core data.
- **(b) rejected**: no runtime API, Experimental, and Epic is pivoting to UE6 (per existing note).
- **(c) borrow**:
  1. Weight channels as a texture array fed by sim data (our surface/material masks per tile) with one auto-material.
  2. Tiles as plain `UStaticMeshComponent`-class meshes with Nanite so they reach Lumen/VSM/distance fields cheaply (relevant to the runtime-geometry-vs-Lumen risk; whether runtime-built Nanite is possible stays open).
  3. Per-cell streaming bounds and HLOD for far field, matching our 256 m tile plus 1 km super-tile plan.
  4. RVT as the road/terrain blend layer (already planned).
  5. Variable tessellation/displacement settings: read the Material page for parameter ideas only.
- **Short trial once the editor runs (about half a day, optional)**: enable `MeshPartition` + `MeshTerrainMode`, build a 1 km partition with a Boolean tunnel, then inspect (i) how the Mesh Terrain material is wired (copy the layering and UV layout ideas), (ii) Lumen/VSM cost of its Nanite sections on the 3060. Cheap, informational.
- **What would change the verdict**: a public Runtime API (or Blueprint/C++ call in a packaged build) that applies a modifier and recompiles a section under ~10 ms off-thread, or Epic publicly stating runtime modifiers are planned. Absent those, no.

## Sources
- Local engine, read: `D:\UE_5.8\Engine\Plugins\Experimental\MeshPartition\MeshPartition.uplugin`, `MeshTerrainMode\MeshTerrainMode.uplugin`, `PCGMeshPartitionInterop\*.uplugin`, `MeshPartition\Source\MeshPartition\MeshPartition.Build.cs` and Public headers named above, `MeshPartitionEditor\Public` file list. (`C:\UE_5.8` copy did not yet exist.)
- https://dev.epicgames.com/documentation/unreal-engine/mesh-terrain-in-unreal-engine
- https://forums.unrealengine.com/t/mesh-terrain-modifiers-at-runtime/2741259
- https://forums.unrealengine.com/t/mesh-terrain-mesh-partition-5-8/2730153 (via search summary)
- https://www.unrealengine.com/news/unreal-engine-5-8-is-now-available and 80.lv / strayspark / projprod / vagon write-ups (search summaries only, [U])
- Not read: the Material, RVT and PCG sub-pages and the knowledge-base article (page returned no body).
