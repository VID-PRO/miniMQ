#!/usr/bin/env python3
# ------------------------------------------------------------------
#  Sheet 1: input board (4 identical DMX/RDM input channels)
# ------------------------------------------------------------------

from schematic_lib import (D, frame, note_box, xlr, sm712_v, iso1410,
                           b0505s, conn, NET)


def channel(d: D, y0: float, ch: int):
    """One isolated DMX/RDM input channel."""
    xlr_ref = f"J{ch + 1}"
    iso_ref = f"U{ch}"
    tvs_ref = f"D{ch}"
    r_sig = 3 * (ch - 1) + 1
    r_term = 3 * (ch - 1) + 3

    d.text(40, y0 + 10, f"IN{ch}", size=13, cls="t", weight=700)

    xs = xlr(d, 46, y0 + 30, xlr_ref, "female")
    a_y = y0 + 80
    b_y = y0 + 180

    # --- XLR shield ------------------------------------------------
    d.wire(xs["1"], (140, xs["1"][1]))
    d.net(144, xs["1"][1], f"GND_ISO{ch}", size=8)

    # --- D+ -> A, D- -> B with 10 ohm series resistors -------------
    d.wire(xs["3"], (190, xs["3"][1]), (190, a_y), (200, a_y))
    _, x2 = d.res_h(200, a_y, f"R{r_sig}", "10 Ω")
    d.wire((x2, a_y), (490, a_y))

    d.wire(xs["2"], (150, xs["2"][1]), (150, b_y), (200, b_y))
    _, x2 = d.res_h(200, b_y, f"R{r_sig + 1}", "10 Ω")
    d.wire((x2, b_y), (490, b_y))

    # --- switchable 120 ohm termination ----------------------------
    d.dot(270, a_y)
    d.wire((270, a_y), (270, a_y + 10))
    d.res_v(270, a_y + 10, f"R{r_term}", "120 Ω", 46)
    d.jumper(270, a_y + 56, b_y, f"JP{ch}", closed=True)
    d.dot(270, b_y)
    d.text(286, b_y + 22, "werksseitig geschlossen", size=7,
           cls="note")

    # --- SM712 surge protection ------------------------------------
    p1, p2, p3 = sm712_v(d, 360, a_y, b_y, tvs_ref)
    d.dot(360, a_y)
    d.dot(360, b_y)
    d.wire(p3, (416, p3[1]))
    d.net(420, p3[1], f"GND_ISO{ch}", size=7.5)

    # --- transceiver ------------------------------------------------
    p = iso1410(d, 520, y0 + 40, iso_ref)

    # logic supply and ground
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

    # logic signals
    d.wire(p[3], (750, p[3][1]))
    d.net(754, p[3][1], f"RXI{ch}", size=9)

    d.wire(p[4], (718, p[4][1]))
    d.wire((718, p[4][1]), (718, p[5][1]))
    d.wire(p[5], (718, p[5][1]))
    d.dot(718, p[4][1])
    d.dot(718, p[5][1])
    d.wire((718, p[5][1]), (750, p[5][1]))
    d.net(754, p[5][1], "DIR_IN", size=9)

    d.wire(p[6], (750, p[6][1]))
    d.net(754, p[6][1], "TXD_IN", size=9)

    # pin 7 is NC on the ISO1410DWR (X is drawn by the symbol)

    # isolated bus supply + decoupling
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


def sheet_input(path: str):
    W, H = 1800, 1450
    d = D()
    frame(d, W, H,
          "Eingangsplatine 4 × DMX/RDM",
          "4 × isolierter ISO1410DWR-Empfänger · XLR5 female · 120 Ω Abschluss · 5 V über B0505S-1W",
          1, 4,
          "Eine Platine. Alle vier Kanäle sind identisch; J1 verbindet die Platine mit dem Controller.")

    for i in range(4):
        channel(d, 120 + i * 310, i + 1)

    # --- J1 controller connector -----------------------------------
    rows = [
        (1, "+3V3", "pwr"), (2, "+3V3", "pwr"),
        (3, "+5V", "pwr"), (4, "+5V", "pwr"),
        (5, "GND", "gnd"), (6, "GND", "gnd"),
        (7, "TXD_IN", "sig"), (8, "TXD_IN", "sig"),
        (9, "TXD_IN", "sig"), (10, "TXD_IN", "sig"),
        (11, "DIR_IN", "sig"),
        (12, "RXI1", "sig"), (13, "RXI2", "sig"),
        (14, "RXI3", "sig"), (15, "RXI4", "sig"),
        (16, "GND", "gnd"),
    ]
    conn(d, 1560, 140, "J1", "zum Controller (2×8)", rows, pitch=21)
    d.text(1400, 520, "Pins 7..10 sind auf dieser Platine", size=8,
           cls="note")
    d.text(1400, 534, "miteinander verbunden (TXD_IN).", size=8,
           cls="note")

    # --- isolated supplies -----------------------------------------
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

    # --- notes ------------------------------------------------------
    note_box(d, 1400, 560, 340, 180, "Kanalzuordnung", [
        "IN1   J2   U1   RXI1   R1/R2/R3   JP1",
        "IN2   J3   U2   RXI2   R4/R5/R6   JP2",
        "IN3   J4   U3   RXI3   R7/R8/R9   JP3",
        "IN4   J5   U4   RXI4   R10..R12   JP4",
        "D1..D4: SM712,  U5..U8: B0505S-1W",
        "J1: 2×8-Wannenstecker, Raster 2,54 mm",
    ], size=8.2)

    note_box(d, 1400, 760, 340, 190, "Abschluss und Schirm", [
        "Der 120-Ω-Abschluss sitzt am Router,",
        "weil er das Ende des Eingangsstrangs ist.",
        "JP1..JP4 nur öffnen, wenn die Platine",
        "nicht am Strangende sitzt.",
        "",
        "XLR Pin 1 liegt auf GND_ISO. Optional",
        "1 MΩ || 4,7 nF / 1 kV von GND_ISO an PE.",
        "SM712 schützt A/B gegen ESD und Surge.",
    ], size=8.0)

    note_box(d, 1400, 970, 340, 340, "Stückliste (1 Platine)", [
        "4 × U1..U4    ISO1410DWR (SOIC-16 DW)",
        "4 × U5..U8    B0505S-1W",
        "4 × D1..D4    SM712 (SOT-23)",
        "4 × J2..J5    XLR5 female, PCB",
        "1 × J1        2×8 Wannenstecker",
        "8 × R1,R2,R4,R5,R7,R8,R10,R11   10 Ω",
        "4 × R3,R6,R9,R12                120 Ω",
        "4 × JP1..JP4  Lötbrücke, geschlossen",
        "8 × 100 nF    VCC1 + VCC2 je Kanal",
        "4 × 100 nF    B0505S-Ausgang",
        "4 × 10 µF/25 V  5-V-Eingang",
        "4 × 10 µF/16 V  isolierte Seite",
        "optional je Kanal: 1 MΩ + 4,7 nF/1 kV",
        "",
        "VCC1 immer 3,3 V. Bei 5 V an VCC1",
        "liegen 5 V an den RP2040-Eingängen.",
    ], size=7.6)

    d.save(path, W, H)
