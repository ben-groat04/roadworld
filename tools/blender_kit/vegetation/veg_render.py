"""Preview rendering helpers (Cycles, low samples) for the vegetation kit. Blender headless."""
import bpy, math, os, random
import numpy as np
from mathutils import Vector
from veg_common import get_material, Z


def setup(w=640, h=640, samples=16, sun_elev=38, sun_az=40, sun_strength=3.2, sky_strength=1.0, overcast=False):
    sc = bpy.context.scene
    sc.render.engine = 'CYCLES'
    sc.render.resolution_x, sc.render.resolution_y = w, h
    sc.render.film_transparent = False
    cy = sc.cycles
    cy.samples = samples
    cy.use_denoising = True
    try:
        cy.denoiser = 'OPENIMAGEDENOISE'
    except Exception:
        pass
    try:
        prefs = bpy.context.preferences.addons['cycles'].preferences
        prefs.compute_device_type = os.environ.get("KIT_GPU", "CUDA")
        prefs.get_devices()
        for d in prefs.devices:
            d.use = d.type != 'CPU'
        cy.device = 'GPU'
    except Exception as e:
        print("gpu setup failed", e)
    cy.max_bounces = 4
    sc.view_settings.view_transform = 'Standard'
    try:
        sc.view_settings.view_transform = 'AgX'
    except TypeError:
        pass
    wd = bpy.data.worlds.new("w")
    sc.world = wd
    wd.use_nodes = True
    nt = wd.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new("ShaderNodeOutputWorld")
    bg = nt.nodes.new("ShaderNodeBackground")
    sky = nt.nodes.new("ShaderNodeTexSky")
    try:
        sky.sky_type = 'MULTIPLE_SCATTERING'
        sky.sun_elevation = math.radians(sun_elev)
        sky.sun_rotation = math.radians(sun_az)
        sky.sun_disc = False
    except Exception as e:
        print("sky", e)
    nt.links.new(sky.outputs[0], bg.inputs[0])
    bg.inputs[1].default_value = sky_strength
    nt.links.new(bg.outputs[0], out.inputs[0])
    sun = bpy.data.lights.new("sun", 'SUN')
    sun.energy = sun_strength if not overcast else 1.0
    sun.angle = math.radians(1.5 if not overcast else 12)
    so = bpy.data.objects.new("sun", sun)
    bpy.context.scene.collection.objects.link(so)
    # sun direction from elevation/azimuth (azimuth measured so that the light comes from -Y,+X side)
    el, az = math.radians(sun_elev), math.radians(sun_az)
    d = Vector((math.cos(el) * math.sin(az), -math.cos(el) * math.cos(az), math.sin(el)))
    so.rotation_euler = (-d).to_track_quat('-Z', 'Y').to_euler()
    so.rotation_euler = d.to_track_quat('Z', 'Y').to_euler()
    cam = bpy.data.cameras.new("cam")
    co = bpy.data.objects.new("cam", cam)
    bpy.context.scene.collection.objects.link(co)
    sc.camera = co
    return co


def look_at(cam, loc, target, lens=40):
    cam.location = loc
    cam.data.lens = lens
    cam.data.clip_end = 3000
    cam.rotation_euler = (Vector(target) - Vector(loc)).to_track_quat('-Z', 'Y').to_euler()


def flat_material(name, rgb, rough=0.9):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    b.inputs['Base Color'].default_value = (*rgb, 1)
    b.inputs['Roughness'].default_value = rough
    try:
        b.inputs['Specular IOR Level'].default_value = 0.1
    except KeyError:
        pass
    return m


def plane(name, x0, x1, y0, y1, z, mat):
    me = bpy.data.meshes.new(name)
    me.from_pydata([(x0, y0, z), (x1, y0, z), (x1, y1, z), (x0, y1, z)], [], [(0, 1, 2, 3)])
    me.materials.append(mat)
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    return ob


def figure(x, y, height=1.8):
    """Simple 1.8 m scale figure (capsule-ish body + head), grey."""
    m = flat_material("figure", (0.55, 0.12, 0.1), 0.7)
    objs = []
    bpy.ops.mesh.primitive_cylinder_add(radius=0.2, depth=height - 0.45, location=(x, y, (height - 0.45) / 2 + 0.0))
    b = bpy.context.active_object
    b.data.materials.append(m)
    bpy.ops.mesh.primitive_uv_sphere_add(radius=0.12, location=(x, y, height - 0.15))
    h = bpy.context.active_object
    h.data.materials.append(m)
    b.scale.y = 0.7
    return [b, h]


def import_fbx(path, tint=(1, 1, 1)):
    for _s in ("veg_leaf", "veg_grass", "veg_bark", "veg_core", "veg_canopy_far", "veg_timber"):
        get_material(_s)
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=path, global_scale=1.0, use_custom_normals=True, axis_forward='-Z', axis_up='Y')
    new = [o for o in bpy.data.objects if o not in before and o.type == 'MESH']
    ob = new[0]
    for o in bpy.data.objects:
        if o not in before and o.type != 'MESH':
            bpy.data.objects.remove(o)
    ob["tint"] = tuple(tint)
    ob.rotation_euler = (0, 0, 0)
    ob.scale = (1, 1, 1)
    # materials by slot name
    for i, s in enumerate(ob.material_slots):
        base = s.material.name.split(".")[0] if s.material else None
        nm = base if base and base.startswith("veg_") else None
        if nm:
            s.material = get_material(nm)
    return ob


def instance(ob, loc, rot_z=0.0, scale=1.0, tint=None, parent_coll=None):
    o = bpy.data.objects.new(ob.name + "_i", ob.data)
    o.location = loc
    o.rotation_euler = (0, 0, rot_z)
    if isinstance(scale, (int, float)):
        o.scale = (scale, scale, scale)
    else:
        o.scale = scale
    o["tint"] = tuple(tint) if tint else tuple(ob.get("tint", (1, 1, 1)))
    (parent_coll or bpy.context.scene.collection).objects.link(o)
    return o


def render(path):
    bpy.context.scene.render.filepath = path
    bpy.context.scene.render.image_settings.file_format = 'PNG'
    bpy.ops.render.render(write_still=True)
