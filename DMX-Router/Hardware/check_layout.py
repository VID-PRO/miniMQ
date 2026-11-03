#!/usr/bin/env python3
"""Rough geometry check for the generated SVG sheets.

Reports text outside the drawing frame and text/text overlaps.  Text
widths are estimated; the check is deliberately conservative.
"""

import re
import sys

TEXT = re.compile(
    r'<text x="([-\d.]+)" y="([-\d.]+)" font-size="([-\d.]+)" '
    r'class="([^"]*)" text-anchor="([^"]*)"[^>]*>(.*?)</text>')
SVG = re.compile(r'<svg[^>]*width="(\d+)" height="(\d+)"')


def unesc(s):
    return (s.replace("&amp;", "&").replace("&lt;", "<")
            .replace("&gt;", ">").replace("&quot;", '"'))


def main(path):
    src = open(path, encoding="utf-8").read()
    m = SVG.search(src)
    if not m:
        print("no svg size")
        return
    W, H = int(m.group(1)), int(m.group(2))
    boxes = []
    for x, y, size, cls, anchor, txt in TEXT.findall(src):
        x, y, size = float(x), float(y), float(size)
        s = unesc(txt)
        if not s.strip():
            continue
        w = len(s) * size * (0.62 if "m" in cls.split() else 0.58)
        if anchor == "middle":
            x0 = x - w / 2
        elif anchor == "end":
            x0 = x - w
        else:
            x0 = x
        boxes.append((x0, y - size * 0.78, x0 + w, y + size * 0.24, s,
                      x, y, size))

    print(f"{path}: {W}x{H}, {len(boxes)} texts")
    bad = 0
    for b in boxes:
        if b[0] < 24 or b[2] > W - 24 or b[1] < 24 or b[3] > H - 24:
            print(f"  OOB  {b[4]!r}  x={b[0]:.0f}..{b[2]:.0f} "
                  f"y={b[1]:.0f}..{b[3]:.0f}")
            bad += 1
    overlaps = 0
    for i in range(len(boxes)):
        a = boxes[i]
        for j in range(i + 1, len(boxes)):
            b = boxes[j]
            if (a[0] < b[2] - 1 and b[0] < a[2] - 1 and
                    a[1] < b[3] - 1 and b[1] < a[3] - 1):
                # ignore identical anchor points / tiny texts
                if abs(a[5] - b[5]) < 0.5 and abs(a[6] - b[6]) < 0.5:
                    continue
                print(f"  OVL  {a[4]!r} <-> {b[4]!r}  "
                      f"({a[0]:.0f},{a[1]:.0f})-({a[2]:.0f},{a[3]:.0f}) / "
                      f"({b[0]:.0f},{b[1]:.0f})-({b[2]:.0f},{b[3]:.0f})")
                overlaps += 1
                if overlaps > 40:
                    print("  ...")
                    break
        if overlaps > 40:
            break
    print(f"  {bad} out of bounds, {overlaps} overlapping text pairs")


if __name__ == "__main__":
    for p in sys.argv[1:]:
        main(p)
