# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Sunil J and the Oreo contributors
"""Oreo's wiring diagram: the real-looking modules (modules.py, to scale) with every wire drawn.

  python docs/tools/wiring.py   ->  docs/img/wiring.svg

Two areas, the way the robot is built: the HEAD (ESP32-S3 board, display, sensors, audio) and the CHEST (power,
servo board, IMU, OLED, servos), joined by the cable that runs up through the neck.
"""
import os
import modules as M

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "img", "wiring.svg")
W, H = 2240, 1720
C = {"5V": "#d7262b", "3V3": "#ef8a17", "GND": "#1d1e22", "VSW": "#9c3c19", "VSERVO": "#8a1f73",
     "SDA": "#2f6fdb", "SCL": "#e1b51b", "SPI": "#159a8c", "I2S": "#7a4cc9", "IO": "#2f9a45", "SPK": "#7a7f88"}
svg = []


def add(s):
    svg.append(s)


def wire(pts, color, w=3.0, dots=()):
    d = "M%g %g" % pts[0]
    r = 6
    for i in range(1, len(pts) - 1):
        (x0, y0), (x1, y1), (x2, y2) = pts[i - 1], pts[i], pts[i + 1]

        def towards(ax, ay, bx, by):
            L = max(1e-6, ((bx - ax) ** 2 + (by - ay) ** 2) ** 0.5)
            k = min(r, L / 2)
            return ax + (bx - ax) * k / L, ay + (by - ay) * k / L
        a, b = towards(x1, y1, x0, y0), towards(x1, y1, x2, y2)
        d += " L%g %g Q%g %g %g %g" % (a[0], a[1], x1, y1, b[0], b[1])
    d += " L%g %g" % pts[-1]
    add('<path d="%s" fill="none" stroke="#ffffff" stroke-width="%g" stroke-linecap="round" stroke-linejoin="round"/>' % (d, w + 2.6))
    add('<path d="%s" fill="none" stroke="%s" stroke-width="%g" stroke-linecap="round" stroke-linejoin="round"/>' % (d, color, w))
    for p in dots:
        dot(p, color)


def cable(a, b, color, c1, c2, w=4.0):
    add('<path d="M%g %g C%g %g %g %g %g %g" fill="none" stroke="#ffffff" stroke-width="%g" stroke-linecap="round"/>' % (
        a[0], a[1], c1[0], c1[1], c2[0], c2[1], b[0], b[1], w + 2.6))
    add('<path d="M%g %g C%g %g %g %g %g %g" fill="none" stroke="%s" stroke-width="%g" stroke-linecap="round"/>' % (
        a[0], a[1], c1[0], c1[1], c2[0], c2[1], b[0], b[1], color, w))


def dot(p, color):
    add('<circle cx="%g" cy="%g" r="4.6" fill="%s" stroke="#fff" stroke-width="1.2"/>' % (p[0], p[1], color))


def text(x, y, t, size=12, color="#5b6070", anchor="start", bold=False):
    add('<text x="%g" y="%g" font-size="%g" fill="%s" text-anchor="%s" font-weight="%s" %s>%s</text>' % (
        x, y, size, color, anchor, 700 if bold else 400, M.FONT, M.esc(t)))


def rail(y, x0, x1, net, label=True):
    wire([(x0, y), (x1, y)], C[net], 4.0)
    if label:
        tw = 8 * len(net) + 12
        add('<rect x="%g" y="%g" width="%g" height="16" rx="8" fill="%s"/>' % (x0 - tw - 6, y - 8, tw, C[net]))
        text(x0 - 6 - tw / 2, y + 4.5, net, 10.5, "#fff", "middle", True)


def panel(x, y, w, h, title, sub):
    add('<rect x="%g" y="%g" width="%g" height="%g" rx="14" fill="#f6f7f9" stroke="#d5d8de" stroke-width="1.5"/>' % (x, y, w, h))
    text(x + 18, y + 30, title, 20, "#1e2127", bold=True)
    text(x + 18 + 14 * len(title) + 14, y + 30, sub, 12.5)


def place(fn, *a):
    s, p = fn(*a)
    add(s)
    return p


def main():
    add('<rect width="%d" height="%d" fill="#ffffff"/>' % (W, H))
    text(30, 36, "Oreo - wiring", 26, "#1e2127", "start", True)
    text(240, 36, "Wire colours: red 5 V, orange 3.3 V, black GND, brown battery after the switch, magenta servo power, "
         "blue SDA, yellow SCL, teal display (SPI), violet audio (I2S), green other GPIO.", 12.5)
    text(30, 58, "The ESP32-S3 headers are drawn in the board's real order (Freenove / DevKitC layout, antenna up); Oreo's pins are bold. "
         "A dot joins wires; wires that cross without a dot are not connected.", 12.5)
    text(30, 80, "Left unconnected: display TE, amp GAIN and SD, PCA9685 OE, MPU6050 XDA/XCL/AD0/INT, ToF GPIO1/XSHUT. "
         "Mic L/R goes to GND (left channel).", 12.5)

    # ================================================================ HEAD
    panel(30, 96, 1340, 704, "HEAD", "everything that lives in the head")
    disp = place(M.st7789, 80, 270)
    s3 = place(M.esp32s3, 640, 210)
    touch = place(M.ttp223, 930, 240)
    mic = place(M.inmp441, 1212, 240)
    amp = place(M.max98357, 900, 540)
    spk = place(M.speaker, 1004, 455)
    tof = place(M.vl53l0x, 660, 610)

    top3, topg = 146, 164
    rail(top3, 120, 1342, "3V3", True)
    rail(topg, 120, 1330, "GND", True)
    bot3, botg = 768, 750
    wire([(1342, top3), (1342, bot3), (620, bot3)], C["3V3"], 4.0)
    wire([(1330, topg), (1330, botg), (620, botg)], C["GND"], 4.0)
    wire([s3["L3V3"], (620, s3["L3V3"][1]), (620, top3)], C["3V3"], dots=[(620, top3)])
    wire([s3["RGND"], (768, s3["RGND"][1]), (768, topg)], C["GND"], dots=[(768, topg)])

    # display
    wire([disp["VCC"], (372, disp["VCC"][1]), (372, top3)], C["3V3"], dots=[(372, top3)])
    wire([disp["GND"], (384, disp["GND"][1]), (384, topg)], C["GND"], dots=[(384, topg)])
    a, b = disp["EN"], s3["LGPIO14"]
    wire([a, (400, a[1]), (400, b[1]), b], C["IO"])
    # the SPI pins are on the right-hand header: over the top of the board
    for pin, g, lx, xr, yt in (("SCL", "GPIO42", 430, 776, 178), ("SDA", "GPIO41", 470, 784, 186),
                               ("CS", "GPIO21", 445, 792, 194), ("DC", "GPIO20", 415, 800, 202)):
        a, b = disp[pin], s3["R" + g]
        wire([a, (lx, a[1]), (lx, yt), (xr, yt), (xr, b[1]), b], C["SPI"])
    wire([disp["RST"], (400, disp["RST"][1]), (400, disp["EN"][1])], C["IO"], dots=[(400, disp["EN"][1])])

    # touch
    wire([touch["VCC"], (915, touch["VCC"][1]), (915, top3)], C["3V3"], dots=[(915, top3)])
    wire([touch["GND"], (905, touch["GND"][1]), (905, topg)], C["GND"], dots=[(905, topg)])
    wire([s3["RGPIO19"], (820, s3["RGPIO19"][1]), (820, touch["I/O"][1]), touch["I/O"]], C["IO"])

    # mic: power to the right-hand rails, signals down to the I2S lanes
    wire([mic["GND"], (mic["GND"][0], 330), (1330, 330)], C["GND"], dots=[(1330, 330)])
    wire([mic["L/R"], (mic["L/R"][0], 330)], C["GND"], dots=[(mic["L/R"][0], 330)])
    wire([mic["VDD"], (mic["VDD"][0], 318), (1342, 318)], C["3V3"], dots=[(1342, 318)])
    # I2S
    yl = {"GPIO45": 680, "GPIO46": 692, "GPIO47": 704, "GPIO48": 716}
    xl = {"GPIO45": 840, "GPIO46": 852, "GPIO47": 864, "GPIO48": 876}
    for g in yl:
        a = s3["R" + g] if g != "GPIO46" else s3["LGPIO46"]
        if g == "GPIO46":  # on the left-hand header: round the board's left side and under it
            wire([a, (630, a[1]), (630, yl[g]), (xl[g], yl[g])], C["I2S"])
        else:
            wire([a, (xl[g], a[1]), (xl[g], yl[g])], C["I2S"])
    wire([(xl["GPIO45"], yl["GPIO45"]), (mic["SCK"][0], yl["GPIO45"]), mic["SCK"]], C["I2S"])
    wire([(amp["BCLK"][0], yl["GPIO45"]), amp["BCLK"]], C["I2S"], dots=[(amp["BCLK"][0], yl["GPIO45"])])
    wire([(xl["GPIO46"], yl["GPIO46"]), (mic["WS"][0], yl["GPIO46"]), mic["WS"]], C["I2S"])
    wire([(amp["LRC"][0], yl["GPIO46"]), amp["LRC"]], C["I2S"], dots=[(amp["LRC"][0], yl["GPIO46"])])
    wire([(xl["GPIO47"], yl["GPIO47"]), (mic["SD"][0], yl["GPIO47"]), mic["SD"]], C["I2S"])
    wire([(xl["GPIO48"], yl["GPIO48"]), (amp["DIN"][0], yl["GPIO48"]), amp["DIN"]], C["I2S"])
    wire([amp["GND"], (amp["GND"][0], botg)], C["GND"], dots=[(amp["GND"][0], botg)])
    # speaker on the amp's screw terminal
    # (round the speaker's left side, in under its tabs - the two never cross)
    wire([amp["SPK+"], (amp["SPK+"][0], 530), (999, 530), (999, spk["+"][1] + 3), (spk["+"][0], spk["+"][1] + 3), spk["+"]], C["SPK"], 2.6)
    wire([amp["SPK-"], (amp["SPK-"][0], 540), (993, 540), (993, spk["-"][1] + 11), (spk["-"][0], spk["-"][1] + 11), spk["-"]], "#2b2e35", 2.6)
    # ToF
    wire([tof["VIN"], (tof["VIN"][0], bot3)], C["3V3"], dots=[(tof["VIN"][0], bot3)])
    wire([tof["GND"], (tof["GND"][0], botg)], C["GND"], dots=[(tof["GND"][0], botg)])

    # ---------------------------------------------------------------- the neck cable
    nx = {"5V": 548, "GPIO3": 560, "GND": 572, "GPIO1": 808, "GPIO2": 816}  # I2C is on the right-hand header
    netc = {"GPIO1": C["SDA"], "GPIO2": C["SCL"], "GPIO3": C["IO"], "GND": C["GND"], "5V": C["5V"]}
    for g, x in nx.items():
        a = s3[("R" if g in ("GPIO1", "GPIO2") else "L") + g]
        wire([a, (x, a[1]), (x, 870)], netc[g])
    wire([(620, bot3), (608, bot3), (608, 870)], C["3V3"], dots=[(620, bot3)])
    wire([tof["SDA"], (tof["SDA"][0], 678), (nx["GPIO1"], 678)], C["SDA"], dots=[(nx["GPIO1"], 678)])
    wire([tof["SCL"], (tof["SCL"][0], 688), (nx["GPIO2"], 688)], C["SCL"], dots=[(nx["GPIO2"], 688)])
    wire([(nx["5V"], 780), (amp["VIN"][0], 780), amp["VIN"]], C["5V"], dots=[(nx["5V"], 780)])
    text(830, 826, "neck cable, 6 wires:", 12, "#1e2127", "start", True)
    text(830, 842, "5V, battery sense, GND, SDA, SCL, 3V3", 11)

    # ================================================================ CHEST
    panel(30, 868, 2180, 832, "CHEST", "power, servo board, IMU, OLED, servos")
    bat = place(M.lipo, 60, 940)
    chg = place(M.charger, 60, 1200)
    bms = place(M.bms, 290, 1200)
    sw = place(M.rocker, 150, 1345)
    b5 = place(M.buck5v, 410, 1345)
    xl4 = place(M.xl4015, 52, 1500)
    r1 = place(M.resistor, 340, 1455, "100k", ["#8b4513", "#111", "#e6c619"])
    r2 = place(M.resistor, 340, 1545, "10k", ["#8b4513", "#111", "#ef7d1a"])
    pca = place(M.pca9685, 860, 1040)
    mpu = place(M.mpu6050, 1620, 1000)
    oled = place(M.oled, 1820, 1000)
    gnd_y = 1660
    rail(gnd_y, 120, 2150, "GND")

    # I2C bus + 3V3 along the top of the chest
    c3_y, scl_y, sda_y = 910, 922, 934
    wire([(608, 870), (608, c3_y), (2100, c3_y)], C["3V3"])
    wire([(nx["GPIO2"], 870), (nx["GPIO2"], scl_y), (2100, scl_y)], C["SCL"])
    wire([(nx["GPIO1"], 870), (nx["GPIO1"], sda_y), (2100, sda_y)], C["SDA"])
    wire([(nx["GND"], 870), (nx["GND"], gnd_y)], C["GND"], dots=[(nx["GND"], gnd_y)])

    # battery -> BMS, charger -> BMS
    cable(bat["BAT+"], bms["B+"], "#d7262b", (bat["BAT+"][0] + 80, bat["BAT+"][1] + 120), (bms["B+"][0] + 90, bms["B+"][1]))
    cable(bat["BAT-"], bms["B-"], "#1d1e22", (bat["BAT-"][0] + 30, bat["BAT-"][1] + 150), (bms["B-"][0] + 70, bms["B-"][1]))
    cable(bat["BALM"], bms["BM"], "#2e64c8", (bat["BALM"][0] + 20, bat["BALM"][1] + 110), (bms["BM"][0], bms["BM"][1] - 90), 2.4)
    # the BMS title again on a plate, over the battery cables
    add('<rect x="308" y="1160" width="232" height="36" rx="5" fill="#f6f7f9" opacity="0.95"/>')
    text(314, 1178, "2S BMS 20 A (HW-391)", 13, "#1e2127", "start", True)
    text(314, 1192, "8.4V = B+, 4.2V = balance middle, 0V = B-", 10.5, "#646a78")
    text(70, 1128, "balance plug: only its", 10.5)
    text(70, 1142, "middle wire goes to BM", 10.5)
    # charger BAT+ / BAT- -> BMS (+) / (-): it charges through the BMS
    cable(chg["BAT+"], bms["P+"], "#d7262b", (chg["BAT+"][0] + 50, chg["BAT+"][1] + 20), (bms["P+"][0] - 50, bms["P+"][1]), 3.2)
    cable(chg["BAT-"], bms["P-"], "#1d1e22", (chg["BAT-"][0] + 40, chg["BAT-"][1] - 30), (bms["P-"][0] - 40, bms["P-"][1]), 3.2)
    # (-) is the robot's ground: taken from the charger's BAT- pad, the same net
    wire([chg["BAT-"], (278, chg["BAT-"][1]), (278, 1318), (44, 1318), (44, gnd_y)], C["GND"], dots=[(44, gnd_y)])
    # P+ (the BMS's (+) pad) -> switch -> VSW
    wire([bms["P+"], (bms["P+"][0], 1305), (130, 1305), (130, 1425), (sw["A"][0], 1425), sw["A"]], C["VSW"])
    vy = 1445
    wire([sw["B"], (sw["B"][0], vy), (26, vy), (26, xl4["IN+"][1]), xl4["IN+"]], C["VSW"])
    wire([(sw["B"][0], vy), (r1["a"][0], vy), r1["a"]], C["VSW"], dots=[(sw["B"][0], vy)])
    wire([(r1["a"][0], vy), (390, vy), (390, b5["IN+"][1]), b5["IN+"]], C["VSW"], dots=[(r1["a"][0], vy)])
    text(214, vy - 6, "battery after the switch", 11, C["VSW"])
    # divider -> GPIO3
    wire([r1["b"], r2["a"]], C["IO"], 2.4)
    mid = (r1["a"][0], (r1["b"][1] + r2["a"][1]) / 2)
    wire([(nx["GPIO3"], 870), (nx["GPIO3"], mid[1]), mid], C["IO"], dots=[mid])
    wire([r2["b"], (r2["b"][0], gnd_y)], C["GND"], dots=[(r2["b"][0], gnd_y)])
    # 5 V buck -> the S3 5V pin
    wire([(nx["5V"], 870), (nx["5V"], b5["OUT+"][1]), b5["OUT+"]], C["5V"])
    text(nx["5V"] - 8, 1140, "5 V up the neck to the S3's 5V pin", 11, C["5V"], "end")
    for p in (xl4["IN-"], xl4["OUT-"]):
        wire([p, (p[0], gnd_y)], C["GND"], dots=[(p[0], gnd_y)])
    # the buck's minus pads are on its top edge: over the board and down its right side
    gx = b5["OUT-"][0] + 22
    wire([b5["IN-"], (b5["IN-"][0], b5["IN-"][1] - 7), (gx, b5["IN-"][1] - 7), (gx, gnd_y)], C["GND"], dots=[(gx, gnd_y)])
    wire([b5["OUT-"], (gx, b5["OUT-"][1])], C["GND"], dots=[(gx, b5["OUT-"][1])])
    # XL4015 -> servo power terminal
    vs = (xl4["OUT+"][0] + 40, xl4["OUT+"][1])
    wire([xl4["OUT+"], vs, (vs[0], 1480), (640, 1480), (640, 996), (pca["V+"][0], 996), pca["V+"]], C["VSERVO"])
    wire([pca["TERM_GND"], (pca["TERM_GND"][0], 1027), (1240, 1027), (1240, gnd_y)], C["GND"], dots=[(1240, gnd_y)])

    # I2C devices
    x0 = 850
    wire([(x0, sda_y), (x0, pca["SDA"][1]), pca["SDA"]], C["SDA"], dots=[(x0, sda_y)])
    wire([(x0 - 12, scl_y), (x0 - 12, pca["SCL"][1]), pca["SCL"]], C["SCL"], dots=[(x0 - 12, scl_y)])
    wire([(x0 - 24, c3_y), (x0 - 24, pca["VCC"][1]), pca["VCC"]], C["3V3"], dots=[(x0 - 24, c3_y)])
    wire([pca["GND"], (x0 + 12, pca["GND"][1]), (x0 + 12, 1180), (790, 1180), (790, gnd_y)], C["GND"], dots=[(790, gnd_y)])
    for nm, xx, y0, col in (("VCC", 1580, c3_y, "3V3"), ("SCL", 1592, scl_y, "SCL"), ("SDA", 1604, sda_y, "SDA")):
        wire([(xx, y0), (xx, mpu[nm][1]), mpu[nm]], C[col], dots=[(xx, y0)])
    wire([mpu["GND"], (1568, mpu["GND"][1]), (1568, gnd_y)], C["GND"], dots=[(1568, gnd_y)])
    for nm, y0, col in (("VCC", c3_y, "3V3"), ("SCL", scl_y, "SCL"), ("SDA", sda_y, "SDA")):
        wire([(oled[nm][0], y0), oled[nm]], C[col], dots=[(oled[nm][0], y0)])
    wire([oled["GND"], (oled["GND"][0], 1000 - 4), (1800, 1000 - 4), (1800, gnd_y)], C["GND"], dots=[(1800, gnd_y)])
    text(1260, 1160, "I2C: servo board 0x40, OLED 0x3C, MPU6050 0x68 (ToF 0x29 is in the head)", 11.5)

    # servos, upright, in channel order under the servo board
    roles = {14: ("R1", "front-right hip"), 4: ("R2", "back-right hip"), 12: ("L1", "front-left hip"), 13: ("L2", "back-left hip"),
             8: ("R4", "back-right knee"), 9: ("R3", "front-right knee"), 11: ("L3", "front-left knee"), 10: ("L4", "back-left knee"),
             7: ("PAN", "head pan"), 5: ("TILT", "head tilt")}
    pitch = 0.1 * 25.4 * M.S
    for k, ch in enumerate(sorted(roles)):
        role, name = roles[ch]
        sx, sy = 700 + k * 120, 1330
        sv = place(M.sg90_up, sx, sy)
        text(sx + 32, sy + 240, "CH%d  %s" % (ch, role), 12, "#1e2127", "middle", True)
        text(sx + 32, sy + 256, name, 10.5, "#5b6070", "middle")
        p = pca["PWM%d" % ch]
        for j, (nm, colr) in enumerate((("pulse", "#f0a030"), ("vcc", "#d7262b"), ("gnd", "#6b3b1e"))):
            a = (p[0], p[1] + j * pitch)
            b = sv[nm]
            cable(a, b, colr, (a[0], a[1] + 70), (b[0], b[1] - 80), 2.0)
    text(700, 1626, "Default channels - change them on the robot's Servo mapping page (/servomap) if yours are wired differently.", 12, "#1e2127")

    open(OUT, "w", encoding="utf-8").write(
        '<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" viewBox="0 0 %d %d" width="%d" height="%d">%s</svg>'
        % (W, H, W, H, "".join(svg)))
    print("wrote", os.path.normpath(OUT))


if __name__ == "__main__":
    main()
