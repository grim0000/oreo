# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Sunil J and the Oreo contributors
"""Export each module drawing (modules.py) as a board-only SVG; render_module_images.ps1 turns them into PNGs that
manual_render.py prints onto the 3D boards.

  python docs/tools/module_images.py      ->  docs/tools/module_png/<name>.svg  (+ sizes.json)
"""
import json, os
import modules as M

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "module_png")
# name: (function, width mm, height mm) - the board outline, without leads or titles
BOARDS = {
    "s3": (M.esp32s3, 26, 58), "display_back": (M.st7789, 58, 35), "tof": (M.vl53l0x, 20, 13.5),
    "amp": (M.max98357, 17.8, 19.3), "touch": (M.ttp223, 15, 11), "oled": (M.oled, 27.3, 27.8),
    "xl4015": (M.xl4015, 54, 23), "buck5v": (M.buck5v, 22, 17), "bms": (M.bms, 46, 20),
    "charger": (M.charger, 36, 18), "battery": (M.lipo, 70, 34), "rocker": (M.rocker, 15, 10),
    "pca9685": (M.pca9685, 62.2, 25.4), "mpu6050": (M.mpu6050, 15.56, 20.26),
}


def board_only(svg):
    """Drop the title/subtitle texts that follow the board drawing."""
    if svg.rstrip().endswith("</text>"):
        cut = max(svg.rfind("</g>"), svg.rfind("</svg>"))
        end = "</g>" if svg.rfind("</g>") > svg.rfind("</svg>") else "</svg>"
        return svg[:cut] + end
    return svg


def main():
    os.makedirs(OUT, exist_ok=True)
    sizes = {}
    for name, (fn, w, h) in BOARDS.items():
        svg, _ = fn(0, 0)
        svg = board_only(svg)
        W, H = w * M.S, h * M.S
        doc = ('<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="%g" height="%g" '
               'viewBox="0 0 %g %g" style="margin:0;display:block">%s</svg>' % (W, H, W, H, svg))
        open(os.path.join(OUT, name + ".svg"), "w", encoding="utf-8").write(doc)
        sizes[name] = [W, H]
    json.dump(sizes, open(os.path.join(OUT, "sizes.json"), "w"), indent=1)
    print("wrote", len(sizes), "svgs")


if __name__ == "__main__":
    main()
