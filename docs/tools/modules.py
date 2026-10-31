# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Sunil J and the Oreo contributors
"""To-scale 2D drawings of Oreo's modules for the wiring diagram (top views, the way the boards look).

Every function draws one module with its top-left corner at (x, y) in page pixels and returns (svg, pins), where
pins = {name: (px, py)}. Sizes are real millimetres times S. Three modules use Fritzing's breadboard artwork
(CC-BY-SA 3.0, see NOTICE): the PCA9685 board, the GY-521 MPU6050 board and the SG90 servo.
"""
import json, os, re

S = 5.0  # pixels per millimetre
HERE = os.path.dirname(os.path.abspath(__file__))
FZ = os.path.join(HERE, "fritzing")
GOLD, PAD = "#d8b14a", "#c9a23c"
FONT = "font-family=\"Arial,Helvetica,sans-serif\""


def esc(t):
    return t.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


class G:
    """Draw in millimetres inside a module; collects svg and pins."""
    def __init__(self, x, y):
        self.x, self.y, self.s, self.pins = x, y, [], {}

    def add(self, s):
        self.s.append(s)

    def rect(self, x, y, w, h, fill, stroke="none", sw=0.25, r=0):
        self.add('<rect x="%g" y="%g" width="%g" height="%g" rx="%g" fill="%s" stroke="%s" stroke-width="%g"/>' % (x, y, w, h, r, fill, stroke, sw))

    def circle(self, x, y, r, fill, stroke="none", sw=0.25):
        self.add('<circle cx="%g" cy="%g" r="%g" fill="%s" stroke="%s" stroke-width="%g"/>' % (x, y, r, fill, stroke, sw))

    def line(self, x1, y1, x2, y2, stroke, sw=0.3):
        self.add('<line x1="%g" y1="%g" x2="%g" y2="%g" stroke="%s" stroke-width="%g" stroke-linecap="round"/>' % (x1, y1, x2, y2, stroke, sw))

    def text(self, x, y, t, size=1.3, fill="#ffffff", anchor="middle", bold=True, rot=0):
        tr = ' transform="rotate(%g %g %g)"' % (rot, x, y) if rot else ""
        self.add('<text x="%g" y="%g" font-size="%g" fill="%s" text-anchor="%s" font-weight="%s" %s%s>%s</text>' % (
            x, y, size, fill, anchor, 700 if bold else 400, FONT, tr, esc(t)))

    def hole(self, x, y, r=1.4):
        self.circle(x, y, r + 0.7, "#e8d48a")
        self.circle(x, y, r, "#ffffff")

    def pin(self, name, x, y, square=False, label=None, ldx=0, ldy=0, lanchor="middle", lrot=0, lcol="#ffffff", lsize=1.25):
        """A through-hole header pin (pad + the pin's square post) at (x, y) mm."""
        if square:
            self.rect(x - 0.85, y - 0.85, 1.7, 1.7, GOLD)
        else:
            self.circle(x, y, 0.85, GOLD)
        self.rect(x - 0.32, y - 0.32, 0.64, 0.64, "#3a3a3a")
        if name:
            self.pins[name] = (self.x + x * S, self.y + y * S)
        if label:
            self.text(x + ldx, y + ldy, label, lsize, lcol, lanchor, rot=lrot)

    def smd(self, x, y, w, h, fill="#1c1c1c", leads=0, lead_side="both"):
        """An IC / part with optional gull-wing leads."""
        if leads:
            for i in range(leads):
                lx = x + 0.4 + i * (w - 0.8) / max(1, leads - 1)
                if lead_side in ("both", "top"):
                    self.rect(lx - 0.18, y - 0.5, 0.36, 0.5, "#c0c0c0")
                if lead_side in ("both", "bottom"):
                    self.rect(lx - 0.18, y + h, 0.36, 0.5, "#c0c0c0")
        self.rect(x, y, w, h, fill, r=0.2)

    def chip_rc(self, x, y, horiz=True, col="#7a5a3a"):
        """A tiny 0603 resistor / capacitor."""
        w, h = (1.6, 0.8) if horiz else (0.8, 1.6)
        self.rect(x, y, w, h, col)
        if horiz:
            self.rect(x, y, 0.35, h, "#cfcfcf"); self.rect(x + w - 0.35, y, 0.35, h, "#cfcfcf")
        else:
            self.rect(x, y, w, 0.35, "#cfcfcf"); self.rect(x, y + h - 0.35, w, 0.35, "#cfcfcf")

    def done(self, title=None, sub=None, tdx=0, below=None):
        body = '<g transform="translate(%g %g) scale(%g)">%s</g>' % (self.x, self.y, S, "".join(self.s))
        ty = self.y - (20 if sub else 8) if below is None else self.y + below * S + 18
        sy = self.y - 6 if below is None else ty + 14
        if title:
            body += '<text x="%g" y="%g" font-size="13" font-weight="700" fill="#1e2127" %s>%s</text>' % (self.x + tdx, ty, FONT, esc(title))
        if sub:
            body += '<text x="%g" y="%g" font-size="10.5" fill="#646a78" %s>%s</text>' % (self.x + tdx, sy, FONT, esc(sub))
        return body, self.pins


# ---------------------------------------------------------------- Fritzing artwork
_fz_cache = {}


def _fz(key):
    if key not in _fz_cache:
        meta = json.load(open(os.path.join(FZ, "pins.json"), encoding="utf-8-sig"))
        names = json.load(open(os.path.join(FZ, "names.json"), encoding="utf-8"))
        files = {"mpu": "MPU6050_GY521_9.svg", "pca": "pca9685_adafruit.svg", "servo": "sg90_top.svg"}
        s = open(os.path.join(FZ, files[key]), encoding="utf-8", errors="ignore").read()
        s = s[s.index("<svg"):]
        # keep ids unique when several copies are on one page
        _fz_cache[key] = (s, meta[key], names[key])
    return _fz_cache[key]


_fz_n = [0]


def fritzing(key, x, y, w_mm, h_mm):
    s, meta, names = _fz(key)
    _fz_n[0] += 1
    p = "f%d_" % _fz_n[0]
    s = re.sub(r'id="([^"]+)"', lambda m: 'id="%s%s"' % (p, m.group(1)), s)
    s = re.sub(r'url\(#([^)]+)\)', lambda m: 'url(#%s%s)' % (p, m.group(1)), s)
    s = re.sub(r'href="#([^"]+)"', lambda m: 'href="#%s%s"' % (p, m.group(1)), s)
    W, H = w_mm * S, h_mm * S
    s = re.sub(r"<svg\b([^>]*)>", lambda m: '<svg x="%g" y="%g" width="%g" height="%g" preserveAspectRatio="none"%s>' % (
        x, y, W, H, re.sub(r'\s(width|height|x|y)="[^"]*"', "", m.group(1))), s, count=1)
    pins = {}
    for cid, (fx, fy) in meta["pins"].items():
        n = names.get(cid)
        if n:
            pins.setdefault(n, []).append((x + fx * W, y + fy * H))
    return s, pins


def pca9685(x, y):
    s, pins = fritzing("pca", x, y, 62.2, 25.4)
    one = {k: v[0] for k, v in pins.items()}
    for k, v in pins.items():
        if k.startswith("PWM"):
            one[k] = v[0]
    # left-hand header (the board has the same header on both ends; use the left one)
    for k in ("GND", "OE", "SCL", "SDA", "VCC"):
        one[k] = min(pins[k], key=lambda p: p[0])
    one["V+"] = pins["PWRIN"][0]
    vp = one["V+"]
    one["TERM_GND"] = min(pins["GND"], key=lambda p: (p[0] - vp[0]) ** 2 + (p[1] - vp[1]) ** 2 if p != vp and abs(p[1] - vp[1]) < 4 else 1e9)
    body = s + '<text x="%g" y="%g" font-size="13" font-weight="700" fill="#1e2127" %s>PCA9685 servo board</text>' % (x, y - 20, FONT)
    body += '<text x="%g" y="%g" font-size="10.5" fill="#646a78" %s>16 channels, I2C 0x40</text>' % (x, y - 6, FONT)
    return body, one


def mpu6050(x, y):
    s, pins = fritzing("mpu", x, y, 15.56, 20.26)
    body = s + '<text x="%g" y="%g" font-size="13" font-weight="700" fill="#1e2127" %s>MPU6050</text>' % (x, y - 20, FONT)
    body += '<text x="%g" y="%g" font-size="10.5" fill="#646a78" %s>GY-521, I2C 0x68</text>' % (x, y - 6, FONT)
    return body, {k: v[0] for k, v in pins.items()}


def sg90_up(x, y):
    """Servo stood on end (Fritzing artwork turned 90 deg): 12.7 mm wide, 43.2 mm tall, lead at the top.
    (x, y) is the top-left corner."""
    s, pins = fritzing("servo", 0, 0, 43.2, 12.7)
    w = 12.7 * S
    body = '<g transform="translate(%g %g) rotate(90)">%s</g>' % (x + w, y, s)
    out = {}
    for k, v in pins.items():
        u, vv = v[0]
        out[k] = (x + w - vv, y + u)
    return body, out


def sg90(x, y):
    """Servo, top view, 43.2 x 12.7 mm (Fritzing artwork), lead pins on the left."""
    s, pins = fritzing("servo", x, y, 43.2, 12.7)
    return s, {k: v[0] for k, v in pins.items()}


# ---------------------------------------------------------------- drawn modules (modelled on the real boards)
def esp32s3(x, y):
    """ESP32-S3 WROOM CAM board (N16R8, Freenove / DevKitC-1 layout): 26 x 58 mm, two 22-pin headers in the board's
    real order (they run the full length), camera, two USB-C ports between them at the bottom."""
    g = G(x, y)
    W, H = 26, 58
    g.rect(0, 0, W, H, "#1b1d22", "#0c0d10", 0.3, 1.2)
    # module: antenna + shield
    g.rect(4, 1.2, 18, 25.5, "#20242c", "#000", 0.2, 0.4)
    g.rect(4.4, 1.6, 17.2, 5.4, "#2c4a2e")
    for i in range(6):
        g.add('<path d="M%g 2.3 v3.6 h1.3 v-3.6" fill="none" stroke="#c9a23c" stroke-width="0.35"/>' % (5.3 + i * 2.6))
    g.rect(4.6, 7.6, 16.8, 18.6, "#c9ccd2", "#8e9299", 0.25, 0.4)
    g.text(13, 13.2, "ESP32-S3-WROOM-1", 1.45, "#2b2e35")
    g.text(13, 15.4, "N16R8", 1.45, "#2b2e35")
    g.add('<g opacity="0.6">')
    g.rect(9.5, 18, 7, 5, "none", "#7c8088", 0.25, 0.3)
    g.add('</g>')
    # camera (on its ribbon) and connector
    g.rect(6.5, 28.6, 13, 2.6, "#5a4532")
    g.rect(9, 30.9, 8, 4.2, "#d08a2b")
    g.rect(8.5, 34.6, 9, 9, "#26282e", "#4a4e57", 0.25, 0.6)
    g.circle(13, 39.1, 3.2, "#0d0e12", "#5b606b", 0.35)
    g.circle(13, 39.1, 1.6, "#1d2a44")
    g.circle(12.3, 38.4, 0.45, "#7d8aa8")
    g.text(13, 45.2, "OV2640", 1.15, "#c8cbd2", bold=False)
    # buttons either side of the camera, labels under them
    for bx, nm in ((6.2, "BOOT"), (19.8, "RST")):
        g.rect(bx - 1.8, 44.6, 3.6, 2.8, "#d9d9d9", "#9a9a9a", 0.2, 0.3)
        g.circle(bx, 46.0, 0.8, "#2a2a2a")
        g.text(bx, 49.0, nm, 1.0, "#c8cbd2", bold=False)
    # two USB-C on the bottom edge, labels above them
    for ux, nm in ((4.6, "USB"), (13.4, "COM")):
        g.rect(ux, 53.6, 8, 4.0, "#c3c6cb", "#8b8f96", 0.25, 1.6)
        g.rect(ux + 1.4, 54.9, 5.2, 1.4, "#2e3036", r=0.6)
        g.text(ux + 4, 52.6, nm, 1.0, "#c8cbd2", bold=False)
    # headers as printed on the board (antenna up), labels on the board like its silkscreen; Oreo's pins bold
    left = ("3V3", "3V3", "RST", "4", "5", "6", "7", "15", "16", "17", "18", "8", "3", "46", "9", "10", "11", "12", "13",
            "14", "5V", "GND")
    right = ("GND", "TX", "RX", "1", "2", "42", "41", "40", "39", "38", "37", "36", "35", "0", "45", "48", "47", "21",
             "20", "19", "GND", "GND")
    used = {"3V3", "GND", "5V", "1", "2", "3", "14", "19", "20", "21", "41", "42", "45", "46", "47", "48"}
    for side, names, px, tx, anc in (("L", left, 1.5, 2.75, "start"), ("R", right, 24.5, 23.25, "end")):
        for i, nm in enumerate(names):
            yy = 2.0 + i * 2.54
            key = side + ("GPIO" + nm if nm.isdigit() else nm)
            g.pin(None if key in g.pins else key, px, yy, square=(side == "L" and i == 0))
            on = nm in used and not (nm == "GND" and i > 0 and side == "R") and not (nm == "3V3" and i == 1)
            g.text(tx, yy + 0.4, nm, 1.0, "#ffffff" if on else "#8a8f99", anc, bold=on)  # silkscreen beside the pin
    pins = {}
    for k, v in g.pins.items():
        pins[k] = v
        pins.setdefault(k[1:], v)
    svg, _ = g.done("ESP32-S3 CAM board", "N16R8, the head's brain", below=H)
    return svg, pins


def st7789(x, y):
    """2.0 inch IPS module (ST7789, 240 x 320), seen from the back: 58 x 35 mm, 9-pin header on the right."""
    g = G(x, y)
    g.rect(0, 0, 58, 35, "#15171b", "#050506", 0.3, 0.8)
    for hx, hy in ((2.6, 2.6), (55.4, 2.6), (2.6, 32.4), (55.4, 32.4)):
        g.circle(hx, hy, 1.6, "#b8b8b8"); g.add('<path d="M%g %g h1.6 M%g %g v1.6" stroke="#555" stroke-width="0.35"/>' % (hx - 0.8, hy, hx, hy - 0.8))
    # cream 9-pin JST on the left edge
    g.rect(4.5, 8.2, 4.6, 19.6, "#efe6cf", "#b9ad8c", 0.25, 0.3)
    for i in range(9):
        g.rect(8.6, 9.3 + i * 2.1, 1.6, 0.6, "#c9c9c9")
    # driver, FPC
    g.smd(21, 22, 7, 3, leads=10)
    g.rect(33, 9.5, 2.2, 12, "#efe6cf", "#b9ad8c", 0.2)
    g.add('<path d="M35.2 10 h10 q2 0 2 2 v6 h3 v12 h-5 v-8 h-10 z" fill="#d48a2a" opacity="0.92"/>')
    for i in range(7):
        g.line(36 + i * 1.3, 11, 36 + i * 1.3, 20.5, "#a65f12", 0.18)
    g.text(29, 30.6, "2.0 inch IPS TFT Module", 1.5, "#d6d6d6", bold=False)
    for i, nm in enumerate(("VCC", "GND", "EN", "DC", "SCL", "CS", "TE", "RST", "SDA")):
        yy = 6.8 + i * 2.54
        g.pin(nm, 55.5, yy, square=(i == 0), label=nm, ldx=-1.6, ldy=0.45, lanchor="end")
    return g.done("2.0\" IPS display", "ST7789, SPI (seen from the back)")


def vl53l0x(x, y):
    """GY-530 VL53L0X, purple, 20 x 13.5 mm."""
    g = G(x, y)
    g.rect(0, 0, 20, 13.5, "#6b3fa0", "#4b2a73", 0.3, 0.6)
    g.hole(17, 3.2, 1.3)
    g.smd(7.2, 2.6, 4.4, 2.4, "#121212")
    g.circle(8.3, 3.8, 0.45, "#5a2020"); g.circle(10.4, 3.8, 0.45, "#22305a")
    g.smd(2.2, 2.5, 2.2, 1.6, leads=3, lead_side="bottom")
    g.chip_rc(13, 2.6, False); g.chip_rc(14.4, 2.6, False)
    g.text(9.6, 8.0, "GY-530", 1.3, "#e9ddff", bold=False)
    for i, nm in enumerate(("VIN", "GND", "SCL", "SDA", "GPIO1", "XSHUT")):
        xx = 3.65 + i * 2.54
        g.pin(nm, xx, 11.6, square=(i == 0), label=nm, ldy=-1.5, lsize=0.95, lrot=0)
    return g.done("VL53L0X ToF", "GY-530, I2C 0x29")


def max98357(x, y):
    """MAX98357A I2S amplifier, purple, 17.8 x 19.3 mm, screw terminal for the speaker."""
    g = G(x, y)
    g.rect(0, 0, 17.8, 19.3, "#6b3fa0", "#4b2a73", 0.3, 0.6)
    g.rect(5.1, 0.6, 7.6, 5.8, "#2f9a49", "#1f6b31", 0.25, 0.3)
    for cx in (7.0, 10.8):
        g.circle(cx, 2.4, 1.2, "#cfd3d6", "#888", 0.2); g.line(cx - 0.8, 2.4, cx + 0.8, 2.4, "#666", 0.3)
    g.text(7.0, 5.7, "+", 1.4, "#ffffff"); g.text(10.8, 5.7, "-", 1.4, "#ffffff")
    g.smd(7.4, 8.6, 3, 3, "#151515")
    g.chip_rc(3, 8.8); g.chip_rc(13, 8.8); g.chip_rc(13, 11)
    g.text(8.9, 14.2, "MAX98357", 1.1, "#e9ddff", bold=False)
    for i, nm in enumerate(("LRC", "BCLK", "DIN", "GAIN", "SD", "GND", "VIN")):
        g.pin(nm, 1.28 + i * 2.54, 17.6, square=(i == 0), label=nm, ldy=-1.5, lsize=0.85)
    g.pins["SPK+"] = (g.x + 7.0 * S, g.y + 2.4 * S)
    g.pins["SPK-"] = (g.x + 10.8 * S, g.y + 2.4 * S)
    return g.done("MAX98357A amp", "I2S, 3 W")


def inmp441(x, y):
    """INMP441 I2S MEMS mic, round 14 mm board, six pins."""
    g = G(x, y)
    g.circle(7, 7, 7, "#1d4f96", "#123566", 0.3)
    g.rect(4.8, 2.2, 4.4, 3.4, "#c9ccd2", "#8e9299", 0.2, 0.3)
    g.circle(7, 3.9, 0.5, "#333")
    g.text(7, 8.0, "INMP441", 1.1, "#dbe6ff", bold=False)
    g.rect(0.65, 9.6, 12.7, 3.4, "#1d4f96")
    for i, nm in enumerate(("SCK", "WS", "SD", "L/R", "GND", "VDD")):
        g.pin(nm, 0.65 + 1.27 + i * 2.54, 11.6)
        g.text(0.65 + 1.27 + i * 2.54, 15.0, nm, 0.85, "#1e2127")
    return g.done("INMP441 mic", "I2S MEMS")


def ttp223(x, y):
    """TTP223 touch module, red, 15 x 11 mm."""
    g = G(x, y)
    g.rect(0, 0, 15, 11, "#c92f2f", "#8d1f1f", 0.3, 0.6)
    g.circle(10, 5.5, 4.2, "#e6e6e6", "#bdbdbd", 0.3)
    g.smd(4.0, 7.6, 2, 1.6, leads=3, lead_side="both")
    for i, nm in enumerate(("VCC", "GND", "I/O")):
        g.pin(nm, 1.6, 2.6 + i * 2.54, label=nm, ldx=1.3, ldy=0.4, lanchor="start", lsize=0.9)
    return g.done("TTP223 touch", "")


def oled(x, y):
    """0.96 inch SSD1306 I2C OLED, 27.3 x 27.8 mm, pins at the top."""
    g = G(x, y)
    g.rect(0, 0, 27.3, 27.8, "#1c4f9a", "#123566", 0.3, 0.8)
    for hx, hy in ((2, 2), (25.3, 2), (2, 25.8), (25.3, 25.8)):
        g.hole(hx, hy, 0.9)
    g.rect(0.6, 5.0, 26.1, 18.8, "#2b2f36", "#111", 0.25)
    g.rect(2.4, 7.0, 22.5, 11.6, "#06080c")
    g.text(13.65, 13.6, "128 x 64", 1.6, "#5fb2ff", bold=False)
    g.rect(9.6, 23.8, 8, 2.2, "#c58a2e")
    for i, nm in enumerate(("GND", "VCC", "SCL", "SDA")):
        g.pin(nm, 9.84 + i * 2.54, 2.4, label=nm, ldy=-1.4 + 3.5, lsize=0.95)
    return g.done("0.96\" OLED", "SSD1306, I2C 0x3C")


def xl4015(x, y):
    """XL4015 5 A buck module, blue, 54 x 23 mm: in on the left, out on the right."""
    g = G(x, y)
    g.rect(0, 0, 54, 23, "#2a5db0", "#1b3f7a", 0.3, 0.8)
    # input cap, toroid, heatsink, output cap, trimmer
    g.circle(10.5, 8.5, 4.2, "#262a30", "#111", 0.3); g.circle(10.5, 8.5, 3.4, "#3a3f47")
    g.add('<path d="M7.5 6.0 a4.2 4.2 0 0 1 2.4 -1.6" stroke="#c9ccd2" stroke-width="0.9" fill="none"/>')
    g.text(10.5, 9.0, "100uF", 1.0, "#c9ccd2", bold=False)
    g.circle(26, 11.5, 6.0, "#b87333", "#7a4a1d", 0.3)
    g.circle(26, 11.5, 3.0, "#2a5db0")
    for i in range(24):
        import math
        a = i * math.pi / 12
        g.line(26 + 3.1 * math.cos(a), 11.5 + 3.1 * math.sin(a), 26 + 5.9 * math.cos(a), 11.5 + 5.9 * math.sin(a), "#e2a86a", 0.25)
    g.rect(34.5, 3.5, 7.0, 9.0, "#cfd3d8", "#8e9299", 0.25, 0.3)
    for i in range(5):
        g.line(35.2 + i * 1.4, 3.8, 35.2 + i * 1.4, 12.2, "#9aa0a8", 0.35)
    g.circle(46.5, 14.5, 4.2, "#262a30", "#111", 0.3); g.circle(46.5, 14.5, 3.4, "#3a3f47")
    g.text(46.5, 15.0, "220uF", 1.0, "#c9ccd2", bold=False)
    g.rect(14.5, 16.5, 4, 4, "#2a62d8", "#173a85", 0.25, 0.3); g.circle(16.5, 18.5, 1.0, "#d8b14a")
    g.smd(30, 16.8, 3.2, 2.5, leads=4)
    g.text(27, 21.6, "XL4015", 1.2, "#dbe6ff", bold=False)
    for nm, (px, py) in (("IN+", (2.4, 3.2)), ("IN-", (2.4, 19.8)), ("OUT+", (51.6, 3.2)), ("OUT-", (51.6, 19.8))):
        g.rect(px - 1.6, py - 1.6, 3.2, 3.2, PAD, "#8f7027", 0.2, 0.3)
        g.circle(px, py, 0.7, "#6a5320")
        g.pins[nm] = (g.x + px * S, g.y + py * S)
        g.text(px + (2.4 if px < 20 else -2.4), py + 0.45, nm, 1.15, "#ffffff", "start" if px < 20 else "end")
    return g.done("XL4015 buck", "servo power, set to 5.6 V")


def buck5v(x, y):
    """MP1584 mini buck (3 A), black, 22 x 17 mm, top view, trimmed to 5 V. Its pad names are printed on the back:
    from the top IN- / IN+ are on the left, OUT- / OUT+ on the right (minus pads along the top edge)."""
    g = G(x, y)
    g.rect(0, 0, 22, 17, "#17181b", "#0b0c0e", 0.3, 0.6)
    g.rect(5.6, 1.4, 3.2, 3.2, "#cfcfcf", "#8a8a8a", 0.2, 0.3); g.circle(7.2, 3.0, 1.0, "#e6e0c8"); g.line(6.6, 3.0, 7.8, 3.0, "#777", 0.3)
    g.smd(10.0, 4.2, 4.6, 3.8, "#1c1c1c", leads=4)
    g.text(12.3, 5.6, "MP1584EN", 0.62, "#8c8c8c", bold=False)
    g.rect(4.0, 9.0, 6.0, 6.4, "#3a3d43", "#111", 0.25, 0.4)
    g.text(7.0, 12.8, "4R7", 1.3, "#cfd3d8", bold=False, rot=-90)
    g.rect(12.0, 12.4, 4.6, 2.6, "#1c1c1c", "#444", 0.15, 0.2); g.text(14.3, 14.1, "SS34", 0.8, "#bdbdbd", bold=False)
    for cx, cy in ((3.4, 6.0), (17.6, 6.0)):
        g.rect(cx - 0.6, cy - 1.2, 1.2, 2.4, "#c9a26a", r=0.15)
    g.chip_rc(10.4, 1.8); g.chip_rc(13.0, 1.8); g.chip_rc(15.6, 9.6, False)
    for key, (px, py) in (("IN-", (1.6, 2.2)), ("IN+", (1.6, 14.8)), ("OUT-", (20.4, 2.2)), ("OUT+", (20.4, 14.8))):
        for dy in (-0.0, 0.0):
            pass
        for k in (-1.2, 1.2):  # two castellated holes per pad
            g.circle(px, py + k * 0.9, 0.85, "#d8d8d8"); g.circle(px, py + k * 0.9, 0.45, "#2a2a2a")
        g.pins[key] = (g.x + px * S, g.y + py * S)
        g.text(px + (1.6 if px < 10 else -1.6), py + 0.45, key, 1.0, "#ffffff", "start" if px < 10 else "end")
    return g.done("MP1584 buck", "logic power, 5 V")


def bms(x, y):
    """HW-391 2S 20 A BMS, green, 46 x 20 mm, turned 90 deg (silkscreen as printed): output / charge pads (-) and (+)
    (= P- / P+) and 4.2V (= BM, the cells' middle) at the left end, battery pads 0V (= B-) and 8.4V (= B+) at the
    right end, four TO-252 MOSFETs, 3 mOhm shunts."""
    g = G(x, y)
    g.rect(0, 0, 46, 20, "#14935a", "#0d6a40", 0.3, 0.6)
    g.add('<g transform="rotate(180 23 10)">')  # parts drawn in the board's other orientation, then turned
    for i, j in ((0, 0), (1, 0), (0, 1), (1, 1)):
        mx, my = 8.4 + i * 7.6, 3.2 + j * 7.6
        g.rect(mx, my, 6.6, 6.0, "#1a1a1a", r=0.3)
        g.rect(mx + (6.6 if i == 0 else -1.6), my + 0.6, 1.6, 4.8, "#d8d8d8")
        for k in (1.1, 4.9):
            g.rect(mx + (-1.2 if i == 0 else 6.6), my + k - 0.35, 1.2, 0.7, "#cfcfcf")
    g.rect(4.4, 15.6, 1.8, 3.6, "#ececec", r=0.2); g.rect(4.65, 15.85, 1.3, 3.1, "#1d1d1d")
    g.rect(39.4, 1.0, 1.8, 3.6, "#ececec", r=0.2); g.rect(39.65, 1.25, 1.3, 3.1, "#1d1d1d")
    g.smd(25.2, 15.4, 2.2, 1.4, leads=3)
    g.smd(31.0, 7.6, 2.8, 2.2, leads=3)
    for cx, cy, col in ((28.0, 3.0, "#7a5a3a"), (28.0, 5.2, "#c99a4a"), (30.6, 3.0, "#3a3a3a"), (30.4, 12.6, "#7a5a3a"),
                        (28.0, 15.4, "#c99a4a"), (22.0, 15.8, "#3a3a3a"), (34.6, 12.2, "#7a5a3a")):
        g.chip_rc(cx, cy, False, col)
    g.rect(41.6, 6.0, 3.6, 7.8, "#a8dcef", r=0.2)
    g.add('</g>')
    # texts upright, in the turned positions
    g.text(2.6 + 0.6, 10.0, "20A", 1.7, "#123c58", bold=False, rot=-90)
    g.text(19.0, 18.9, "HW-391", 1.15, "#e8f6ee", bold=False)
    for i, j in ((0, 0), (1, 0), (0, 1), (1, 1)):
        mx, my = 46 - (8.4 + i * 7.6) - 3.3, 20 - (3.2 + j * 7.6) - 3.0
        g.text(mx, my + 0.3, "D472A", 0.9, "#8c8c8c", bold=False)
    # pads (turned): (silkscreen, key, x, y, size)
    for lab, key, px, py, w in (("8.4V", "B+", 43.4, 17.2, 3.6), ("0V", "B-", 43.4, 9.6, 3.6),
                                ("4.2V", "BM", 2.6, 3.0, 3.6), ("+", "P+", 10.4, 16.4, 3.8), ("-", "P-", 10.4, 8.0, 3.8)):
        g.rect(px - w / 2, py - w / 2, w, w, "#d6d6d6", "#9a9a9a", 0.2, 0.3)
        g.circle(px, py, 0.75, "#b0b0b0")
        g.pins[key] = (g.x + px * S, g.y + py * S)
        if lab in "+-":
            g.circle(px + 4.3, py, 1.3, "none", "#ffffff", 0.25)
            g.line(px + 3.6, py, px + 5.0, py, "#ffffff", 0.25)
            if lab == "+":
                g.line(px + 4.3, py - 0.7, px + 4.3, py + 0.7, "#ffffff", 0.25)
        elif key == "BM":
            g.text(px + 2.3, py + 0.4, lab, 1.15, "#ffffff", "start")
        else:
            g.text(px - 2.5, py, lab, 1.15, "#ffffff", rot=-90)
    return g.done()  # title drawn by the caller (wiring.py puts it over the battery cables)


def charger(x, y):
    """USB-C 2S boost charger (8.4 V, 2 A), black, 36 x 18 mm: USB-C and VIN / GND on the left, two SOP-8 chips,
    100 uH inductor, SS34 diode, OK (green, full) and CR (red, charging) LEDs, GND and BAT pads on the right."""
    g = G(x, y)
    g.rect(0, 0, 36, 18, "#141518", "#050506", 0.3, 0.6)
    g.rect(-0.8, 4.6, 7.0, 8.8, "#c3c6cb", "#8b8f96", 0.25, 1.2)
    g.rect(0.6, 7.6, 4.2, 2.8, "#2e3036", r=0.6)
    for lab, py in (("VIN", 1.8), ("GND", 16.2)):
        g.rect(1.1, py - 1.0, 2.0, 2.0, "#d6d6d6"); g.circle(2.1, py, 0.55, "#ffffff")
        g.text(4.3, py + 0.45, lab, 1.05, "#ffffff", "start")
    g.text(9.6, 7.2, "RCS", 1.0, "#ffffff", rot=-90)
    g.rect(11.0, 6.0, 1.6, 3.2, "#cfcfcf", "#8a8a8a", 0.15, 0.2)
    for cx in (9.4, 11.0, 12.6):
        g.chip_rc(cx, 1.0, False, "#c9a26a")
    g.rect(9.2, 10.0, 7.4, 7.0, "#3a3d43", "#111", 0.25, 0.5)
    g.text(12.9, 14.0, "100", 1.5, "#cfd3d8", bold=False)
    g.smd(13.6, 2.6, 4.6, 3.6, "#1c1c1c", leads=4); g.smd(19.6, 2.6, 4.6, 3.6, "#1c1c1c", leads=4)
    g.rect(17.8, 11.4, 5.0, 3.2, "#1c1c1c", "#444", 0.15, 0.2); g.text(20.3, 13.4, "SS34", 0.9, "#bdbdbd", bold=False)
    g.text(20.3, 16.4, "D1", 0.9, "#ffffff", bold=False)
    for cx in (24.4, 26.2, 28.0):
        g.rect(cx - 0.7, 11.2, 1.4, 3.0, "#a8865c", "#7a5a3a", 0.15, 0.2)
    g.text(26.0, 1.8, "OK", 1.05, "#ffffff"); g.text(28.4, 1.8, "CR", 1.05, "#ffffff")
    g.rect(25.4, 2.6, 1.2, 1.4, "#3fbf5a"); g.rect(27.8, 2.6, 1.2, 1.4, "#e04040")  # green = full, red = charging
    g.chip_rc(25.6, 5.4, False); g.chip_rc(28.0, 5.4, False)
    for hy in (2.4, 15.6):
        g.circle(33.6, hy, 1.4, "#ffffff", "#9a9a9a", 0.2)
    g.text(31.2, 3.0, "GND", 1.05, "#ffffff", rot=90); g.text(31.2, 15.0, "BAT", 1.05, "#ffffff", rot=90)
    for key, py in (("BAT-", 7.0), ("BAT+", 11.4)):
        g.rect(31.6, py - 1.7, 4.0, 3.4, "#d6d6d6", "#9a9a9a", 0.2, 0.3)
        g.circle(33.6, py, 0.6, "#b0b0b0")
        g.pins[key] = (g.x + 33.6 * S, g.y + py * S)
    return g.done("USB-C 2S charger", "8.4 V, 2 A: GND pad = battery -, BAT pad = battery +")


def lipo(x, y):
    """2S 7.4 V 1000 mAh 30C pack, 70 x 34 mm, XT30 lead + 3-pin balance plug."""
    g = G(x, y)
    g.rect(0, 0, 70, 34, "#d7dade", "#9aa0a8", 0.35, 2.0)
    g.rect(3, 4, 64, 26, "#e9b93b", "#b98b1d", 0.25, 1.2)
    g.text(35, 13.5, "2S 7.4V", 3.4, "#3a2c06")
    g.text(35, 19.5, "1000mAh 30C", 2.6, "#3a2c06", bold=False)
    g.text(35, 25.0, "LiPo", 2.0, "#3a2c06", bold=False)
    g.add('<path d="M70 8 C 75 8 75 6 79 6" stroke="#d23b3b" stroke-width="1.4" fill="none"/>')
    g.add('<path d="M70 12 C 75 12 75 12 79 12" stroke="#1d1d1d" stroke-width="1.4" fill="none"/>')
    g.rect(79, 3.6, 6.4, 10.8, "#f2c12e", "#b48a10", 0.25, 0.6)
    g.text(82.2, 9.5, "XT30", 1.0, "#3a2c06", rot=90)
    g.pins["BAT+"] = (g.x + 85.4 * S, g.y + 6 * S)
    g.pins["BAT-"] = (g.x + 85.4 * S, g.y + 12 * S)
    for i, c in enumerate(("#1d1d1d", "#2e64c8", "#d23b3b")):
        g.add('<path d="M70 %g C 74 %g 74 %g 79 %g" stroke="%s" stroke-width="0.8" fill="none"/>' % (22 + i * 2, 22 + i * 2, 21 + i * 2.2, 21 + i * 2.2, c))
    g.rect(79, 19.2, 4.6, 8.6, "#f4f1e8", "#b9b19a", 0.25, 0.3)
    for i, nm in enumerate(("BAL-", "BALM", "BAL+")):
        g.pins[nm] = (g.x + 83.6 * S, g.y + (21 + i * 2.2) * S)
    return g.done("2S LiPo battery", "7.4 V 1000 mAh 30C (drone pack)")


def rocker(x, y):
    """Mini rocker switch (KCD11), top view 15 x 10 mm, two tabs below."""
    g = G(x, y)
    g.rect(0, 0, 15, 10, "#1b1d22", "#000", 0.3, 0.8)
    g.rect(1.8, 1.4, 11.4, 7.2, "#c43030", "#7d1c1c", 0.25, 0.6)
    g.rect(1.8, 1.4, 5.7, 7.2, "#a52626", r=0.6)
    g.text(4.6, 5.9, "I", 2.0, "#ffffff"); g.text(10.4, 5.9, "O", 1.8, "#ffffff")
    for nm, px in (("A", 4.5), ("B", 10.5)):
        g.rect(px - 0.7, 10, 1.4, 3, "#c0c0c0")
        g.pins[nm] = (g.x + px * S, g.y + 13 * S)
    return g.done("Rocker switch", "")


def speaker(x, y):
    """40 mm speaker (8 ohm, 0.5 W), two solder tabs."""
    g = G(x, y)
    g.circle(20, 20, 20, "#b8bcc2", "#80858d", 0.35)
    g.circle(20, 20, 16.5, "#2b2e35")
    g.circle(20, 20, 10, "#3c4049")
    g.circle(20, 20, 4.3, "#5b606b")
    for nm, px in (("+", 16.5), ("-", 23.5)):
        g.rect(px - 1.0, 39, 2.0, 3.0, "#d8b14a")
        g.pins[nm] = (g.x + px * S, g.y + 42 * S)
        g.text(px + 2.6, 42.4, nm, 1.8, "#1e2127")
    return g.done("Speaker", "40 mm, 8 ohm, 0.5 W")


def resistor(x, y, value, bands, vertical=True):
    """A through-hole resistor, 6.3 mm body, leads to (x, y) and (x, y + 16 mm)."""
    g = G(x, y)
    g.line(0, 0, 0, 16, "#9a9a9a", 0.5)
    g.rect(-1.25, 4.85, 2.5, 6.3, "#d9c08c", "#a48e58", 0.2, 1.0)
    for i, c in enumerate(bands):
        g.rect(-1.25, 5.8 + i * 1.25, 2.5, 0.6, c)
    g.text(2.2, 8.6, value, 1.9, "#1e2127", "start", bold=False)
    g.pins["a"] = (x, y)
    g.pins["b"] = (x, y + 16 * S)
    return g.done()
