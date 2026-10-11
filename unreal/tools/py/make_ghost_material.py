"""Creates /Game/CityPainter/Materials/M_Ghost: the road tool's ghost ribbon. Unlit, translucent, depth test off (the
prototype's ghost shows through hills), emissive = Color.rgb, opacity = Color.a (vector parameter "Color").
Run: powershell -NoProfile -ExecutionPolicy Bypass -File unreal\tools\Run-EditorPython.ps1 -Script make_ghost_material"""
import os
import sys

sys.path.insert(0, os.environ.get("CP_PY_DIR", os.path.dirname(os.path.abspath(__file__))))
import unreal
import cp_common as cp
import mat_lib as ml


def main():
    mb = ml.MB("M_Ghost", "/Game/CityPainter/Materials", blend=unreal.BlendMode.BLEND_TRANSLUCENT)
    mb.m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mb.m.set_editor_property("two_sided", True)
    mb.m.set_editor_property("disable_depth_test", True)
    color = mb.vp("Color", (0.87, 0.9, 0.94), "Ghost")
    mb.out("MP_EMISSIVE_COLOR", color)
    mb.out("MP_OPACITY", (color, "A"))
    mb.finish()
    cp.log("M_Ghost created")


cp.run(main)
