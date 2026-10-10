"""FBX axis/scale verification.
  blender --background --factory-startup --python verify_fbx.py -- export <out.fbx>
  blender --background --factory-startup --python verify_fbx.py -- check <in.fbx>
export writes a 1 (X) x 2 (Y) x 3 (Z) m box whose min corner is the origin; check re-imports it in a fresh
process, once with unit scale applied (expect 1,2,3 m) and once raw (expect 100,200,300 = cm = Unreal uu).
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy
import common as C

argv = sys.argv[sys.argv.index("--") + 1:]
mode, path = argv[0], argv[1]


def bounds(ob):
    vs = [ob.matrix_world @ v.co for v in ob.data.vertices]
    return [round(max(v[i] for v in vs) - min(v[i] for v in vs), 4) for i in range(3)], \
           [round(min(v[i] for v in vs), 4) for i in range(3)]


if mode == "export":
    C.reset_scene()
    b = C.Builder("SM_TestBox")
    b.box(0, 1, 0, 2, 0, 3, "kerb_concrete")
    C.export_fbx(b.to_object(), path)
    print("VERIFY exported", path)
else:
    for apply_unit in (True, False):
        C.reset_scene()
        bpy.ops.import_scene.fbx(filepath=path, axis_forward='-Z', axis_up='Y', global_scale=1.0,
                                 use_manual_orientation=True, bake_space_transform=False) if False else None
        # defaults of the importer round-trip the exporter's axis settings
        bpy.ops.import_scene.fbx(filepath=path, global_scale=1.0)
        ob = [o for o in bpy.context.scene.objects if o.type == 'MESH'][0]
        d, mn = bounds(ob)
        print("VERIFY importer-default: dims(m, Blender X,Y,Z) =", d, "min =", mn, "obj scale", tuple(ob.scale), "rot", tuple(ob.rotation_euler))
        break
    # raw value check: scale-free read of vertex data with the importer's own scale undone
    C.reset_scene()
    bpy.ops.import_scene.fbx(filepath=path, global_scale=100.0)
    ob = [o for o in bpy.context.scene.objects if o.type == 'MESH'][0]
    print("VERIFY global_scale=100 import (=> raw file units if file is in metres... see below): dims =", bounds(ob)[0])
    C.reset_scene()
    bpy.ops.import_scene.fbx(filepath=path, global_scale=1.0, use_custom_props=False)
    # read the FBX header unit scale
    import struct
    with open(path, 'rb') as fh:
        data = fh.read()
    i = data.find(b'UnitScaleFactor')
    print("VERIFY UnitScaleFactor bytes:", data[i:i + 40])
