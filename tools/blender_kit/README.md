# City Painter Blender kit

Headless bpy scripts that generate the instanced street-kit meshes (kerbs, drainage, lighting columns, bollards,
barriers, masonry) and export FBX + `kit_manifest.json` into `unreal/CityPainter/SourceArt/Meshes/Kit/`.
No add-ons, no GUI, no downloads. Needs Blender 5.x (tested 5.2.2).

## Build (all pieces, about 10 s)

    "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" --background --factory-startup --python tools/blender_kit/build_all.py --

Options after `--`: `--only Kerbs,Drainage,Lighting,Bollards,Barriers,Masonry`, `--params my.json` (deep-merged over
`params.py`), `--out DIR`. Deterministic and idempotent (overwrites outputs; `--only` keeps other manifest entries).

## Previews (Cycles, CUDA GPU; OptiX fails on this machine; `KIT_GPU=OPTIX` to try it)

    blender --background --factory-startup --python tools/blender_kit/render_previews.py -- --cats Kerbs,Drainage,Lighting,Bollards,Barriers,Masonry --samples 24
    blender --background --factory-startup --python tools/blender_kit/render_previews.py -- --cats none --assembly --samples 32

Writes `tools/blender_kit/preview/` (git-ignored): `sheet_<Category>.png`, `street_assembly.png`, `closeup_*.png`.
Add `--proxy` to render the proxies.

## FBX axis/scale check

    blender --background --factory-startup --python tools/blender_kit/verify_fbx.py -- export %TEMP%/axis_test.fbx
    blender --background --factory-startup --python tools/blender_kit/verify_fbx.py -- check  %TEMP%/axis_test.fbx

Exports a 1 (X) x 2 (Y) x 3 (Z) m box and re-imports it in a fresh process: expects 1/2/3 m with the importer's unit
handling and 100/200/300 raw (cm) values.

## Conventions

* Local frame of every piece: **+X along the run, +Y toward the carriageway/traffic, +Z up**; units metres in Blender,
  FBX in cm (1 m = 100 uu, import scale 1.0). Export axes forward -Z, up Y (Blender defaults).
* Pivots: kerbs at the road-side bottom edge of the start face (body at -Y); covers at the frame centre, top at +3 mm
  (gully +6 mm); columns/bollards/barriers at ground level on the axis; copings at the front-face bottom edge.
* Joints: units are modelled 5 mm shorter than their pitch (0.915 m kerb, 4/3/2/1 m modules).
* UVs: real-world, **UV 1.0 = 1 m**; tile materials in Unreal as UV / `size_m` from the PolyHaven manifest.
* Material slots: `kerb_concrete`, `structural_concrete`, `pavement`, new: `galvanised_steel`, `painted_steel`,
  `cast_iron`, `lantern_glass`, `plastic_black`, `plastic_white`, `timber`.
* Each mesh has a `_proxy` FBX (decimated or 12-triangle box; explicit for columns).
* Tunable dimensions: `params.py` (one dict per category).
