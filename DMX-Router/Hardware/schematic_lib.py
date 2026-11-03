#!/usr/bin/env python3
# ------------------------------------------------------------------
#  DMX-Router 4 -> 16  --  shared drawing library
#
#  Small SVG helper, frame/title block and the schematic symbols used
#  by the four sheet modules.  Everything is drawn on a 10 px grid.
# ------------------------------------------------------------------

from __future__ import annotations

import math

CSS = """
.w   { stroke:#1565c0; stroke-width:2.0; fill:none; stroke-linecap:round; stroke-linejoin:round; }
.wt  { stroke:#1565c0; stroke-width:1.4; fill:none; stroke-linecap:round; stroke-linejoin:round; }
.c   { stroke:#a61b1b; stroke-width:2.0; fill:#fffdfd; }
.p   { stroke:#a61b1b; stroke-width:1.5; fill:none; stroke-linecap:round; }
.j   { fill:#1565c0; }
.t   { font-family:'Segoe UI', Arial, sans-serif; fill:#1c1c1c; }
.m   { font-family:Consolas, 'Courier New', monospace; fill:#1c1c1c; }
.net { fill:#00695c; font-weight:600; }
.note{ fill:#5f6368; }
.pwr { fill:#c62828; font-weight:600; }
.gnd { fill:#37474f; font-weight:600; }
.ref { fill:#a61b1b; font-weight:700; }
.val { fill:#7a2e2e; }
.frm { stroke:#263238; stroke-width:1.4; fill:none; }
.tb  { stroke:#263238; stroke-width:1.2; fill:#ffffff; }
"""

NET = "#00695c"


def esc(s: str) -> str:
    return (s.replace("&", "&amp;").replace("<", "&lt;")
            .replace(">", "&gt;").replace('"', "&quot;"))


class D:
    """Tiny SVG drawing helper."""

    def __init__(self):
        self.parts: list[str] = []

    def add(self, s: str) -> None:
        self.parts.append(s)

    # --- primitives -------------------------------------------------
    def line(self, x1, y1, x2, y2, cls="w"):
        self.add(f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" '
                 f'y2="{y2:.1f}" class="{cls}"/>')

    def poly(self, pts, cls="w"):
        p = " ".join(f"{x:.1f},{y:.1f}" for x, y in pts)
        self.add(f'<polyline points="{p}" class="{cls}"/>')

    def rect(self, x, y, w, h, cls="c", rx=0):
        r = f' rx="{rx}"' if rx else ""
        self.add(f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" '
                 f'height="{h:.1f}" class="{cls}"{r}/>')

    def circle(self, x, y, r, cls="j"):
        self.add(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{r:.1f}" '
                 f'class="{cls}"/>')

    def text(self, x, y, s, size=11, cls="t", anchor="start", weight=None,
             rotate=None):
        extra = ""
        if weight:
            extra += f' font-weight="{weight}"'
        if rotate is not None:
            extra += f' transform="rotate({rotate} {x:.1f} {y:.1f})"'
        self.add(f'<text x="{x:.1f}" y="{y:.1f}" font-size="{size}" '
                 f'class="{cls}" text-anchor="{anchor}"{extra}>{esc(s)}</text>')

    def dot(self, x, y):
        self.circle(x, y, 3.2, "j")

    # --- wires ------------------------------------------------------
    def wire(self, *pts):
        """Draw a wire.  Accepts (x, y) tuples and/or flat coordinates."""
        flat = []
        for p in pts:
            if isinstance(p, (tuple, list)):
                flat.append((float(p[0]), float(p[1])))
            else:
                flat.append(float(p))
        if flat and not isinstance(flat[0], tuple):
            pts2 = [(flat[i], flat[i + 1]) for i in range(0, len(flat) - 1, 2)]
        else:
            pts2 = flat
        self.poly(pts2, "w")

    def wire_t(self, *pts):
        flat = []
        for p in pts:
            if isinstance(p, (tuple, list)):
                flat.append((float(p[0]), float(p[1])))
            else:
                flat.append(float(p))
        if flat and not isinstance(flat[0], tuple):
            pts2 = [(flat[i], flat[i + 1]) for i in range(0, len(flat) - 1, 2)]
        else:
            pts2 = flat
        self.poly(pts2, "wt")

    def arrow(self, x1, y1, x2, y2, cls="w"):
        self.line(x1, y1, x2, y2, cls)
        ang = math.atan2(y2 - y1, x2 - x1)
        for a in (ang + 2.55, ang - 2.55):
            self.line(x2, y2, x2 + 13 * math.cos(a), y2 + 13 * math.sin(a), cls)

    def nc(self, x, y, s=6):
        self.line(x - s, y - s, x + s, y + s, "p")
        self.line(x - s, y + s, x + s, y - s, "p")

    def net(self, x, y, s, anchor="start", size=9.5, color=NET):
        """Net label with a thin underline along the wire."""
        w = len(s) * size * 0.58
        if anchor == "start":
            ux1, ux2 = x, x + w
        elif anchor == "end":
            ux1, ux2 = x - w, x
        else:
            ux1, ux2 = x - w / 2, x + w / 2
        self.text(x, y - 4, s, size=size, cls="net", anchor=anchor,
                  weight=600)
        self.add(f'<line x1="{ux1:.1f}" y1="{y:.1f}" x2="{ux2:.1f}" '
                 f'y2="{y:.1f}" stroke="{color}" stroke-width="1.1"/>')

    def pwr(self, x, y, s="+3V3"):
        self.line(x, y, x, y - 12, "wt")
        self.line(x - 7, y - 12, x + 7, y - 12, "w")
        self.text(x, y - 17, s, size=9, cls="pwr", anchor="middle",
                  weight=700)

    def gnd(self, x, y, s=None, iso=False):
        self.line(x, y, x, y + 9, "wt")
        if iso:
            self.poly([(x - 9, y + 9), (x + 9, y + 9), (x, y + 21),
                       (x - 9, y + 9)], "wt")
        else:
            self.line(x - 10, y + 9, x + 10, y + 9, "w")
            self.line(x - 6, y + 14, x + 6, y + 14, "w")
            self.line(x - 2, y + 19, x + 2, y + 19, "w")
        if s:
            self.text(x, y + 33, s, size=8, cls="gnd", anchor="middle",
                      weight=600)

    def gnd_label(self, x, y):
        self.text(x, y + 3, "GND", size=9, cls="gnd", weight=700)

    # --- passives ---------------------------------------------------
    def res_h(self, x, y, ref, val, length=46, label_side=1):
        body = length - 12
        self.line(x, y, x + 6, y, "p")
        self.rect(x + 6, y - 7, body, 14, "c")
        self.line(x + 6 + body, y, x + length, y, "p")
        dy = -11 if label_side > 0 else 22
        self.text(x + length / 2, y + dy, ref, size=7.5, cls="ref",
                  anchor="middle")
        self.text(x + length / 2, y + dy + 10, val, size=7.5, cls="val",
                  anchor="middle")
        return x, x + length

    def res_v(self, x, y, ref, val, length=46):
        body = length - 12
        self.line(x, y, x, y + 6, "p")
        self.rect(x - 7, y + 6, 14, body, "c")
        self.line(x, y + 6 + body, x, y + length, "p")
        self.text(x + 12, y + length / 2 - 3, ref, size=7.5, cls="ref")
        self.text(x + 12, y + length / 2 + 7, val, size=7.5, cls="val")
        return y, y + length

    def cap_v(self, x, y, ref, val, length=30):
        mid = y + length / 2
        self.line(x, y, x, mid - 3, "p")
        self.line(x - 9, mid - 3, x + 9, mid - 3, "w")
        self.line(x - 9, mid + 3, x + 9, mid + 3, "w")
        self.line(x, mid + 3, x, y + length, "p")
        self.text(x + 13, mid - 2, ref, size=7.5, cls="ref")
        self.text(x + 13, mid + 8, val, size=7.5, cls="val")
        return y, y + length

    def jumper(self, x, y1, y2, ref, closed=True):
        """Two solder pads between y1 and y2."""
        self.circle(x, y1 + 4, 4.5, "c")
        self.circle(x, y2 - 4, 4.5, "c")
        if closed:
            self.line(x, y1 + 8, x, y2 - 8, "w")
        else:
            self.line(x, y1 + 8, x, y1 + 14, "wt")
            self.line(x, y2 - 14, x, y2 - 8, "wt")
        self.text(x + 10, (y1 + y2) / 2 + 3, ref, size=7.5, cls="ref")

    # --- output -----------------------------------------------------
    def save(self, path, w, h):
        body = "\n".join(self.parts)
        svg = (f'<?xml version="1.0" encoding="UTF-8"?>\n'
               f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" '
               f'height="{h}" viewBox="0 0 {w} {h}">\n'
               f'<style>{CSS}</style>\n'
               f'<rect x="0" y="0" width="{w}" height="{h}" '
               f'fill="#ffffff"/>\n{body}\n</svg>\n')
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(svg)


# ------------------------------------------------------------------
#  Frame / title block
# ------------------------------------------------------------------

def frame(d: D, w, h, title, subtitle, sheet_no, sheet_total, board_note=""):
    d.rect(18, 18, w - 36, h - 36, "frm")
    d.rect(26, 26, w - 52, h - 52, "frm")
    d.text(46, 58, title, size=25, cls="t", weight=700)
    d.text(46, 82, subtitle, size=12.5, cls="note")
    d.line(40, 96, w - 40, 96, "wt")

    x0, y0, tw, th = w - 540, h - 112, 500, 92
    d.rect(x0, y0, tw, th, "tb")
    d.line(x0, y0 + 30, x0 + tw, y0 + 30, "frm")
    d.line(x0, y0 + 58, x0 + tw, y0 + 58, "frm")
    d.line(x0 + 300, y0 + 30, x0 + 300, y0 + th, "frm")
    d.text(x0 + 12, y0 + 21, "DMX-Router 4 → 16", size=15, cls="t",
           weight=700)
    d.text(x0 + 12, y0 + 49, f"Blatt {sheet_no} / {sheet_total}", size=12,
           cls="t", weight=700)
    d.text(x0 + 12, y0 + 78, "Rev. A  ·  2026-11-03", size=10, cls="note")
    d.text(x0 + 312, y0 + 49, "Schaltbild", size=12, cls="t", weight=700)
    d.text(x0 + 312, y0 + 78, "nicht maßstäblich", size=9.5, cls="note")
    if board_note:
        d.text(46, h - 34, board_note, size=9, cls="note")


def note_box(d: D, x, y, w, h, title, lines, size=9.0, line_gap=None):
    d.rect(x, y, w, h, "tb", rx=4)
    d.text(x + 12, y + 20, title, size=10.5, cls="t", weight=700)
    d.line(x + 10, y + 27, x + w - 10, y + 27, "wt")
    gap = line_gap if line_gap is not None else size + 4.5
    yy = y + 43
    for ln in lines:
        d.text(x + 12, yy, ln, size=size, cls="note")
        yy += gap


# ------------------------------------------------------------------
#  Symbols
# ------------------------------------------------------------------

def xlr(d: D, x, y, ref, kind="female"):
    """5-pin XLR.  Pins leave to the right; 3 (D+) is above 2 (D-)."""
    h = 132
    d.rect(x, y, 62, h, "c", rx=4)
    d.text(x + 31, y - 7, ref, size=8.5, cls="ref", anchor="middle")
    d.text(x + 31, y + h + 13, f"XLR5 {kind}", size=8, cls="val",
           anchor="middle")
    rows = [("1", "SCHIRM"), ("3", "D+"), ("2", "D-"),
            ("4", "NC"), ("5", "NC")]
    pins = {}
    for i, (num, name) in enumerate(rows):
        py = y + 20 + i * 22
        pins[num] = (x + 90, py)
        d.line(x + 62, py, x + 90, py, "p")
        d.text(x + 56, py - 4, num, size=7, cls="ref", anchor="end")
        d.text(x + 6, py + 3, name, size=7.5, cls="m")
    return pins


def sm712_v(d: D, x, y1, y2, ref):
    """SM712 across A/B; pin 1 top, pin 2 bottom, pin 3 to the right."""
    mid = (y1 + y2) / 2
    bt, bb = mid - 30, mid + 30
    d.rect(x - 32, bt, 64, 60, "c", rx=3)
    d.text(x, mid - 5, "SM712", size=8, cls="ref", anchor="middle")
    d.text(x, mid + 8, "TVS", size=7.5, cls="val", anchor="middle")
    d.text(x - 36, bt - 4, ref, size=7.5, cls="ref", anchor="end")
    d.line(x, y1, x, bt, "p")
    d.line(x, bb, x, y2, "p")
    d.line(x + 32, mid, x + 46, mid, "p")
    d.text(x - 4, bt + 12, "1", size=6.5, cls="ref", anchor="end")
    d.text(x - 4, bb - 6, "2", size=6.5, cls="ref", anchor="end")
    d.text(x + 34, mid - 3, "3", size=6.5, cls="ref")
    return (x, y1), (x, y2), (x + 46, mid)


def adm2483(d: D, x, y, ref):
    """ADM2483.

    Left:  A (12), B (13)
    Right: VDD1 (1), GND1 (2), GND1 (8), RXD (3), /RE (4), DE (5),
           TXD (6), PV (7)
    Bottom: VDD2 (16), GND2 (15), GND2 (9)
    """
    bw, bh = 150, 200
    d.rect(x, y, bw, bh, "c", rx=3)
    d.text(x + bw / 2, y - 7, ref, size=9, cls="ref", anchor="middle")
    d.text(x + bw / 2, y + 17, "ADM2483", size=8, cls="val",
           anchor="middle")

    pins = {}
    left = [(12, "A", 40), (13, "B", 140)]
    for num, name, dy in left:
        py = y + dy
        d.line(x - 30, py, x, py, "p")
        d.text(x - 15, py - 4, str(num), size=7, cls="ref",
               anchor="middle")
        d.text(x + 6, py + 3, name, size=8, cls="m")
        pins[num] = (x - 30, py)

    right = [(1, "VDD1", 22), (2, "GND1", 44), (8, "GND1", 66),
             (3, "RXD", 88), (4, "/RE", 110), (5, "DE", 132),
             (6, "TXD", 154), (7, "PV", 176)]
    for num, name, dy in right:
        py = y + dy
        d.line(x + bw, py, x + bw + 30, py, "p")
        d.text(x + bw + 15, py - 4, str(num), size=7, cls="ref",
               anchor="middle")
        d.text(x + bw - 6, py + 3, name, size=8, cls="m", anchor="end")
        pins[num] = (x + bw + 30, py)

    bottom = [(16, "VDD2", 40), (15, "GND2", 75), (9, "GND2", 110)]
    for num, name, dx in bottom:
        px = x + dx
        d.line(px, y + bh, px, y + bh + 24, "p")
        d.text(px - 3, y + bh + 18, str(num), size=7, cls="ref",
               anchor="end")
        d.text(px, y + bh - 6, name, size=7, cls="m", anchor="middle")
        pins[num] = (px, y + bh + 24)
    return pins


def b0505s(d: D, x, y, ref):
    bw, bh = 104, 76
    d.rect(x, y, bw, bh, "c", rx=3)
    d.text(x + bw / 2, y - 7, ref, size=8.5, cls="ref", anchor="middle")
    d.text(x + bw / 2, y + 26, "B0505S-1W", size=8.5, cls="ref",
           anchor="middle")
    d.text(x + bw / 2, y + 42, "5 V → 5 V · 1 W", size=7, cls="val",
           anchor="middle")
    d.text(x + bw / 2, y + 66, "1 kV isoliert", size=6.8, cls="val",
           anchor="middle")
    pins = {}
    for num, name, dy in [(1, "+VIN", 24), (2, "-VIN", 54)]:
        d.line(x - 24, y + dy, x, y + dy, "p")
        d.text(x - 12, y + dy - 4, str(num), size=6.5, cls="ref",
               anchor="middle")
        d.text(x + 5, y + dy + 3, name, size=7, cls="m")
        pins[num] = (x - 24, y + dy)
    for num, name, dy in [(3, "+VOUT", 24), (4, "-VOUT", 54)]:
        d.line(x + bw, y + dy, x + bw + 24, y + dy, "p")
        d.text(x + bw + 12, y + dy - 4, str(num), size=6.5, cls="ref",
               anchor="middle")
        d.text(x + bw - 5, y + dy + 3, name, size=7, cls="m", anchor="end")
        pins[num] = (x + bw + 24, y + dy)
    return pins


def mux151(d: D, x, y, ref):
    """74HC151, D0..D7 and A/B/C on the left, Y//Y//E on the right."""
    bw, bh = 170, 290
    d.rect(x, y, bw, bh, "c", rx=3)
    d.text(x + bw / 2, y - 7, ref, size=9, cls="ref", anchor="middle")
    d.text(x + bw / 2, y + bh + 14, "74HC151", size=8.5, cls="val",
           anchor="middle")
    d.text(x + bw / 2, y + 20, "8:1 MUX", size=7.5, cls="val",
           anchor="middle")

    pins = {}
    left = [(14, "D0", 40), (13, "D1", 62), (12, "D2", 84),
            (11, "D3", 106), (1, "D4", 128), (2, "D5", 150),
            (3, "D6", 172), (4, "D7", 194),
            (10, "A", 226), (9, "B", 250), (8, "C", 274)]
    for num, name, dy in left:
        py = y + dy
        d.line(x - 30, py, x, py, "p")
        d.text(x - 15, py - 4, str(num), size=6.5, cls="ref",
               anchor="middle")
        d.text(x + 6, py + 3, name, size=7.5, cls="m")
        pins[num] = (x - 30, py)
        pins[name] = (x - 30, py)

    right = [(5, "Y", 60), (15, "/Y", 100), (6, "/E", 140)]
    for num, name, dy in right:
        py = y + dy
        d.line(x + bw, py, x + bw + 30, py, "p")
        d.text(x + bw + 15, py - 4, str(num), size=6.5, cls="ref",
               anchor="middle")
        d.text(x + bw - 6, py + 3, name, size=7.5, cls="m", anchor="end")
        pins[num] = (x + bw + 30, py)
        pins[name] = (x + bw + 30, py)

    # VCC top, GND bottom
    vx = x + 60
    d.line(vx, y - 24, vx, y, "p")
    d.text(vx, y - 28, "16", size=6.5, cls="ref", anchor="middle")
    d.text(vx + 22, y + 12, "VCC", size=7, cls="m")
    pins[16] = (vx, y - 24)
    gx = x + 60
    d.line(gx, y + bh, gx, y + bh + 24, "p")
    d.text(gx, y + bh + 18, "7", size=6.5, cls="ref", anchor="middle")
    d.text(gx + 22, y + bh - 6, "GND", size=7, cls="m")
    pins[7] = (gx, y + bh + 24)
    return pins


def pico(d: D, x, y):
    """Raspberry Pi Pico module, 40 pins, DIP style."""
    bw, bh = 280, 540
    d.rect(x, y, bw, bh, "c", rx=6)
    d.text(x + bw / 2, y - 8, "U1", size=10, cls="ref", anchor="middle")
    d.text(x + bw / 2, y + bh + 16, "Raspberry Pi Pico (RP2040)",
           size=8.5, cls="val", anchor="middle")

    left = [
        (1, "GP0"), (2, "GP1"), (3, "GND"), (4, "GP2"), (5, "GP3"),
        (6, "GP4"), (7, "GP5"), (8, "GND"), (9, "GP6"), (10, "GP7"),
        (11, "GP8"), (12, "GP9"), (13, "GND"), (14, "GP10"),
        (15, "GP11"), (16, "GP12"), (17, "GP13"), (18, "GND"),
        (19, "GP14"), (20, "GP15"),
    ]
    right = [
        (40, "VBUS"), (39, "VSYS"), (38, "GND"), (37, "3V3_EN"),
        (36, "3V3"), (35, "ADC_VREF"), (34, "GP28"), (33, "GND"),
        (32, "GP27"), (31, "GP26"), (30, "RUN"), (29, "GP22"),
        (28, "GND"), (27, "GP21"), (26, "GP20"), (25, "GP19"),
        (24, "GP18"), (23, "GND"), (22, "GP17"), (21, "GP16"),
    ]
    pins = {}
    for i, (num, name) in enumerate(left):
        py = y + 24 + i * 24.5
        d.line(x - 28, py, x, py, "p")
        d.text(x - 14, py - 4, str(num), size=6.5, cls="ref",
               anchor="middle")
        d.text(x + 6, py + 3, name, size=7.5, cls="m")
        pins[num] = (x - 28, py)
        pins[name] = (x - 28, py)
    for i, (num, name) in enumerate(right):
        py = y + 24 + i * 24.5
        d.line(x + bw, py, x + bw + 28, py, "p")
        d.text(x + bw + 14, py - 4, str(num), size=6.5, cls="ref",
               anchor="middle")
        d.text(x + bw - 6, py + 3, name, size=7.5, cls="m", anchor="end")
        pins[num] = (x + bw + 28, py)
        pins[name] = (x + bw + 28, py)
    return pins


def ic(d: D, x, y, w, h, ref, name, left, right, top=(), bottom=(),
       sub=""):
    """Generic IC box. left/right/top/bottom are (pin, label) lists."""
    d.rect(x, y, w, h, "c", rx=3)
    d.text(x + w / 2, y - 7, ref, size=9, cls="ref", anchor="middle")
    d.text(x + w / 2, y + h + 14, name, size=8.5, cls="val",
           anchor="middle")
    if sub:
        d.text(x + w / 2, y + 34, sub, size=7.5, cls="val",
               anchor="middle")
    pins = {}
    for i, (num, nm) in enumerate(left):
        py = y + (i + 1) * h / (len(left) + 1)
        d.line(x - 28, py, x, py, "p")
        d.text(x - 14, py - 4, str(num), size=6.5, cls="ref",
               anchor="middle")
        d.text(x + 6, py + 3, nm, size=7.5, cls="m")
        pins[num] = (x - 28, py)
        pins[nm] = (x - 28, py)
    for i, (num, nm) in enumerate(right):
        py = y + (i + 1) * h / (len(right) + 1)
        d.line(x + w, py, x + w + 28, py, "p")
        d.text(x + w + 14, py - 4, str(num), size=6.5, cls="ref",
               anchor="middle")
        d.text(x + w - 6, py + 3, nm, size=7.5, cls="m", anchor="end")
        pins[num] = (x + w + 28, py)
        pins[nm] = (x + w + 28, py)
    for i, (num, nm) in enumerate(top):
        px = x + (i + 1) * w / (len(top) + 1)
        d.line(px, y - 26, px, y, "p")
        d.text(px - 5, y - 10, str(num), size=6.5, cls="ref",
               anchor="end")
        d.text(px + 4, y + 12, nm, size=7, cls="m")
        pins[num] = (px, y - 26)
        pins[nm] = (px, y - 26)
    for i, (num, nm) in enumerate(bottom):
        px = x + (i + 1) * w / (len(bottom) + 1)
        d.line(px, y + h, px, y + h + 26, "p")
        d.text(px - 5, y + h + 22, str(num), size=6.5, cls="ref",
               anchor="end")
        d.text(px + 4, y + h - 6, nm, size=7, cls="m")
        pins[num] = (px, y + h + 26)
        pins[nm] = (px, y + h + 26)
    return pins


def conn(d: D, x, y, ref, title, rows, pitch=21, width=96):
    """Generic pin header, pins on the left.

    rows: (pin, net, kind) with kind in sig/pwr/gnd/nc.
    """
    bh = 30 + len(rows) * pitch + 10
    d.rect(x, y, width, bh, "c", rx=3)
    d.text(x + width / 2, y - 7, ref, size=9, cls="ref", anchor="middle")
    d.text(x + width / 2, y + bh + 13, title, size=8, cls="val",
           anchor="middle")
    d.circle(x + 12, y + 11, 3)
    d.text(x + 20, y + 14, "1", size=6.5, cls="m")
    for i, (pin, net, kind) in enumerate(rows):
        py = y + 30 + i * pitch
        d.line(x - 34, py, x, py, "p")
        d.text(x + 9, py + 3, str(pin), size=7.5, cls="m")
        cls = {"pwr": "pwr", "gnd": "gnd", "nc": "note"}.get(kind, "net")
        d.text(x - 40, py - 3.5, net, size=8, cls=cls, anchor="end",
               weight=700 if kind in ("pwr", "gnd") else 600)
    return bh


def term2(d: D, x, y, ref, l1="+5V", l2="GND"):
    """2-pin screw terminal, pins on the right."""
    h = 84
    d.rect(x, y, 62, h, "c", rx=3)
    d.text(x + 31, y - 6, ref, size=8.5, cls="ref", anchor="middle")
    d.text(x + 31, y + h + 12, "5 V / 3 A", size=7.5, cls="val",
           anchor="middle")
    for i, lab in enumerate((l1, l2)):
        py = y + 25 + i * 34
        d.circle(x + 22, py, 8, "c")
        d.line(x + 22 - 5, py, x + 22 + 5, py, "p")
        d.line(x + 62, py, x + 84, py, "p")
        d.text(x + 74, py - 4, str(i + 1), size=6.5, cls="ref",
               anchor="middle")
        d.text(x + 8, py + 3, lab, size=7.5, cls="m")
    return (x + 84, y + 25), (x + 84, y + 59)


def fuse_h(d: D, x, y, ref, val="3 A"):
    d.rect(x, y - 8, 50, 16, "c", rx=2)
    d.line(x, y, x + 50, y, "p")
    d.text(x + 25, y - 12, ref, size=7, cls="ref", anchor="middle")
    d.text(x + 25, y + 19, val, size=7, cls="val", anchor="middle")
    return (x, y), (x + 50, y)


def diode_h(d: D, x, y, ref, val="SS34"):
    d.line(x, y, x + 12, y, "p")
    d.poly([(x + 12, y - 9), (x + 12, y + 9), (x + 32, y)], "p")
    d.line(x + 32, y - 9, x + 32, y + 9, "p")
    d.line(x + 32, y, x + 44, y, "p")
    d.text(x + 22, y - 13, ref, size=7, cls="ref", anchor="middle")
    d.text(x + 22, y + 20, val, size=7, cls="val", anchor="middle")
    return (x, y), (x + 44, y)


def tvs_v(d: D, x, y, ref, val="SMBJ5.0A"):
    d.rect(x - 15, y, 30, 40, "c", rx=2)
    d.text(x, y + 17, "TVS", size=6.5, cls="ref", anchor="middle")
    d.text(x, y + 29, val, size=6, cls="val", anchor="middle")
    d.line(x, y - 8, x, y, "p")
    d.line(x, y + 40, x, y + 48, "p")
    d.text(x + 18, y + 10, ref, size=6.5, cls="ref")
    return (x, y - 8), (x, y + 48)


def rnet(d: D, x, y, ref, nets, val="100 kΩ"):
    """8-resistor network to +3V3, pins on the left."""
    n = len(nets)
    h = 34 + n * 22 + 8
    d.rect(x, y, 150, h, "c", rx=3)
    d.text(x + 75, y - 7, ref, size=8.5, cls="ref", anchor="middle")
    d.text(x + 75, y + 20, f"{n}× {val}", size=7.5, cls="val",
           anchor="middle")
    rail = x + 178
    for i, net in enumerate(nets):
        py = y + 38 + i * 22
        d.line(x - 24, py, x, py, "p")
        d.text(x - 28, py + 3, str(i + 1), size=6.5, cls="ref",
               anchor="end")
        d.text(x + 8, py + 3, net, size=6.5, cls="m")
        d.rect(x + 45, py - 4, 62, 8, "c")
        d.line(x + 107, py, rail, py, "wt")
        d.net(x - 34, py, net, anchor="end", size=8)
    d.line(rail, y + 38, rail, y + 38 + (n - 1) * 22, "w")
    d.line(rail, y + 38, rail, y + 18, "wt")
    d.pwr(rail, y + 18, "+3V3")
    return y, y + h
