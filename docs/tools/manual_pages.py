# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Sunil J and the Oreo contributors
"""Turn the Blender line renders (docs/img/manual/step_<n>.png + .json) into assembly-manual pages:
big step number, title, dashed explode arrows, callouts with leader lines, and a short note.

  python docs/tools/manual_pages.py   ->  docs/img/manual/page_<n>.png
"""
import json, math, os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
IMG = os.path.join(HERE, "..", "img", "manual")
INK = (24, 24, 28)
SOFT = (120, 120, 128)
FONT = "C:/Windows/Fonts/arial.ttf"
BOLD = "C:/Windows/Fonts/arialbd.ttf"

STEPS = {
    "1": ("Build a leg (x4)", "Push the knee servo into the femur, cable out the back. Fit the foot on the servo horn; screw the horn on only after calibration."),
    "2": ("Hip servos into the body", "Four MG90S servos drop into the corner pockets of the middle block, shafts up. Route the cables to the centre."),
    "3": ("Battery and bottom cover", "The 2S 1000 mAh LiPo goes into the bay under the deck, leads up through the deck. Clip the bottom cover over it."),
    "4": ("Power and servo boards on the deck", "Right side: the USB-C charger at the back (its port in the slot of the back wall), the HW-391 BMS in front of it. Left side: the PCA9685 at the back, the MP1584 5 V buck in front of it."),
    "5": ("Parts under the roof", "Shell upside down: XL4015 flat under the middle of the roof, MPU6050 beside it (flat and square), mini rocker into the back-slope window, OLED behind the front window."),
    "6": ("Close the chest", "Lower the top shell (with the step 5 parts) onto the body. The charger's USB-C must line up with the slot in the back wall."),
    "7": ("Attach the legs", "Press each leg onto its hip-servo horn. Wait for calibration before tightening the horn screws."),
    "8": ("Build the neck", "Pan servo into the neck base, then the pan bracket, the tilt servo and the tilt plate the head mounts on."),
    "9": ("The face, from behind", "Display in the face (landscape, pins to the right), camera at the forehead hole, ToF to its left, mic to its right."),
    "10": ("Board, sound, back cover", "ESP32-S3 board upright behind the display, USB-C ports down through the floor slots. Speaker in the left-wall opening, amp in the right-wall pocket, touch pad under the crown. Then the back cover."),
    "11": ("Done", "Head onto the tilt plate, cables through the hole behind the neck. Switch on, join the Oreo WiFi, open /servomap then /calibrate."),
}


def font(size, bold=False):
    return ImageFont.truetype(BOLD if bold else FONT, size)


def dashed(d, a, b, dash=14, gap=9, width=3):
    (x0, y0), (x1, y1) = a, b
    L = math.hypot(x1 - x0, y1 - y0)
    if L < 1:
        return
    ux, uy = (x1 - x0) / L, (y1 - y0) / L
    t = 0
    while t < L:
        e = min(t + dash, L)
        d.line([(x0 + ux * t, y0 + uy * t), (x0 + ux * e, y0 + uy * e)], fill=INK, width=width)
        t += dash + gap


def arrowhead(d, a, b, size=18):
    ang = math.atan2(b[1] - a[1], b[0] - a[0])
    p = [b, (b[0] - size * math.cos(ang - 0.4), b[1] - size * math.sin(ang - 0.4)),
         (b[0] - size * math.cos(ang + 0.4), b[1] - size * math.sin(ang + 0.4))]
    d.polygon(p, fill=INK)


def wrap(text, f, width, d):
    words, lines, cur = text.split(), [], ""
    for w in words:
        t = (cur + " " + w).strip()
        if d.textlength(t, font=f) > width and cur:
            lines.append(cur)
            cur = w
        else:
            cur = t
    return lines + [cur]


def page(step):
    src = Image.open(os.path.join(IMG, "step_%s.png" % step)).convert("RGB")
    meta = json.load(open(os.path.join(IMG, "step_%s.json" % step)))
    W, H = src.size
    M = 380  # side margins for the callouts
    P = Image.new("RGB", (W + 2 * M, H + 260), "white")
    P.paste(src, (M, 150))
    d = ImageDraw.Draw(P)
    title, note = STEPS[step]
    d.text((60, 40), step, font=font(110, True), fill=INK)
    d.text((60 + d.textlength(step, font=font(110, True)) + 30, 78), title, font=font(46, True), fill=INK)
    d.line([(60, 172), (P.width - 60, 172)], fill=INK, width=2)
    # explode arrows: from the moved part toward its seat, skipping the part itself
    for a in meta["arrows"]:
        f = (a["from"][0] + M, a["from"][1] + 150)
        t = (a["to"][0] + M, a["to"][1] + 150)
        L = math.hypot(t[0] - f[0], t[1] - f[1])
        if L < 40:
            continue
        s = (f[0] + (t[0] - f[0]) * 0.35, f[1] + (t[1] - f[1]) * 0.35)
        dashed(d, s, t)
        arrowhead(d, s, t)
    # callouts: left or right margin by side, stacked so they never overlap
    f = font(28)
    sides = {"L": [], "R": []}
    for lab in meta["labels"]:
        x, y = lab["at"][0] + M, lab["at"][1] + 150
        sides["L" if x < P.width / 2 else "R"].append((y, x, lab["text"]))
    for side, items in sides.items():
        items.sort()
        last = 150
        for y, x, text in items:
            ty = max(y, last + 60)
            last = ty
            tw = d.textlength(text, font=f)
            if side == "L":
                tx, ex = 40, 40 + tw + 14
            else:
                tx, ex = P.width - 40 - tw, P.width - 40 - tw - 14
            d.text((tx, ty - 16), text, font=f, fill=INK)
            d.line([(ex, ty), (x, y)], fill=SOFT, width=2)
            d.ellipse([x - 6, y - 6, x + 6, y + 6], fill=INK)
    # the note
    nf = font(30)
    y = P.height - 100
    d.line([(60, y - 20), (P.width - 60, y - 20)], fill=(210, 210, 214), width=2)
    for i, line in enumerate(wrap(note, nf, P.width - 120, d)[:2]):
        d.text((60, y + i * 38), line, font=nf, fill=INK)
    P.save(os.path.join(IMG, "page_%s.png" % step), optimize=True)


if __name__ == "__main__":
    for s in STEPS:
        if os.path.exists(os.path.join(IMG, "step_%s.json" % s)):
            page(s)
            print("page", s)
