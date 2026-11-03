#!/usr/bin/env python3
# ------------------------------------------------------------------
#  Sheet 0: system overview
# ------------------------------------------------------------------

from schematic_lib import D, frame, note_box


def block(d: D, x, y, w, h, title, lines, fill="#ffffff"):
    d.rect(x, y, w, h, "c", rx=5)
    d.text(x + w / 2, y + 22, title, size=10.5, cls="t", anchor="middle",
           weight=700)
    yy = y + 40
    for ln in lines:
        d.text(x + w / 2, yy, ln, size=7.6, cls="note", anchor="middle")
        yy += 12.5


def table(d: D, x, y, widths, rows, row_h=34):
    total = sum(widths)
    for r, row in enumerate(rows):
        yy = y + r * row_h
        cls = "tb"
        d.rect(x, yy, total, row_h, cls)
        cx = x
        for c, (w, cell) in enumerate(zip(widths, row)):
            if c:
                d.line(cx, yy, cx, yy + row_h, "frm")
            size = 8.2 if r else 8.6
            weight = 700 if r == 0 else None
            d.text(cx + 8, yy + row_h / 2 + 3, cell, size=size,
                   cls="t" if r == 0 else "note", weight=weight)
            cx += w
    return y + len(rows) * row_h


def sheet_overview(path: str):
    W, H = 1700, 1150
    d = D()
    frame(d, W, H,
          "DMX-Router 4 → 16 — Systemübersicht",
          "4 DMX/RDM-Eingänge · 16 DMX-Ausgänge · HTP-Merge · best-effort RDM-Relay",
          0, 4,
          "Blockschaltbild; Details auf Blatt 1 (Eingang), 2 (Ausgang) und 3 (Controller).")

    # --- inputs ----------------------------------------------------
    d.text(70, 152, "DMX/RDM-Eingänge", size=9.5, cls="t", weight=700)
    for i in range(4):
        y = 170 + i * 62
        d.rect(70, y, 80, 48, "c", rx=3)
        d.text(110, y + 21, f"IN{i + 1}", size=9, cls="t", anchor="middle",
               weight=700)
        d.text(110, y + 36, "XLR5 female", size=6.8, cls="val",
               anchor="middle")
        d.arrow(150, y + 24, 250, y + 24)

    block(d, 250, 150, 190, 350, "Eingangsplatine", [
        "4 × ADM2483",
        "4 × B0505S-1W",
        "SM712 + 10 Ω",
        "120 Ω Abschluss",
        "",
        "TXD_IN gemeinsam",
        "DIR_IN gemeinsam",
        "RXI1..4 getrennt",
    ])

    # --- controller ------------------------------------------------
    d.rect(520, 130, 380, 480, "c", rx=6)
    d.text(710, 156, "Controllerplatine", size=11.5, cls="t",
           anchor="middle", weight=700)

    block(d, 540, 180, 140, 175, "Pico", [
        "RP2040",
        "USB-Konsole",
        "16 OUT / 4 IN",
        "I²C1",
    ])
    block(d, 700, 180, 180, 90, "PCF8574", [
        "DIR_OUT",
        "MUX A/B/C (74HC125)",
        "4 Tasten",
    ])
    block(d, 700, 290, 180, 120, "2 × 74HC151", [
        "RDM-Rückkanal",
        "U4: OUT1..8",
        "U5: OUT9..16",
    ])
    d.text(710, 450, "J1: Eingangsplatine", size=8, cls="note",
           anchor="middle")
    d.text(710, 466, "J2..J5: Ausgangsplatinen", size=8, cls="note",
           anchor="middle")
    d.text(710, 482, "J6: OLED · J7: Tasten", size=8, cls="note",
           anchor="middle")
    d.text(710, 500, "J8: 5 V / 3 A", size=8, cls="note",
           anchor="middle")

    d.arrow(440, 325, 520, 325)
    d.text(480, 317, "J1", size=8, cls="net", anchor="middle")

    # --- output boards --------------------------------------------
    d.text(1000, 132, "Ausgangsplatinen (4× identisch)", size=9.5,
           cls="t", weight=700)
    for i in range(4):
        y = 140 + i * 125
        block(d, 1000, y, 170, 90, f"Platine #{i + 1}", [
            "4 × ADM2483",
            f"OUT{4 * i + 1}..OUT{4 * i + 4}",
        ])
        block(d, 1240, y, 170, 90, "4 × XLR5 male", [
            f"OUT{4 * i + 1}..OUT{4 * i + 4}",
            "560 Ω Bias",
        ])
        d.arrow(1170, y + 45, 1240, y + 45)
        d.arrow(900, y + 45, 1000, y + 45)
        d.text(950, y + 37, f"J{i + 2}", size=8, cls="net",
               anchor="middle")

    # --- power -----------------------------------------------------
    block(d, 250, 560, 190, 80, "5 V / 3 A", [
        "Pico VSYS",
        "B0505S über J1..J5",
    ])
    d.arrow(345, 560, 345, 500)
    d.text(352, 535, "5 V", size=8, cls="pwr", weight=700)

    # --- mapping table --------------------------------------------
    rows = [
        ("Platine / Stecker", "Datenleitungen", "RDM-Rückkanal",
         "DMX-Ports", "Bemerkung"),
        ("Eingangsplatine J1", "TXD_IN Pin 7..10",
         "RXI1..4 Pin 12..15", "IN1..4", "4 × XLR5 female"),
        ("Ausgang #1 J2", "OUT1..4 Pin 7..10",
         "RDM1..4 → MUX1 D0..D3", "OUT1..4", "XLR5 male"),
        ("Ausgang #2 J3", "OUT5..8 Pin 7..10",
         "RDM5..8 → MUX1 D4..D7", "OUT5..8", "XLR5 male"),
        ("Ausgang #3 J4", "OUT9..12 Pin 7..10",
         "RDM9..12 → MUX2 D0..D3", "OUT9..12", "XLR5 male"),
        ("Ausgang #4 J5", "OUT13..16 Pin 7..10",
         "RDM13..16 → MUX2 D4..D7", "OUT13..16", "XLR5 male"),
    ]
    table(d, 60, 700, [230, 250, 330, 180, 500], rows)

    note_box(d, 60, 920, 1060, 190, "Systemhinweise", [
        "Alle Platinen teilen GND und 3,3 V. Die DMX-Busseiten sind je Kanal über ADM2483 und B0505S galvanisch getrennt.",
        "J1..J5 sind 2×8-Wannenstecker mit 2,54 mm Raster; Flachbandkabel kurz halten und abwechselnd GND führen.",
        "Eingang: 120 Ω fest am Router (Strangende). Ausgang: 560 Ω Fail-Safe-Bias je Port, 120 Ω nur am letzten Gerät des Strangs.",
        "RDM ist best-effort: ein gemeinsamer DIR_OUT und ein Rückkanal je Ausgang; Discovery findet pro Zweig nur den ersten Responder.",
        "Bestückung: 1 × Eingangsplatine, 4 × Ausgangsplatine, 1 × Controllerplatine. 5-V-Netzteil mindestens 3 A.",
    ], size=8.0, line_gap=13)

    d.save(path, W, H)
