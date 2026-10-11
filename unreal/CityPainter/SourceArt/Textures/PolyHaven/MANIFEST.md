# Poly Haven starter texture library

CC0 PBR sets from Poly Haven, 2K JPG, DirectX normals (`nor_dx`, correct for Unreal). Raw source art; imported to `/Game/Materials/Surfaces/<slot>/` by `unreal/tools/py/import_textures.py` using `manifest.json` (machine-readable, includes SHA-256 per file).
Maps per set: `diff` (base colour), `nor_dx`, `rough`, `ao`, `disp` (height), `arm` (packed AO/Rough/Metal). Poly Haven's info JSON carries no licence field; all Poly Haven assets are CC0 per polyhaven.com/license. `size_m` is the real-world edge length of the (square) texture from the API `dimensions` field.

## asphalt_new: Asphalt 01 (`asphalt_01`)
- URL: https://polyhaven.com/a/asphalt_01
- Authors: Dario Barresi, Charlotte Baglioni
- Licence: CC0 (Poly Haven site-wide licence)
- Real-world size covered: 2.08 m x 2.08 m (tiling in metres = 2.08)
- Resolution: 2048x2048, JPG
- Why: Dark, fine-grained asphalt with subtle wear. MEASURED mean sRGB (89, 80, 70): a strong WARM/brown cast; UK tarmac in the reference photos is neutral to slightly cool grey, so M_RoadAsphalt desaturates it (60%) and applies a cool tint.
- Files:
  - `asphalt_01_diff_2k.jpg` (3.12 MB)
  - `asphalt_01_nor_dx_2k.jpg` (4.85 MB)
  - `asphalt_01_rough_2k.jpg` (1.60 MB)
  - `asphalt_01_ao_2k.jpg` (4.05 MB)
  - `asphalt_01_disp_2k.jpg` (1.39 MB)
  - `asphalt_01_arm_2k.jpg` (4.17 MB)

## asphalt_worn: Asphalt 02 (`asphalt_02`)
- URL: https://polyhaven.com/a/asphalt_02
- Authors: Rob Tuytel
- Licence: CC0 (Poly Haven site-wide licence)
- Real-world size covered: 3.0 m x 3.0 m (tiling in metres = 3.0)
- Resolution: 2048x2048, JPG
- Why: Grey asphalt with long cracks and bitumen patches; wear/crack layer. MEASURED mean sRGB (90, 90, 84): near-neutral, very slightly warm.
- Files:
  - `asphalt_02_diff_2k.jpg` (3.08 MB)
  - `asphalt_02_nor_dx_2k.jpg` (5.05 MB)
  - `asphalt_02_rough_2k.jpg` (2.23 MB)
  - `asphalt_02_ao_2k.jpg` (2.04 MB)
  - `asphalt_02_disp_2k.jpg` (1.13 MB)
  - `asphalt_02_arm_2k.jpg` (1.30 MB)

## pavement: Concrete Pavement (`concrete_pavement`)
- URL: https://polyhaven.com/a/concrete_pavement
- Authors: Charlotte Baglioni
- Licence: CC0 (Poly Haven site-wide licence)
- Real-world size covered: 1.8 m x 1.8 m (tiling in metres = 1.8)
- Resolution: 2048x2048, JPG
- Why: Concrete footway flags in a running bond (about 600 x 300 mm per 1.8 m tile). MEASURED mean sRGB (127, 116, 102): warm; M_Footway desaturates it toward neutral grey.
- Files:
  - `concrete_pavement_diff_2k.jpg` (3.82 MB)
  - `concrete_pavement_nor_dx_2k.jpg` (5.18 MB)
  - `concrete_pavement_rough_2k.jpg` (2.13 MB)
  - `concrete_pavement_ao_2k.jpg` (3.58 MB)
  - `concrete_pavement_disp_2k.jpg` (2.49 MB)
  - `concrete_pavement_arm_2k.jpg` (3.47 MB)

## kerb_concrete: Concrete Wall 009 (`concrete_wall_009`)
- URL: https://polyhaven.com/a/concrete_wall_009
- Authors: Charlotte Baglioni
- Licence: CC0 (Poly Haven site-wide licence)
- Real-world size covered: 1.805 m x 1.805 m (tiling in metres = 1.805)
- Resolution: 2048x2048, JPG
- Why: Cast concrete with fine form lines. MEASURED mean sRGB (161, 153, 139): WARM/cream, brighter than UK kerbs (neutral grey in the photos); M_Kerb desaturates and darkens it.
- Files:
  - `concrete_wall_009_diff_2k.jpg` (2.57 MB)
  - `concrete_wall_009_nor_dx_2k.jpg` (2.22 MB)
  - `concrete_wall_009_rough_2k.jpg` (3.07 MB)
  - `concrete_wall_009_ao_2k.jpg` (1.37 MB)
  - `concrete_wall_009_disp_2k.jpg` (0.76 MB)
  - `concrete_wall_009_arm_2k.jpg` (3.60 MB)

## structural_concrete: Concrete (`concrete`)
- URL: https://polyhaven.com/a/concrete
- Authors: Rob Tuytel
- Licence: CC0 (Poly Haven site-wide licence)
- Real-world size covered: 4.0 m x 4.0 m (tiling in metres = 4.0)
- Resolution: 2048x2048, JPG
- Why: Board-marked grey concrete with tie holes and damp staining; typical UK retaining wall/bridge abutment.
- Files:
  - `concrete_diff_2k.jpg` (2.27 MB)
  - `concrete_nor_dx_2k.jpg` (0.60 MB)
  - `concrete_rough_2k.jpg` (0.96 MB)
  - `concrete_ao_2k.jpg` (1.54 MB)
  - `concrete_disp_2k.jpg` (0.57 MB)
  - `concrete_arm_2k.jpg` (0.56 MB)

## wall_brick: Brick Wall 005 (`brick_wall_005`)
- URL: https://polyhaven.com/a/brick_wall_005
- Authors: Dario Barresi, Dimitrios Savva
- Licence: CC0 (Poly Haven site-wide licence)
- Real-world size covered: 1.44 m x 1.44 m (tiling in metres = 1.44)
- Resolution: 2048x2048, JPG
- Why: Weathered mixed red/blue engineering brick with lime mortar; very UK Victorian railway/retaining wall look.
- Files:
  - `brick_wall_005_diff_2k.jpg` (3.51 MB)
  - `brick_wall_005_nor_dx_2k.jpg` (4.34 MB)
  - `brick_wall_005_rough_2k.jpg` (1.56 MB)
  - `brick_wall_005_ao_2k.jpg` (2.86 MB)
  - `brick_wall_005_disp_2k.jpg` (0.93 MB)
  - `brick_wall_005_arm_2k.jpg` (2.79 MB)

## grass: Sparse Grass (`sparse_grass`)
- URL: https://polyhaven.com/a/sparse_grass
- Authors: Amal Kumar
- Licence: CC0 (Poly Haven site-wide licence)
- Real-world size covered: 2.0 m x 2.0 m (tiling in metres = 2.0)
- Resolution: 2048x2048, JPG
- Why: Short turf with soil showing. MEASURED mean sRGB (79, 61, 21): BROWN-orange, not green; M_Ground desaturates 85% and tints green-grey.
- Files:
  - `sparse_grass_diff_2k.jpg` (3.73 MB)
  - `sparse_grass_nor_dx_2k.jpg` (5.86 MB)
  - `sparse_grass_rough_2k.jpg` (1.73 MB)
  - `sparse_grass_ao_2k.jpg` (3.90 MB)
  - `sparse_grass_disp_2k.jpg` (2.31 MB)
  - `sparse_grass_arm_2k.jpg` (3.84 MB)

## soil: Forest Ground 06 (`forest_ground_06`)
- URL: https://polyhaven.com/a/forest_ground_06
- Authors: Charlotte Baglioni
- Licence: CC0 (Poly Haven site-wide licence)
- Real-world size covered: 2.11 m x 2.11 m (tiling in metres = 2.11)
- Resolution: 2048x2048, JPG
- Why: Compact soil. MEASURED mean sRGB (90, 68, 47): warm brown; M_Ground desaturates it 75% for the dirt patches.
- Files:
  - `forest_ground_06_diff_2k.jpg` (3.38 MB)
  - `forest_ground_06_nor_dx_2k.jpg` (5.78 MB)
  - `forest_ground_06_rough_2k.jpg` (2.76 MB)
  - `forest_ground_06_ao_2k.jpg` (4.01 MB)
  - `forest_ground_06_disp_2k.jpg` (1.85 MB)
  - `forest_ground_06_arm_2k.jpg` (4.21 MB)

## rock: Rock Face 04 (`rock_face_04`)
- URL: https://polyhaven.com/a/rock_face_04
- Authors: Dimitrios Savva
- Licence: CC0 (Poly Haven site-wide licence)
- Real-world size covered: 2.0 m x 2.0 m (tiling in metres = 2.0)
- Resolution: 2048x2048, JPG
- Why: Dark grey-brown weathered, fractured rock face; sober temperate cutting rock rather than desert sandstone.
- Files:
  - `rock_face_04_diff_2k.jpg` (3.93 MB)
  - `rock_face_04_nor_dx_2k.jpg` (4.41 MB)
  - `rock_face_04_rough_2k.jpg` (1.92 MB)
  - `rock_face_04_ao_2k.jpg` (1.91 MB)
  - `rock_face_04_disp_2k.jpg` (0.75 MB)
  - `rock_face_04_arm_2k.jpg` (2.51 MB)

## gravel: Gravel Floor 02 (`gravel_floor_02`)
- URL: https://polyhaven.com/a/gravel_floor_02
- Authors: Jenelle van Heerden, Dimitrios Savva
- Licence: CC0 (Poly Haven site-wide licence)
- Real-world size covered: 2.0 m x 2.0 m (tiling in metres = 2.0)
- Resolution: 2048x2048, JPG
- Why: Fine pale gravel, even and tileable. MEASURED mean sRGB (171, 167, 156): neutral-light, slightly warm; used darkened (x0.35) for the road-edge fringe.
- Files:
  - `gravel_floor_02_diff_2k.jpg` (4.70 MB)
  - `gravel_floor_02_nor_dx_2k.jpg` (5.88 MB)
  - `gravel_floor_02_rough_2k.jpg` (2.70 MB)
  - `gravel_floor_02_ao_2k.jpg` (4.28 MB)
  - `gravel_floor_02_disp_2k.jpg` (2.16 MB)
  - `gravel_floor_02_arm_2k.jpg` (4.20 MB)

Total on disk: 173.5 MB
