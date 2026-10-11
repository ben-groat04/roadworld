# Infinigen pre-flight (read-only), 11 Oct 2026

Tags: [V] read on a page or measured on this machine this pass; [U] inference or unverified. Nothing was installed or changed.

## 1. This machine [V, commands run]
- WSL: one distro, `Ubuntu`, state Running, WSL version 2. Ubuntu 26.04.1 LTS, 16 CPUs, 11 GB RAM total (6 used, 5 available while the CFD job may be running), root disk 1007 GB with 946 GB free. Python 3.14.4 in WSL; no conda, mamba or blender; git present.
- GPU: `nvidia-smi` works in WSL and shows NVIDIA GeForce RTX 4060 Laptop GPU, 8188 MiB. (Note: the laptop GPU is a 4060, not a 3060.)
- Windows: C: has 118 GB free of 510 GB. Blender 4.2 and Blender 5.2 are installed under `C:\Program Files\Blender Foundation\`. Only Python 3.14 (py launcher); no 3.11.
- Constraint: Infinigen needs Python 3.11, which neither side has. `uv` (not installed) can fetch 3.11 into a venv by itself; conda would work but is not installed either. The 11 GB WSL RAM ceiling is the real risk if the CFD job is running: a Blender tree bake can use several GB.

## 2. Infinigen today
Pages read: infinigen.cs.princeton.edu/docs/latest (Installation, GeneratingIndividualAssets, ExportingToExternalFileFormats, ConfiguringInfinigen), the GitHub repo front page and releases page. GitHub `docs/*.md` blob URLs returned 404.
- Version: latest release 2.0.0a3 (alpha, "Add new roomtypes, assets, house tours"); the page shows "08 Oct" without a year, so presumably 2026 [V date, U year]. Previous: 2.0.0a1, v1.19.0, v1.17.0. A DeepWiki summary says the pinned bpy is 4.2.0 [U, secondary source]; the official install page gives no Blender version.
- Platform [V, install page]: minimal install (objects and materials) is "experimental" on native Windows and on WSL2 ("feasible but untested and undocumented"). Terrain is "no" on native Windows, "experimental" on WSL2. So native Windows does support the minimal install in principle, but only experimentally.
- Install, minimal path [V]: `git clone https://github.com/princeton-vl/infinigen.git; cd infinigen; git submodule update --init; uv venv --python 3.11; INFINIGEN_MINIMAL_INSTALL=True uv pip install -e .`. `bpy` arrives as a pip dependency; no separate Blender needed. Alternative: `INFINIGEN_MINIMAL_INSTALL=True bash scripts/install/interactive_blender.sh` (standalone Blender, Linux/Mac/WSL). Docker also documented. Minimal install skips terrain and C++ builds.
- Disk/GPU [V by absence]: the docs state no disk, RAM or GPU requirement; minimal install needs no GPU (CUDA only speeds terrain). My estimate: bpy wheel plus deps roughly 1-3 GB, repo plus submodules under 1 GB [U].
- Single assets [V]: `python -m infinigen_examples.generate_individual_assets -f <Factory> -n N --save_blend` (docs show `CoralFactory`, `Seashells`, `ChunkyRock`; `BushFactory` in the export example). List files `tests/infinigen/assets/list_nature_meshes.txt` and `list_nature_materials.txt` name the available mesh and material factories; I could not read them, so exact tree, fern, grass and flower factory names are [U]. The nature module imports `trees`, `monocot`, `fern`, `grass`, `decorative_plants` [V via search summary of generate_nature.py], which suggests trees, grasses/monocots, ferns and flowering plants exist as factories.
- Seeds: `--specific_seed` exists for full scenes [V]; for individual assets only `-n` is documented, so variants likely come from per-index seeds [U].
- Timing: no per-asset timings anywhere in the docs [V by absence]. Docs call forest scenes "very expensive" and trees "especially expensive".
- Export [V, export page]: `python -m infinigen.tools.export --input_folder <blends> --output_folder out -f fbx|obj|stl|ply|usdc -r 1024 [--individual] [-v]`. Bakes procedural materials to Albedo, Roughness, Normal, Metallic only; slow and lossy. Only .blend and .usdc keep instancing; other formats realise every instance. Trees use instancing for leaves and branches, so whole-tree OBJ/FBX export is "generally unsupported" (may work at very low resolution or with leaves/branches off). Transparent materials may export with wrong textures; translucent leaves not reproduced; FBX can fail on invalid UVs. Nothing about leaf alpha cards, LODs, wind or pivot setup.
- LOD/density [V]: `target_face_size.global_multiplier` (4 or higher to cut plant cost, diminishing returns); `compose_nature.generate_resolution`. Resolution is keyed to closest camera distance, so a single-asset run has no true LOD chain; decimation would be our job.
- Licence: code BSD-3-Clause; repo notes some procedural code taken from the internet under CC0 [V]. Whether any bundled textures/data are non-BSD is not stated [U]; generated meshes are our output, but check before shipping.
- WSL2 problems: none documented beyond "experimental" [V]. Docker on Windows needs WSL2 + Docker Desktop.

## 3. Fit for the vegetation plan
| Plan type id | Infinigen | Verdict |
|---|---|---|
| tree_broadleaf_round / open | Tree factories likely give generic oak/maple/birch-like forms [U]; not UK-calibrated | Maybe; leaf realisation makes meshes enormous |
| tree_conifer_plantation | Pine/fir-like factory likely [U] | Maybe |
| hedge_trimmed / hedge_overgrown | No hedge generator; a trimmed box cannot come from it | No; Blender kit is the route |
| shrub_scrub / shrub_gorse | `BushFactory` [V name]; gorse-like unlikely | Scrub yes, gorse no |
| grass_tussock / grass_rough_tuft | grass/monocot factories likely [U] | Plausible, but as cards the Blender kit or Poly Haven is cheaper |
| bracken_patch | fern factory likely [U] | Plausible |
| rocks, cutting faces, ground | `ChunkyRock` [V]; terrain is not available on native Windows, experimental on WSL2; ground materials bake to 4 maps | Rocks and rough-ground material: the one clear win for cutting faces and macro textures |

Comparison (honest, all [U] on Infinigen output until tested):
- Realism: Infinigen highest for individual plants and rocks; scripted kit stylised but controllable; PVE good broadleaf trees, nothing else.
- Effort: Infinigen install risk (alpha, experimental platform, unknown factory names) plus per-asset clean-up; kit is in progress already; PVE is manual GUI work.
- Automation: Infinigen and the Blender kit both run headless from a script; PVE has no Python entry points seen.
- Performance: Infinigen meshes are realised per leaf, probably 0.5M to several M triangles per tree; needs decimation, leaf-to-card conversion and LODs, which is most of the work and is where it will likely fail for trees. Rocks and bushes are far less affected. RTX 3060 budget in the plan is 8-20k tris trunk + cards per tree.

## 4. Proposal: smallest worthwhile experiment
Where: WSL2 Ubuntu, new directory `~/infinigen-test/` with its own `uv` venv (Python 3.11 fetched by uv). The CFD environment is untouched. Do not run while the OpenFOAM job is using the RAM (check `free -g` first; need about 6 GB free). Native Windows is a fallback only.
Download list for approval:
1. `uv` installer, from astral.sh, about 20-40 MB.
2. Python 3.11 managed by uv, about 30-60 MB.
3. `git clone princeton-vl/infinigen` with submodules, roughly 0.5-1 GB [U].
4. pip dependencies including `bpy` (Blender 4.2 module), roughly 1-3 GB [U].
Total around 2-4 GB in WSL; Windows C: untouched.
Run: list `list_nature_meshes.txt`; generate 2 BushFactory, 3 grass/fern, 2 tree factories (seeds 1-3), 2 ChunkyRock with `--save_blend`; export with `-f fbx -r 1024 --individual`; import beside Blender-kit equivalents in one Unreal lighting setup.
Time: about 1 hour setup, 1-2 hours generation and review [U].
Stop conditions: (a) install fails after one retry on bpy/Python; (b) a tree's realised FBX exceeds ~1M triangles or does not export; (c) leaves cannot be turned into cards without hand work; (d) RAM pressure threatens the CFD job. Rocks and bushes alone passing is still worth keeping.

Recommendation: conditional GO for rocks, bushes, ferns and grass only; NO-GO for trees and hedges (use PVE and the Blender kit). Wait for approval of the download list.

## Sources
- https://infinigen.cs.princeton.edu/docs/latest/Installation.html
- https://infinigen.cs.princeton.edu/docs/latest/GeneratingIndividualAssets.html
- https://infinigen.cs.princeton.edu/docs/latest/ExportingToExternalFileFormats.html
- https://infinigen.cs.princeton.edu/docs/latest/ConfiguringInfinigen.html
- https://github.com/princeton-vl/infinigen and /releases
- https://deepwiki.com/princeton-vl/infinigen/1.1-installation-and-setup (search snippet only)
