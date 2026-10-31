# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Sunil J and the Oreo contributors
"""Assembly-manual line drawings with Blender (Freestyle): black outlines on white, isometric, exploded steps.

  "C:/Program Files/Blender Foundation/Blender 4.5/blender.exe" -b -P docs/tools/manual_render.py

Parts: hardware/assembly_parts/*.stl (the CAD, in assembled positions, metres) + simple board models for the
electronics (drawn here). Writes docs/img/manual/step_<n>.png and step_<n>.json (where each part landed on the
image, and the explode arrows) for manual_pages.py to add the numbers, arrows and captions.
"""
import bpy, json, math, os, sys
from mathutils import Vector
from bpy_extras.object_utils import world_to_camera_view

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PARTS = os.path.join(ROOT, "hardware", "assembly_parts")
OUT = os.path.join(ROOT, "docs", "img", "manual")
W, H = 1600, 1200
MM = 0.001
GREY = {"dark": 0.80, "pcb": 0.93, "white": 1.0, "battery": 0.88, "screen": 0.55}


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.samples = 6
    sc.cycles.use_denoising = False
    sc.render.resolution_x, sc.render.resolution_y = W, H
    sc.render.film_transparent = False
    sc.render.use_freestyle = True
    sc.render.line_thickness_mode = "ABSOLUTE"
    sc.render.line_thickness = 1.6
    sc.view_settings.view_transform = "Standard"
    world = bpy.data.worlds.new("w")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[0].default_value = (1, 1, 1, 1)
    world.node_tree.nodes["Background"].inputs[1].default_value = 1.0
    sc.world = world
    fs = bpy.context.view_layer.freestyle_settings
    fs.crease_angle = math.radians(135)
    ls = fs.linesets[0] if fs.linesets else fs.linesets.new("lines")
    ls.select_by_visibility = True
    ls.visibility = "VISIBLE"
    ls.select_silhouette = ls.select_border = ls.select_crease = True
    ls.select_external_contour = True
    if ls.linestyle is None:
        ls.linestyle = bpy.data.linestyles.new("ink")
    ls.linestyle.color = (0.08, 0.08, 0.09)
    ls.linestyle.thickness = 1.6
    return sc


def mat(tone):
    name = "tone_%.2f" % tone
    if name in bpy.data.materials:
        return bpy.data.materials[name]
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    em = nt.nodes.new("ShaderNodeEmission")
    em.inputs[0].default_value = (tone, tone, tone, 1)
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    nt.links.new(em.outputs[0], out.inputs[0])
    return m


def load(name, tone=1.0):
    bpy.ops.wm.stl_import(filepath=os.path.join(PARTS, name + ".stl"))
    o = bpy.context.selected_objects[0]
    o.name = name
    o.data.materials.append(mat(tone))
    return o


def cube(name, c, size, tone, parent=None):
    bpy.ops.mesh.primitive_cube_add(size=1, location=(c[0] * MM, c[1] * MM, c[2] * MM))
    o = bpy.context.active_object
    o.scale = (size[0] * MM, size[1] * MM, size[2] * MM)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    o.name = name
    o.data.materials.append(mat(tone))
    if parent:
        bpy.context.view_layer.update()
        o.parent = parent
        o.matrix_parent_inverse = parent.matrix_world.inverted()
    return o


def cyl(name, c, r, h, tone, axis="z", parent=None):
    rot = {"z": (0, 0, 0), "x": (0, math.pi / 2, 0), "y": (math.pi / 2, 0, 0)}[axis]
    bpy.ops.mesh.primitive_cylinder_add(vertices=40, radius=r * MM, depth=h * MM, location=(c[0] * MM, c[1] * MM, c[2] * MM), rotation=rot)
    o = bpy.context.active_object
    o.name = name
    o.data.materials.append(mat(tone))
    if parent:
        bpy.context.view_layer.update()
        o.parent = parent
        o.matrix_parent_inverse = parent.matrix_world.inverted()
    return o


def board(name, c, size, chips=(), header=None, tone=GREY["pcb"], vertical=None):
    """A PCB: the plate, a few chips on top, a row of header pins. vertical='x' / 'y': stood on edge."""
    root = cube(name, c, size, tone)
    sx, sy, sz = size
    for (cx, cy, w, l, h) in chips:  # chip offsets from the board centre (mm), on the top face
        cube(name + "_chip", (c[0] + cx, c[1] + cy, c[2] + sz / 2 + h / 2), (w, l, h), GREY["dark"], root)
    if header:
        n, along, off = header  # n pins along 'x' or 'y', at offset off from the centre line
        for i in range(n):
            t = (i - (n - 1) / 2) * 2.54
            p = (c[0] + (t if along == "x" else off), c[1] + (t if along == "y" else off), c[2] + sz / 2 + 1.2)
            cube(name + "_pin", p, (2.2, 2.2, 2.4), GREY["dark"], root)
    if vertical:
        # stand it up: rotate about its own centre
        bpy.ops.object.select_all(action="DESELECT")
        root.select_set(True)
        bpy.context.view_layer.objects.active = root
        root.rotation_euler = (math.pi / 2, 0, 0) if vertical == "y" else (0, math.pi / 2, 0)
    return root


# ---------------------------------------------------------------- the electronics (board models, robot frame, mm)
# Positions measured from the printed parts (hardware/assembly_parts): the charger's USB-C slot in the back wall,
# the rocker window in the back slope, the OLED window in the front face, the head's USB-C slots, crown slot,
# camera/ToF/mic holes, speaker opening (+y wall) and amp pocket (-y wall).
# Each board carries a light print of the module's real top view (module_png/<name>_g.png, from modules.py).
ART = os.path.join(os.path.dirname(os.path.abspath(__file__)), "module_png")


def art_mat(name):
    key = "art_" + name
    if key in bpy.data.materials:
        return bpy.data.materials[key]
    m = bpy.data.materials.new(key)
    m.use_nodes = True
    nt = m.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = bpy.data.images.load(os.path.join(ART, name + "_g.png"))
    tex.interpolation = "Cubic"
    em = nt.nodes.new("ShaderNodeEmission")
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    nt.links.new(tex.outputs[0], em.inputs[0])
    nt.links.new(em.outputs[0], out.inputs[0])
    return m


def decal(name, art, c, U, V, w, h, parent):
    """A printed face: a w x h mm quad centred at c, image right along U, image up along V (unit vectors)."""
    c, U, V = Vector(c), Vector(U), Vector(V)
    pts = [c - U * w / 2 - V * h / 2, c + U * w / 2 - V * h / 2, c + U * w / 2 + V * h / 2, c - U * w / 2 + V * h / 2]
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(p * MM) for p in pts], [], [(0, 1, 2, 3)])
    uv = me.uv_layers.new()
    for i, (u, v) in enumerate(((0, 0), (1, 0), (1, 1), (0, 1))):
        uv.data[i].uv = (u, v)
    o = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(o)
    o.data.materials.append(art_mat(art))
    bpy.context.view_layer.update()
    o.parent = parent
    o.matrix_parent_inverse = parent.matrix_world.inverted()
    return o


def tilt(o, deg):
    """Rotate a part (and its children) about its own centre, around y."""
    o.rotation_euler = (0, math.radians(deg), 0)
    return o


def flat_board(name, art, c, w, l, t=1.6, face="top"):
    """A board lying flat: w along x, l along y. Its print faces up (top) or down (bottom)."""
    root = cube(name, c, (w, l, t), GREY["pcb"])
    if face == "top":
        decal(name + "_art", art, (c[0], c[1], c[2] + t / 2 + 0.05), (1, 0, 0), (0, 1, 0), w, l, root)
    else:
        decal(name + "_art", art, (c[0], c[1], c[2] - t / 2 - 0.05), (1, 0, 0), (0, -1, 0), w, l, root)
    return root


def electronics_chest():
    e = {}
    # battery: in the bay under the deck (x -42..42, y -22..22), closed by the bottom cover
    bat = cube("battery", (0, 0, 43.5), (70, 34, 14), GREY["battery"])
    decal("battery_art", "battery", (0, 0, 36.45), (1, 0, 0), (0, -1, 0), 70, 34, bat)
    cyl("battery_xt30", (39, 8, 44), 3, 8, GREY["dark"], "x", bat)
    e["battery"] = bat
    # right-hand column (-y): USB-C charger at the back (port in the back-wall slot), the BMS in front of it
    e["charger"] = flat_board("charger", "charger", (-36.0, -10.25, 54.8), 36, 18)
    cube("charger_usbc", (-51.4, -10.25, 57.3), (7.4, 9, 3.2), 0.85, e["charger"])
    e["bms"] = flat_board("bms", "bms", (6, -10, 54.8), 46, 20)
    # left-hand column (+y): PCA9685 at the back, the 5 V buck in front of it
    e["pca9685"] = flat_board("pca9685", "pca9685", (-19, 12.5, 57), 62, 25)
    cube("pca_terminal", (-19 + 4, 4.5, 59.8), (10, 6, 4), GREY["dark"], e["pca9685"])
    for dx in (-28, 28):
        for dy in (-10, 10):
            cyl("pca_standoff", (-19 + dx, 12.5 + dy, 55.1), 1.6, 2.2, GREY["dark"], "z", e["pca9685"])
    e["buck5v"] = flat_board("buck5v", "buck5v", (28, 12.5, 54.8), 22, 17)
    cube("buck5v_coil", (28, 12.5, 57.6), (8, 8, 4), GREY["dark"], e["buck5v"])
    return e


def electronics_roof():
    """Under the roof: XL4015 in the middle, MPU6050 beside it (both hang under the roof, parts facing down);
    the mini rocker in the back-slope window, the OLED behind the front-face window."""
    e = {}
    e["xl4015"] = flat_board("xl4015", "xl4015", (-1.5, -7, 83), 54, 23, face="bottom")
    cyl("xl4015_coil", (-1.5 + 0.5, -7, 78), 6.0, 8.4, 0.9, "z", e["xl4015"])
    cyl("xl4015_cap_in", (-1.5 - 16.5, -7 + 3, 77), 4.2, 10.4, GREY["dark"], "z", e["xl4015"])
    cyl("xl4015_cap_out", (-1.5 + 19.5, -7 - 3, 77), 4.2, 10.4, GREY["dark"], "z", e["xl4015"])
    cube("xl4015_sink", (-1.5 + 11, -7 + 3.5, 79), (7, 9, 6.4), 0.9, e["xl4015"])
    e["imu"] = cube("imu", (2, 16, 83), (20.3, 15.6, 1.6), GREY["pcb"])
    decal("imu_art", "mpu6050", (2, 16, 82.15), (0, 1, 0), (1, 0, 0), 15.6, 20.3, e["imu"])
    # mini rocker, its axis along the 45 deg back-slope normal; the print is the rocker face
    e["switch"] = cube("switch", (-44.5, 0, 77.5), (10, 15, 12), GREY["dark"])
    decal("switch_art", "rocker", (-44.5, 0, 83.55), (0, -1, 0), (1, 0, 0), 15, 10, e["switch"])
    tilt(e["switch"], -45)
    # OLED behind the front window (the face leans back 20 deg)
    e["oled"] = cube("oled", (45.6, 0, 74.4), (27, 27, 1.6), GREY["pcb"])
    decal("oled_art", "oled", (45.6, 0, 75.25), (0, 1, 0), (-1, 0, 0), 27, 27, e["oled"])
    tilt(e["oled"], 69.7)
    return e


def electronics_face():
    """Mounted on the inside of the face: display (landscape), camera above it, ToF (+y) and mic (-y) beside it.
    Seen from behind, so the prints are on the backs."""
    e = {}
    e["display"] = cube("display", (41.4, 2, 159), (1.6, 54, 36), GREY["pcb"])
    decal("display_art", "display_back", (40.55, 2, 159), (0, -1, 0), (0, 0, 1), 54, 36, e["display"])
    cube("display_glass", (42.8, 2, 159), (1.2, 44, 33), GREY["screen"], e["display"])
    e["camera"] = cube("camera", (42, 2.4, 187.2), (2, 9, 9), GREY["pcb"])
    cyl("camera_lens", (44, 2.4, 187.2), 3.6, 3, GREY["dark"], "x", e["camera"])
    e["tof"] = cube("tof", (43.2, 15.6, 190.4), (1.6, 17, 11.5), GREY["pcb"])
    decal("tof_art", "tof", (42.35, 15.6, 190.4), (0, -1, 0), (0, 0, 1), 17, 11.5, e["tof"])
    e["mic"] = cyl("mic", (43.2, -9.5, 190.5), 7, 1.6, GREY["pcb"], "x")
    cube("mic_can", (42.0, -9.5, 192), (0.8, 4.4, 3.4), 0.8, e["mic"])
    return e


def electronics_back():
    """From the back: the ESP32-S3 board stands upright behind the display (USB-C ports down through the floor
    slots, top edge in the crown slot), speaker in the +y wall opening, amp in the -y wall pocket, touch pad."""
    e = {}
    s3 = cube("s3", (39.4, -0.1, 169.6), (1.6, 24, 55.5), GREY["pcb"])
    decal("s3_art", "s3", (38.55, -0.1, 169.6), (0, -1, 0), (0, 0, 1), 24, 55.5, s3)
    for y in (-6.1, 5.4):
        cube("s3_usbc", (37, y, 145.4), (3.2, 9, 7.4), 0.85, s3)
    e["s3"] = s3
    e["speaker"] = cyl("speaker", (22.5, 31.5, 159.5), 20, 5, GREY["dark"], "y")   # 40 mm speaker
    cyl("speaker_cone", (22.5, 34.2, 159.5), 13.5, 0.6, 0.9, "y", e["speaker"])
    e["amp"] = cube("amp", (17.2, -34, 156.5), (16.6, 1.6, 18), GREY["pcb"])
    decal("amp_art", "amp", (17.2, -33.15, 156.5), (-1, 0, 0), (0, 0, 1), 16.6, 18, e["amp"])
    e["touch"] = cube("touch", (15, 0, 192), (11, 15, 1.6), GREY["pcb"])
    decal("touch_art", "touch", (15, 0, 191.15), (0, 1, 0), (1, 0, 0), 15, 11, e["touch"])
    return e


# ---------------------------------------------------------------- camera / render
def look(objs, view=(1, -1, 0.8), margin=1.18):
    cam_d = bpy.data.cameras.new("cam")
    cam_d.type = "ORTHO"
    cam = bpy.data.objects.new("cam", cam_d)
    bpy.context.scene.collection.objects.link(cam)
    bpy.context.scene.camera = cam
    pts = []
    for o in objs:
        for m in [o] + list(o.children):
            if m.hide_render:
                continue
            vs = m.data.vertices
            step = max(1, len(vs) // 3000)
            pts += [m.matrix_world @ vs[i].co for i in range(0, len(vs), step)]
    lo = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
    hi = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
    centre = (lo + hi) / 2
    d = Vector(view).normalized()
    cam.location = centre + d * 2.0
    cam.rotation_euler = (centre - cam.location).to_track_quat("-Z", "Y").to_euler()
    bpy.context.view_layer.update()
    xs, ys = [], []
    sc = bpy.context.scene
    for p in pts:
        v = world_to_camera_view(sc, cam, p)
        xs.append(v.x)
        ys.append(v.y)
    cam_d.ortho_scale = 1.0
    bpy.context.view_layer.update()
    xs = [world_to_camera_view(sc, cam, p).x for p in pts]
    ys = [world_to_camera_view(sc, cam, p).y for p in pts]
    span = max((max(xs) - min(xs)) * 1.0, (max(ys) - min(ys)) * W / H)
    cam_d.ortho_scale = span * margin
    # re-centre on the projected box
    bpy.context.view_layer.update()
    xs = [world_to_camera_view(sc, cam, p).x for p in pts]
    ys = [world_to_camera_view(sc, cam, p).y for p in pts]
    cam_d.shift_x = ((max(xs) + min(xs)) / 2 - 0.5)
    cam_d.shift_y = ((max(ys) + min(ys)) / 2 - 0.5) * H / W
    return cam


def where(cam, o):
    sc = bpy.context.scene
    c = sum((o.matrix_world @ Vector(b) for b in o.bound_box), Vector()) / 8
    v = world_to_camera_view(sc, cam, c)
    return [round(v.x * W, 1), round((1 - v.y) * H, 1)]


def px(cam, p):
    v = world_to_camera_view(bpy.context.scene, cam, Vector(p))
    return [round(v.x * W, 1), round((1 - v.y) * H, 1)]


def render(step, objs, labels, moves, view=(1, -1, 0.8)):
    """labels: {object: text}; moves: [(object, (dx, dy, dz) mm)] - the explode offsets (arrows drawn back)."""
    starts = {}
    for o, d in moves:
        starts[o.name] = [(o.matrix_world @ Vector(b)) for b in o.bound_box]
        o.location = o.location + Vector(d) * MM
    bpy.context.view_layer.update()
    cam = look(objs, view)
    os.makedirs(OUT, exist_ok=True)
    bpy.context.scene.render.filepath = os.path.join(OUT, "step_%s.png" % step)
    bpy.ops.render.render(write_still=True)
    meta = {"labels": [{"text": t, "at": where(cam, o)} for o, t in labels.items()], "arrows": []}
    for o, d in moves:
        c0 = sum(starts[o.name], Vector()) / 8
        c1 = c0 + Vector(d) * MM
        meta["arrows"].append({"from": px(cam, c1), "to": px(cam, c0 + Vector(d) * MM * 0.25)})
    json.dump(meta, open(os.path.join(OUT, "step_%s.json" % step), "w"), indent=1)


def objs_named(names, tone=1.0):
    return [load(n, tone) for n in names]


def visible_only(keep):
    for o in bpy.data.objects:
        if o.type == "MESH":
            root = o
            while root.parent:
                root = root.parent
            o.hide_render = root not in keep


STEPS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []


def want(s):
    return not STEPS or s in STEPS


def main():
    # 1 - one leg: knee servo into the femur, the foot on its horn
    if want("1"):
        reset()
        fe, ks, ft = objs_named(["FL_femur", "FL_knee_servo", "FL_foot"])
        ks.data.materials[0] = mat(GREY["dark"])
        render("1", [fe, ks, ft], {fe: "femur (printed)", ks: "MG90S knee servo", ft: "foot (printed)"},
               [(ks, (0, 0, 22)), (ft, (0, 0, -8))], view=(1, -1, 0.7))
    # 2 - hip servos into the middle block
    if want("2"):
        reset()
        mb = load("middle_block", GREY["dark"])
        hs = objs_named(["hip_servo_1", "hip_servo_2", "hip_servo_3", "hip_servo_4"], GREY["dark"])
        render("2", [mb] + hs, {mb: "middle block (printed)", hs[0]: "MG90S hip servo x4"}, [(h, (0, 0, 40)) for h in hs])
    # 3 - battery in the centre bay, bottom cover underneath
    if want("3"):
        reset()
        mb = load("middle_block", GREY["dark"])
        hs = objs_named(["hip_servo_1", "hip_servo_2", "hip_servo_3", "hip_servo_4"], GREY["dark"])
        bc = load("bottom_cover")
        e = electronics_chest()
        keep = [mb, bc, e["battery"]] + hs
        visible_only(keep)
        render("3", keep, {e["battery"]: "2S LiPo 1000 mAh", bc: "bottom cover (printed)"},
               [(e["battery"], (0, 0, -32)), (bc, (0, 0, -62))])
    # 4 - the deck: charger (back) + BMS on the right, PCA9685 (back) + 5 V buck on the left
    if want("4"):
        reset()
        mb = load("middle_block", GREY["dark"])
        hs = objs_named(["hip_servo_1", "hip_servo_2", "hip_servo_3", "hip_servo_4"], GREY["dark"])
        bc = load("bottom_cover")
        e = electronics_chest()
        keep = [mb, bc] + hs + list(e.values())
        render("4", keep, {e["bms"]: "2S BMS 20 A (HW-391)", e["charger"]: "USB-C 2S charger", e["buck5v"]: "MP1584 buck (5 V logic)",
                           e["pca9685"]: "PCA9685 servo board"},
               [(e["bms"], (0, 0, 28)), (e["charger"], (0, 0, 28)), (e["buck5v"], (0, 0, 28)), (e["pca9685"], (0, 0, 34))])
    # 5 - under the roof: the top shell upside down, the roof parts go in
    if want("5"):
        reset()
        ts = load("top_shell")
        er = electronics_roof()
        keep = [ts] + list(er.values())
        render("5", keep, {er["xl4015"]: "XL4015 buck (servo power)", er["imu"]: "MPU6050", er["switch"]: "mini rocker switch",
                           er["oled"]: "0.96\" OLED"},
               [(er["xl4015"], (0, 0, -60)), (er["imu"], (0, 0, -60)), (er["switch"], (24, 0, -24)), (er["oled"], (-34, 0, -13))],
               view=(0.9, -1, -0.9))
    # 6 - the top shell onto the body
    if want("6"):
        reset()
        mb = load("middle_block", GREY["dark"])
        hs = objs_named(["hip_servo_1", "hip_servo_2", "hip_servo_3", "hip_servo_4"], GREY["dark"])
        bc = load("bottom_cover")
        ts = load("top_shell")
        ec = electronics_chest()
        er = electronics_roof()
        roof = list(er.values())
        keep = [mb, bc, ts] + hs + list(ec.values()) + roof
        render("6", keep, {ts: "top shell with the roof parts (step 5)", ec["charger"]: "charger port to the back slot"},
               [(ts, (0, 0, 70))] + [(o, (0, 0, 70)) for o in roof])
    # 7 - the legs onto the hip servos
    if want("7"):
        reset()
        body = [load("middle_block", GREY["dark"]), load("bottom_cover"), load("top_shell")] + \
            objs_named(["hip_servo_1", "hip_servo_2", "hip_servo_3", "hip_servo_4"], GREY["dark"])
        legs = []
        for L, sy in (("FL", 1), ("FR", -1), ("RL", 1), ("RR", -1)):
            g = objs_named([L + "_femur", L + "_knee_servo", L + "_foot"])
            g[1].data.materials[0] = mat(GREY["dark"])
            legs.append((g, sy))
        moves = [(o, (0, 30 * sy, 0)) for g, sy in legs for o in g]
        render("7", body + [o for g, _ in legs for o in g], {legs[0][0][0]: "leg (step 1) onto the hip servo horn"}, moves)
    # 8 - the neck: base, pan servo, pan bracket, tilt servo, tilt plate
    if want("8"):
        reset()
        nb, ps, pb, tsv, tp = objs_named(["neck_base", "pan_servo", "neck_pan_bracket", "tilt_servo", "neck_tilt_plate"])
        ps.data.materials[0] = mat(GREY["dark"])
        tsv.data.materials[0] = mat(GREY["dark"])
        render("8", [nb, ps, pb, tsv, tp], {nb: "neck base (on the top shell)", ps: "MG90S pan servo", pb: "pan bracket",
                                            tsv: "MG90S tilt servo", tp: "tilt plate (head mount)"},
               [(ps, (0, 0, 20)), (pb, (0, 0, 40)), (tsv, (0, 0, 60)), (tp, (0, 0, 80))], view=(1, -1, 0.6))
    # 9 - the face, from behind: display, camera, ToF, mic
    if want("9"):
        reset()
        body = load("head_front_plate")
        inlay = load("head_bezel", GREY["dark"])
        e = electronics_face()
        keep = [body, inlay] + list(e.values())
        render("9", keep, {e["display"]: "2.0\" IPS display (landscape)", e["camera"]: "camera (on its ribbon)",
                           e["tof"]: "VL53L0X ToF", e["mic"]: "INMP441 mic"},
               [(e["display"], (-45, 0, 0)), (e["camera"], (-55, 0, 0)), (e["tof"], (-55, 0, 0)), (e["mic"], (-55, 0, 0))],
               view=(-1, -0.8, 0.55))
    # 10 - from behind: the S3 board, speaker, amp, touch, then the back plate
    if want("10"):
        reset()
        body = load("head_front_plate")
        inlay = load("head_bezel", GREY["dark"])
        cover = load("head_shell")
        grille = load("head_side_plate", GREY["dark"])
        f = electronics_face()
        e = electronics_back()
        keep = [body, inlay, cover, grille] + list(f.values()) + list(e.values())
        render("10", keep, {e["s3"]: "ESP32-S3 CAM board", e["speaker"]: "speaker", e["amp"]: "MAX98357A amp",
                            e["touch"]: "TTP223 touch pad", cover: "back cover (back + roof)"},
               [(e["s3"], (-55, 0, 0)), (e["speaker"], (0, 30, 0)), (e["amp"], (0, 22, 0)), (e["touch"], (-40, 0, 25)),
                (cover, (-110, 0, 30)), (grille, (-110, 0, 30))],
               view=(-1, -0.8, 0.55))
    # 11 - complete
    if want("11"):
        reset()
        names = [f[:-4] for f in os.listdir(PARTS) if f.endswith(".stl")]
        allp = []
        for n in names:
            dark = any(k in n for k in ("servo", "middle_block", "neck", "bezel", "side_plate", "trim"))
            allp.append(load(n, GREY["dark"] if dark else 1.0))
        render("11", allp, {}, [], view=(1, -1, 0.65))


main()
