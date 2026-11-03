#!/usr/bin/env python3
# ------------------------------------------------------------------
#  Sheet 2: output board (4 identical DMX output channels)
#
#  The same board is used four times.  On board #n, TXD1..4 carry
#  OUT(4n-3)..OUT(4n) and RDM1..4 return the RDM receive lines.
# ------------------------------------------------------------------

from schematic_lib import (D, frame, note_box, xlr, sm712_v, iso1410,
                           b0505s, conn)


def channel(d: D, y0: float, ch: int):
    jx = f"J{ch + 1}"
    ux = f"U{ch}"
    dx = f"D{ch}"
    r_sig = 3 * (ch - 1) + 1
    r_term = 3 * (ch - 1) + 3
    r_bias = 2 * ch + 12

    d.text(40, y0 + 10, f"OUT{ch}", size=13, cls="t", weight=700)
    d.text(104, y0 + 10, "auf dieser Platine", size=7.5, cls="note")

    xs = xlr(d, 46, y0 + 30, jx, "male")
    a_y = y0 + 80
    b_y = y0 + 180

    d.wire(xs["1"], (140, xs["1"][1]))
    d.net(144, xs["1"][1], f"GND_ISO{ch}", size=8)

    # D+ -> A, D- -> B
    d.wire(xs["3"], (190, xs["3"][1]), (190, a_y), (200, a_y))
    _, x2 = d.res_h(200, a_y, f"R{r_sig}", "10 Ω")
    d.wire((x2, a_y), (490, a_y))

    d.wire(xs["2"], (150, xs["2"][1]), (150, b_y), (200, b_y))
    _, x2 = d.res_h(200, b_y, f"R{r_sig + 1}", "10 Ω")
    d.wire((x2, b_y), (490, b_y))

    # optional 120 ohm termination, default open
    d.dot(270, a_y)
    d.wire((270, a_y), (270, a_y + 10))
    d.res_v(270, a_y + 10, f"R{r_term}", "120 Ω", 46)
    d.jumper(270, a_y + 56, b_y, f"JP{ch}", closed=False)
    d.dot(270, b_y)
    d.text(286, b_y + 22, "JP offen", size=7, cls="note")

    # 560 ohm fail-safe bias: A -> +5V_ISO, B -> GND_ISO
    d.dot(320, a_y)
    d.res_v(320, y0 + 34, f"R{r_bias}", "560 Ω", 46)
    d.wire((320, y0 + 34), (320, y0 + 28))
    d.text(320, y0 + 24, f"+5V_ISO{ch}", size=7.5, cls="pwr",
           anchor="middle", weight=700)

    d.dot(320, b_y)
    d.wire((320, b_y), (320, b_y + 4))
    d.res_v(320, b_y + 4, f"R{r_bias + 1}", "560 Ω", 46)
    d.gnd(320, b_y + 50, f"GND_ISO{ch}", iso=True)

    # surge protection
    p1, p2, p3 = sm712_v(d, 380, a_y, b_y, dx)
    d.dot(380, a_y)
    d.dot(380, b_y)
    d.wire(p3, (436, p3[1]))
    d.net(440, p3[1], f"GND_ISO{ch}", size=7.5)

    # transceiver
    p = iso1410(d, 520, y0 + 40, ux)

    d.wire(p[1], (750, p[1][1]))
    d.pwr(730, p[1][1], "+3V3")
    d.dot(730, p[1][1])
    d.cap_v(750, p[1][1], f"C{2 * ch - 1}", "100 nF", 24)
    d.gnd(750, p[1][1] + 24)
    d.wire(p[2], (720, p[2][1]))
    d.wire((720, p[2][1]), (720, p[8][1]))
    d.wire(p[8], (720, p[8][1]))
    d.dot(720, p[2][1])
    d.dot(720, p[8][1])
    d.gnd(720, p[8][1], "GND")

    d.wire(p[3], (750, p[3][1]))
    d.net(754, p[3][1], f"RDM{ch}", size=9)

    d.wire(p[4], (718, p[4][1]))
    d.wire((718, p[4][1]), (718, p[5][1]))
    d.wire(p[5], (718, p[5][1]))
    d.dot(718, p[4][1])
    d.dot(718, p[5][1])
    d.wire((718, p[5][1]), (750, p[5][1]))
    d.net(754, p[5][1], "DIR_OUT", size=9)

    d.wire(p[6], (750, p[6][1]))
    d.net(754, p[6][1], f"TXD{ch}", size=9)

    # pin 7 is NC on the ISO1410DWR (X is drawn by the symbol)

    # isolated supply + decoupling
    d.dot(p[16][0], p[16][1])
    d.wire(p[16], (540, p[16][1]))
    d.net(536, p[16][1], f"+5V_ISO{ch}", anchor="end", size=8)

    d.wire(p[16], (p[16][0], p[16][1] + 16))
    d.cap_v(p[16][0], p[16][1] + 16, f"C{2 * ch}", "100 nF", 24)
    d.wire((p[16][0], p[16][1] + 40), (p[9][0], p[16][1] + 40))
    d.wire(p[15], (p[15][0], p[16][1] + 40))
    d.wire(p[9], (p[9][0], p[16][1] + 40))
    d.dot(p[15][0], p[16][1] + 40)
    d.dot(p[9][0], p[16][1] + 40)
    d.wire((p[9][0], p[16][1] + 40), (p[9][0] + 20, p[16][1] + 40))
    d.net(p[9][0] + 24, p[16][1] + 40, f"GND_ISO{ch}", size=8)


def sheet_output(path: str):
    W, H = 1800, 1450
    d = D()
    frame(d, W, H,
          "Ausgangsplatine 4 × DMX (4× identisch)",
          "4 × isolierter ISO1410DWR-Treiber · XLR5 male · 560 Ω Fail-Safe-Bias · optionaler 120-Ω-Abschluss",
          2, 4,
          "Diese Platine wird viermal gebaut. Tabelle rechts: Zuordnung der Platine zu OUT1..OUT16.")

    for i in range(4):
        channel(d, 120 + i * 310, i + 1)

    rows = [
        (1, "+3V3", "pwr"), (2, "+3V3", "pwr"),
        (3, "+5V", "pwr"), (4, "+5V", "pwr"),
        (5, "GND", "gnd"), (6, "GND", "gnd"),
        (7, "TXD1", "sig"), (8, "TXD2", "sig"),
        (9, "TXD3", "sig"), (10, "TXD4", "sig"),
        (11, "DIR_OUT", "sig"),
        (12, "RDM1", "sig"), (13, "RDM2", "sig"),
        (14, "RDM3", "sig"), (15, "RDM4", "sig"),
        (16, "GND", "gnd"),
    ]
    conn(d, 1560, 140, "J1", "zum Controller (2×8)", rows, pitch=21)
    d.text(1400, 520, "TXD1..4 = OUT(4n-3)..OUT(4n),", size=8,
           cls="note")
    d.text(1400, 534, "RDM1..4 = Rückkanal dieser Platine.", size=8,
           cls="note")

    d.text(1080, 596, "Isolierte Versorgung je Kanal", size=11,
           cls="t", weight=700)
    d.line(1076, 604, 1350, 604, "wt")

    for i in range(4):
        ch = i + 1
        x, y = 1100, 620 + i * 130
        p = b0505s(d, x, y, f"U{4 + ch}")
        d.wire(p[1], (x - 50, p[1][1]))
        d.pwr(x - 50, p[1][1], "+5V")
        d.wire(p[2], (x - 50, p[2][1]))
        d.gnd(x - 50, p[2][1], "GND")
        d.wire(p[3], (x + 172, p[3][1]))
        d.net(x + 176, p[3][1], f"+5V_ISO{ch}", size=8)
        d.wire(p[4], (x + 172, p[4][1]))
        d.net(x + 176, p[4][1], f"GND_ISO{ch}", size=8)

    note_box(d, 1040, 1120, 340, 150, "Modul-Beschaltung", [
        "Je B0505S-1W:",
        "10 µF / 25 V am 5-V-Eingang,",
        "10 µF / 16 V + 100 nF am",
        "isolierten 5-V-Ausgang.",
        "Die Module sind untereinander",
        "nicht parallel geschaltet.",
    ], size=8)

    note_box(d, 1400, 560, 340, 190, "Zuordnung der vier Platinen", [
        "Platine #1  J1 = J2 am Controller  OUT1..4",
        "Platine #2  J1 = J3 am Controller  OUT5..8",
        "Platine #3  J1 = J4 am Controller  OUT9..12",
        "Platine #4  J1 = J5 am Controller  OUT13..16",
        "",
        "RDM-Rückkanal: Platine #1/#2 an",
        "74HC151 #1 (D0..D7), #3/#4 an #2.",
        "J1: 2×8-Wannenstecker, 2,54 mm.",
    ], size=7.9)

    note_box(d, 1400, 770, 340, 200, "Bias / Abschluss", [
        "Jeder Ausgang hat 560 Ω nach +5V_ISO",
        "(A) und 560 Ω nach GND_ISO (B).",
        "Das ist der Fail-Safe-Bias für den",
        "jeweiligen DMX-Strang.",
        "",
        "Der 120-Ω-Abschluss ist optional und",
        "standardmäßig offen. Er gehört an das",
        "letzte Gerät des Strangs, nicht an den",
        "Router. JP1..JP4 nur schließen, wenn",
        "der Port das Strangende ist.",
        "",
        "XLR Pin 1 liegt auf GND_ISO. Optional",
        "1 MΩ || 4,7 nF / 1 kV an PE.",
    ], size=7.8)

    note_box(d, 1400, 990, 340, 320, "Stückliste (1 Platine)", [
        "4 × U1..U4    ISO1410DWR (SOIC-16 DW)",
        "4 × U5..U8    B0505S-1W",
        "4 × D1..D4    SM712 (SOT-23)",
        "4 × J2..J5    XLR5 male, PCB",
        "1 × J1        2×8 Wannenstecker",
        "8 × R1,R2,R4,R5,R7,R8,R10,R11   10 Ω",
        "8 × R13..R20  560 Ω 1 % (Bias)",
        "4 × R3,R6,R9,R12  120 Ω (optional)",
        "4 × JP1..JP4  Lötbrücke, offen",
        "8 × 100 nF    VCC1 + VCC2 je Kanal",
        "4 × 100 nF    B0505S-Ausgang",
        "4 × 10 µF/25 V  5-V-Eingang",
        "4 × 10 µF/16 V  isolierte Seite",
        "",
        "VCC1 immer 3,3 V; VCC2 = 5 V isoliert.",
        "Alle vier Kanäle sind galvanisch getrennt.",
    ], size=7.5)

    d.save(path, W, H)
