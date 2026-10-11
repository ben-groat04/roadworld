import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from veg_common import *
import veg_render as R, veg_hedge as H
reset_scene()
R.setup(900, 500, 24)
cam = bpy.context.scene.camera
gm = R.flat_material("ground", (0.12, 0.2, 0.05))
R.plane("g", -30, 30, -30, 30, 0, gm)
x = -9
import sys
kind = sys.argv[-1] if sys.argv[-1].startswith("hedge") else "hedge_trimmed"
for v in ("V00", "V01", "V03", "E00", "E01"):
    vm = H.build_hedge(kind, H.HEDGES[kind][v], 0)
    ob = vm.to_object(); s = stats(ob); print(kind, v, s["tris"], s["dims"])
    ob.location = (x, 0, 0); x += 4.6
R.figure(-11, -2)
R.look_at(cam, (-1, -13, 3.2), (-1, 0, 1.2), 38)
R.render("//../../../docs/progress/_t_%s.png" % kind)
