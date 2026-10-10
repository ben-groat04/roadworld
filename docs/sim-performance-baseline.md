# Sim performance baseline (PARTIAL)

**Status.** First numbers only. The session was cut short, so: large was measured for `grid` only, with 1 repeat and no edit loop; medium and large for `organic` and `motorway` were only smoke-run (no numbers recorded here); scaling exponents were not computed (the tool computes them when two or more sizes run in one invocation); no re-run of headline numbers; no second-run determinism comparison across processes. All timings were taken on a busy laptop (other agents compiling), so treat them as +/-30% at best. I did not measure the noise formally; small-network repeats spread by up to about 2x on mesh stages.

Machine: AMD Ryzen 9 7940HS laptop, Windows 11, MSVC (VS 2026), Release. Date 2026-10-10. Base commit 3a85117 plus uncommitted work from other agents.

## Run

```
cmake -S . -B build-bench -DCP_BUILD_TESTS=OFF && cmake --build build-bench --config Release --target bench
bench --quick                                   # small only, 2 repeats
bench --gen grid|organic|motorway|all --size small|medium|large|all [--edges N] [--repeats R] [--no-edit] [--json out.json]
```
Networks are built through `make_draw_road` / `make_place_roundabout` / `make_set_structure_span` on a `CommandStack`. Output: table on stdout, JSON (schema 1) with `--json`. Heap figures come from a counting global `operator new`; OS peak/private memory from `GetProcessMemoryInfo` (Windows only).

## Results: grid, large (20,252 edges, 10,444 nodes, 10,440 junctions, 1,551 km, 0 degraded, 1 repeat)

| stage | ms | notes |
|---|---|---|
| build via 19,384 commands | 168 | 16.6 MB graph + undo stack |
| lanes | 12 | |
| edge sampling 0.02 / 0.1 / 0.5 | 298 / 286 / 289 | tolerance has almost no effect (heading-change cap dominates) |
| junctions_plain | 5,938 | connectors 3,659, geometry 713, arms+classify 246, trims 3.5 |
| roundabout apply / find | 302 / 2.6 | 302 ms even with zero roundabouts |
| mesh chunks 0.02 | 63,264 | 961 tiles, 8.32 M tris, 635 MB |
| mesh chunks 0.1 | 19,313 | 3.22 M tris, 296 MB |
| mesh chunks 0.5 | 9,818 | 1.87 M tris, 181 MB |
| mesh chunks 2.0 | 7,396 | 1.29 M tris, 130 MB |
| of which junction meshes 0.02 / 0.1 | 55,098 / 12,429 | edge meshes only 1,217 / 887 |
| markings | 2,507 (+169 merge, 95 triangulate) | 469,702 marks, 78 MB |
| street lights | 1,349 | 66,964 lights |
| grading: sample roads / prepare / 8 tiles | 272 / 2,913 / 15 | whole-network passes |
| undo / redo last 100 commands | 2.1 / 1.9 | |

Small and medium results (all three generators) were seen in the run but not recorded; small grid for reference: junctions 70 ms, mesh 0.1 157 ms, markings 18 ms.

## Edit loop (small networks only; large NOT measured)

Dirty path on ~200-edge networks: junctions 1 to 16 ms, mesh tiles (0.1) 20 to 150 ms for 2 to 6 tiles, markings 0.2 to 4 ms, lights under 1.3 ms, versus 260 to 850 ms for a full rebuild. Grading has no dirty path: whole-network sampling plus preparation is rebuilt (5 ms small, ~3.2 s large).

How the dirty set is computed today: `RoadGraph::dirty_nodes()` only. Edges are derived as "edges at dirty nodes"; tiles are derived by the caller from old and new edge geometry. Findings (reproduce with `bench --quick --gen organic --edit-iters 1`):
1. `erase_node` removes the node from `dirty_nodes`, so edges whose both ends were removed (a new street between two split nodes, undone) are invisible; their cache entries go stale. The tool follows the old adjacency to find them. The graph should report removed ids.
2. Street lights from `rebuild_dirty_street_lights` differed from a full rebuild with the plain dirty set on the organic network (seeds 12345 and 777); widening the dirty nodes by one hop (`--dirty-ring 1`) fixed it. Markings matched once removed nodes were passed in.
3. No `change road type` command exists; the tool uses a local command built on the graph primitives.

## Hotspots (ranked, not fixed)

1. **Junction meshing** (`build_junction_mesh`): 87% of mesh time at 0.02 (about 5 ms per junction, 1.2 ms at 0.1). Likely per-junction triangle refinement and height blending against every arm per vertex. Fix: coarser refinement for 4-way junctions, cache arm profiles, or instance repeated junction shapes.
2. **Connector generation**: 62% of junction build, 0.35 ms per junction. Likely per-pair path fitting plus the pairwise conflict test. Fix: spatial reject before the crossing test.
3. **Merge/diverge generation**: about 6 ms per node on small motorway (180 ms for 28 nodes); the heaviest per-node stage.
4. **Markings** 0.12 ms per mark; **grading preparation** 2.9 s whole-network with no dirty path.
5. `apply_roundabouts` costs 302 ms on a graph with no roundabouts (a scan of all junctions).
Likely allocation-heavy: 8.6 M allocations for junctions, 342 M for mesh at 0.02 (about 41 per triangle).

Super-linearity: not established (one large size). Per-junction costs on small and large grid are within about 20%, so no obvious O(n^2) in junctions or meshing.

## Frame budget (single worker, from the large grid)

Per 256 m tile: 0.02 about 66 ms, 0.1 about 20 ms, 0.5 about 10 ms, 2.0 about 7.7 ms (dense grid, so an upper-bound city centre). A 16.6 ms frame on one worker fits well under one tile of any LOD; amortised over a worker pool of N threads, roughly N x 1 tile/frame at 0.5, 0.8 at 0.1, 0.25 at 0.02. Junction meshing must get faster before near-LOD rebuilds feel instant.

## Memory per km of road (large grid, heap)

Graph + undo about 11 KB/km; junctions 155 KB/km; markings 63 KB/km (output only); mesh 0.02 410 KB/km, 0.1 191 KB/km, 0.5 117 KB/km, 2.0 84 KB/km. Peak transient for junction build 241 MB, for mesh 0.02 750 MB.

## Quality findings

- Degraded junction share: grid 0 to 0.3%; organic 1 to 4.5% (kerb intersection beyond trim line; splitter island does not fit); motorway 0.4% with 150 m chain spacing but 25% when chain edges were about 47 m (gore does not fit). Reproducer: motorway, medium, short edges.
- `grading.h` declared a second `cp::TileKey` clashing with `render_mesh.h`; another agent has since introduced `tile_key.h` (the tool handles both).
- Large organic/motorway sizes were never run to completion here.

## Thread-safety readiness

A grep of `sim_core/src` for mutable statics found none (only a `static` helper function in markings.cpp); not exhaustively audited. `RegionPack` lookups are const. Clipper2 use looks stateless per call. Stage entry points take const graph/junction inputs. Not verified by running concurrently. The bench's own counting allocator is atomic.

## Unmeasured

Large organic and motorway; edit loop at large; scaling exponents; structure meshes (`structure_mesh.cpp` is not built in sim_core); cross-process determinism comparison (in-process repeat hashes are recorded in JSON and were stable in the runs I saw).
