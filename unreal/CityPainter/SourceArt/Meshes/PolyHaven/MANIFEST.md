# Poly Haven vegetation models

CC0 models from Poly Haven fetched 11 Oct 2026 by `tools/fetch_polyhaven_models.mjs` (md5 verified against the API). FBX, 2K textures (tree_small_02: 1K). Raw source art for the vegetation plan (`docs/research/vegetation-plan.md`); Blender conversion to cards/LODs is plan task 4. Machine-readable: `manifest.json` (SHA-256 per file).

Facts that matter for import:
- Normal maps are OpenGL (`nor_gl`, EXR or PNG): flip green for Unreal, or tick the flip-green option on import.
- The FBX files reference textures as `<id>_diff.png` etc. with no folder; the real files are in `textures/` and are mostly JPG/EXR. Materials must be rebuilt/re-linked on import. There is no roughness-vs-ARM packing; roughness is a separate EXR.
- Each plant is one material using base colour + alpha; every leaf/grass material needs alpha masking (alpha texture `*_alpha_2k.png`). tree_small_02 only masks its leaves material; trunk and branches are opaque.
- Mesh units: FBX in cm, Blender import gives metres below (Z up). Many files are multi-plant clusters (several plants laid out along X), so the bbox is the cluster, not one plant.
- Mean colour = mean sRGB (0-255) of the base-colour texture over pixels with alpha > 0.5, measured with Blender (Non-Color read of the file pixels). Not tied to any reference photo.

## grass_medium_02: Grass Medium 02
- URL: https://polyhaven.com/a/grass_medium_02
- Authors: Rico Cilliers (All)
- Licence: CC0 (https://polyhaven.com/license)
- Format/resolution: FBX, 2K; 2.8 MB total
- Mesh: 7842 triangles, 7031 vertices, 5 objects, 1 material(s) (grass_medium_02); bbox 1.311 x 0.433 x 0.405 m (X, Y, Z up)
- Alpha masking: yes, all materials
- Mean colour (sRGB, alpha > 0.5): grass_medium_02_diff_2k.jpg (147.4, 146.5, 100.4; 10% opaque)
- Suggested type: `grass_rough_tuft`. Also grass_tussock. Rough-verge tuft cluster.
- Files: `grass_medium_02_2k.fbx` (0.31 MB), `textures/grass_medium_02_alpha_2k.png` (0.30 MB), `textures/grass_medium_02_diff_2k.jpg` (0.46 MB), `textures/grass_medium_02_nor_gl_2k.exr` (0.65 MB), `textures/grass_medium_02_rough_2k.exr` (1.11 MB)

## weed_plant_02: Weed Plant 02
- URL: https://polyhaven.com/a/weed_plant_02
- Authors: Rob Tuytel (Photography), Rico Cilliers (Modeling)
- Licence: CC0 (https://polyhaven.com/license)
- Format/resolution: FBX, 2K; 10.0 MB total
- Mesh: 11116 triangles, 6568 vertices, 5 objects, 1 material(s) (weed_plant_02); bbox 2.125 x 0.291 x 0.075 m (X, Y, Z up)
- Alpha masking: yes, all materials
- Mean colour (sRGB, alpha > 0.5): weed_plant_02_diff_2k.jpg (75.7, 81.5, 34.5; 50% opaque)
- Suggested type: `weed_creep`. Flat rosette (7.5 cm tall); road-edge creep strip.
- Files: `weed_plant_02_2k.fbx` (0.38 MB), `textures/weed_plant_02_nor_gl_2k.exr` (5.63 MB), `textures/weed_plant_02_alpha_2k.png` (0.42 MB), `textures/weed_plant_02_rough_2k.exr` (1.98 MB), `textures/weed_plant_02_diff_2k.jpg` (1.57 MB)

## nettle_plant: Nettle Plant
- URL: https://polyhaven.com/a/nettle_plant
- Authors: Rob Tuytel (Photography), Rico Cilliers (modeling)
- Licence: CC0 (https://polyhaven.com/license)
- Format/resolution: FBX, 2K; 20.7 MB total
- Mesh: 31304 triangles, 18035 vertices, 6 objects, 1 material(s) (nettle_plant); bbox 1.083 x 0.095 x 0.218 m (X, Y, Z up)
- Alpha masking: yes, all materials
- Mean colour (sRGB, alpha > 0.5): nettle_plant_diff_2k.jpg (87.9, 89.9, 50; 38% opaque)
- Suggested type: `weed_creep`. Rough-verge / hedge-bottom variant (nettle), low priority.
- Files: `nettle_plant_2k.fbx` (1.01 MB), `textures/nettle_plant_rough_2k.exr` (1.46 MB), `textures/nettle_plant_nor_gl_2k.exr` (4.97 MB), `textures/nettle_plant_diff_2k.jpg` (1.20 MB), `textures/nettle_plant_alpha_2k.png` (0.42 MB), `textures/nettle_plant_dry_diff_2k.png` (11.62 MB)

## dandelion_01: Dandelion 01
- URL: https://polyhaven.com/a/dandelion_01
- Authors: Rob Tuytel (Photography), Rico Cilliers (Modeling)
- Licence: CC0 (https://polyhaven.com/license)
- Format/resolution: FBX, 2K; 8.8 MB total
- Mesh: 64099 triangles, 43441 vertices, 20 objects, 1 material(s) (dandelion_01); bbox 4.214 x 0.258 x 0.17 m (X, Y, Z up)
- Alpha masking: yes, all materials
- Mean colour (sRGB, alpha > 0.5): dandelion_01_diff_2k.jpg (122.7, 109.6, 38.2; 49% opaque)
- Suggested type: `weed_creep`. Flowering variant for mown-verge speckle.
- Files: `dandelion_01_2k.fbx` (2.04 MB), `textures/dandelion_01_rough_2k.exr` (1.34 MB), `textures/dandelion_01_nor_gl_2k.exr` (3.63 MB), `textures/dandelion_01_diff_2k.jpg` (1.47 MB), `textures/dandelion_01_mask_2k.png` (0.03 MB), `textures/dandelion_01_alpha_2k.png` (0.30 MB)

## celandine_01: Celandine 01
- URL: https://polyhaven.com/a/celandine_01
- Authors: Rob Tuytel (Photography), Rico Cilliers (modeling)
- Licence: CC0 (https://polyhaven.com/license)
- Format/resolution: FBX, 2K; 7.1 MB total
- Mesh: 8966 triangles, 8499 vertices, 5 objects, 1 material(s) (celandine_01); bbox 2.239 x 0.307 x 0.186 m (X, Y, Z up)
- Alpha masking: yes, all materials
- Mean colour (sRGB, alpha > 0.5): celandine_01_diff_2k.jpg (110.2, 116.4, 45.7; 44% opaque)
- Suggested type: `weed_creep`. Spring bank/verge flower variant, low priority.
- Files: `celandine_01_2k.fbx` (0.36 MB), `textures/celandine_01_alpha_2k.png` (0.63 MB), `textures/celandine_01_diff_2k.jpg` (1.25 MB), `textures/celandine_01_nor_gl_2k.exr` (2.76 MB), `textures/celandine_01_rough_2k.exr` (2.06 MB)

## shrub_01: Shrub 01
- URL: https://polyhaven.com/a/shrub_01
- Authors: Rico Cilliers (All)
- Licence: CC0 (https://polyhaven.com/license)
- Format/resolution: FBX, 2K; 16.3 MB total
- Mesh: 156012 triangles, 91962 vertices, 9 objects, 1 material(s) (shrub_01); bbox 2.587 x 0.219 x 0.396 m (X, Y, Z up)
- Alpha masking: yes, all materials
- Mean colour (sRGB, alpha > 0.5): shrub_01_diff_2k.jpg (122.4, 121, 65.6; 41% opaque)
- Suggested type: `shrub_scrub`. Low scrub, 0.4 m tall (156k tris: needs decimation/cards).
- Files: `shrub_01_2k.fbx` (4.66 MB), `textures/shrub_01_nor_gl_2k.exr` (5.72 MB), `textures/shrub_01_rough_2k.exr` (1.07 MB), `textures/shrub_01_diff_2k.jpg` (1.65 MB), `textures/shrub_01_disp_2k.png` (2.92 MB), `textures/shrub_01_alpha_2k.png` (0.30 MB)

## shrub_02: Shrub 02
- URL: https://polyhaven.com/a/shrub_02
- Authors: Rico Cilliers (All)
- Licence: CC0 (https://polyhaven.com/license)
- Format/resolution: FBX, 2K; 6.9 MB total
- Mesh: 27254 triangles, 18311 vertices, 4 objects, 1 material(s) (shrub_02); bbox 4.919 x 1.857 x 1.383 m (X, Y, Z up)
- Alpha masking: yes, all materials
- Mean colour (sRGB, alpha > 0.5): shrub_02_diff_2k.jpg (106.5, 84, 59.4; 34% opaque)
- Suggested type: `shrub_scrub`. Largest shrub, 1.4 m tall.
- Files: `shrub_02_2k.fbx` (0.83 MB), `textures/shrub_02_rough_2k.exr` (0.98 MB), `textures/shrub_02_nor_gl_2k.exr` (3.57 MB), `textures/shrub_02_mask_2k.png` (0.03 MB), `textures/shrub_02_diff_2k.jpg` (1.32 MB), `textures/shrub_02_alpha_2k.png` (0.20 MB)

## shrub_03: Shrub 03
- URL: https://polyhaven.com/a/shrub_03
- Authors: Rico Cilliers (All)
- Licence: CC0 (https://polyhaven.com/license)
- Format/resolution: FBX, 2K; 5.8 MB total
- Mesh: 8287 triangles, 5882 vertices, 4 objects, 1 material(s) (shrub_03); bbox 1.367 x 0.151 x 0.403 m (X, Y, Z up)
- Alpha masking: yes, all materials
- Mean colour (sRGB, alpha > 0.5): shrub_03_diff_2k.jpg (133.1, 131, 44.6; 25% opaque)
- Suggested type: `shrub_scrub`. Low scrub, 0.4 m, cheapest (8k tris).
- Files: `shrub_03_2k.fbx` (0.29 MB), `textures/shrub_03_alpha_2k.png` (0.18 MB), `textures/shrub_03_diff_2k.jpg` (1.52 MB), `textures/shrub_03_rough_2k.exr` (0.93 MB), `textures/shrub_03_nor_gl_2k.exr` (2.89 MB)

## shrub_04: Shrub 04
- URL: https://polyhaven.com/a/shrub_04
- Authors: Rico Cilliers (All)
- Licence: CC0 (https://polyhaven.com/license)
- Format/resolution: FBX, 2K; 6.0 MB total
- Mesh: 27327 triangles, 18287 vertices, 4 objects, 1 material(s) (shrub_04); bbox 0.747 x 0.187 x 0.28 m (X, Y, Z up)
- Alpha masking: yes, all materials
- Mean colour (sRGB, alpha > 0.5): shrub_04_diff_2k.jpg (117.5, 102.8, 80; 38% opaque)
- Suggested type: `shrub_scrub`. Small shrub, 0.28 m.
- Files: `shrub_04_2k.fbx` (0.85 MB), `textures/shrub_04_nor_gl_2k.exr` (3.00 MB), `textures/shrub_04_alpha_2k.png` (0.09 MB), `textures/shrub_04_mask_2k.png` (0.02 MB), `textures/shrub_04_diff_2k.jpg` (1.20 MB), `textures/shrub_04_rough_2k.exr` (0.84 MB)

## moss_01: Moss 01
- URL: https://polyhaven.com/a/moss_01
- Authors: Rob Tuytel (All)
- Licence: CC0 (https://polyhaven.com/license)
- Format/resolution: FBX, 2K; 6.0 MB total
- Mesh: 204 triangles, 204 vertices, 12 objects, 1 material(s) (moss_01); bbox 0.215 x 0.029 x 0.04 m (X, Y, Z up)
- Alpha masking: yes, all materials
- Mean colour (sRGB, alpha > 0.5): moss_01_diff_2k.jpg (85.3, 84.7, 30.7; 6% opaque)
- Suggested type: `unassigned`. Candidate for kerb-base / wall-foot moss decals; not in the plan.
- Files: `moss_01_2k.fbx` (0.05 MB), `textures/moss_01_alpha_2k.png` (0.17 MB), `textures/moss_01_diff_2k.jpg` (0.51 MB), `textures/moss_01_nor_gl_2k.exr` (3.63 MB), `textures/moss_01_rough_2k.exr` (1.62 MB)

## tree_small_02: Tree Small 02
- URL: https://polyhaven.com/a/tree_small_02
- Authors: Rico Cilliers (All)
- Licence: CC0 (https://polyhaven.com/license)
- Format/resolution: FBX, 1K; 95.8 MB total
- Mesh: 2062487 triangles, 1755568 vertices, 1 objects, 3 material(s) (tree_small_02_branches, tree_small_02_leaves, tree_small_02_trunk); bbox 2.917 x 4.292 x 4.557 m (X, Y, Z up)
- Alpha masking: leaves material only
- Mean colour (sRGB, alpha > 0.5): tree_small_02_branch_diff_1k.png (115.6, 102, 90.3; 100% opaque); tree_small_02_diff_1k.jpg (101.4, 88, 76.4; 100% opaque); tree_small_02_leaves_diff_1k.png (102.2, 119.2, 58.5; 54% opaque)
- Suggested type: `tree_sapling_staked (fallback tree_broadleaf_open)`. Plan task 3 names it as the fallback if PVE trees fail the packaged-build test. 2.06M tris: must be decimated or converted to cards before use.
- Files: `tree_small_02_1k.fbx` (75.49 MB), `textures/tree_small_02_branch_diff_1k.png` (1.28 MB), `textures/tree_small_02_branch_nor_gl_1k.png` (5.75 MB), `textures/tree_small_02_branch_rough_1k.png` (0.42 MB), `textures/tree_small_02_diff_1k.jpg` (0.86 MB), `textures/tree_small_02_leaves_alpha_1k.png` (0.26 MB), `textures/tree_small_02_leaves_diff_1k.png` (2.34 MB), `textures/tree_small_02_leaves_nor_gl_1k.png` (4.90 MB), `textures/tree_small_02_leaves_rough_1k.png` (1.04 MB), `textures/tree_small_02_nor_gl_1k.exr` (2.46 MB), `textures/tree_small_02_rough_1k.exr` (0.98 MB)

## Not fetched
- `fir_tree_01` (7.85M polygons; FBX 249 MB alone, 329 MB at 2K, 271 MB at 1K) and `pine_tree_01` (17.4M polygons; FBX 649 MB alone, 854 MB at 2K, 704 MB at 1K). Both are geometry-nodes bakes far beyond a usable budget for the planned `tree_conifer_plantation`. Held back pending a decision.

Total fetched: 186 MB.
