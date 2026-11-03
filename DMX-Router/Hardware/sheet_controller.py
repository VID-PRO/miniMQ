#!/usr/bin/env python3
# ------------------------------------------------------------------
#  Sheet 3: controller board
# ------------------------------------------------------------------

from schematic_lib import (D, frame, note_box, pico, ic, mux151, conn,
                           rnet, term2, fuse_h, diode_h, tvs_v)


def buffer_block(d: D, x: float, y: float):
    """74HC125, four buffers with the /OE pins tied to GND."""
    w, h = 200, 260
    d.rect(x, y, w, h, "c", rx=3)
    d.text(x + w / 2, y - 7, "U3", size=9, cls="ref", anchor="middle")
    d.text(x + 20, y + 20, "74HC125", size=8.5, cls="ref")
    d.text(x + 20, y + 34, "4× Buffer, /OE low", size=7, cls="val")

    pins = {}
    rows = [(2, "1A", 3, "1Y", 30), (5, "2A", 6, "2Y", 90),
            (9, "3A", 8, "3Y", 150), (12, "4A", 11, "4Y", 210)]
    for na, la, ny, ly, dy in rows:
        py = y + dy
        d.line(x - 28, py, x, py, "p")
        d.text(x - 14, py - 4, str(na), size=6.5, cls="ref",
               anchor="middle")
        d.text(x + 6, py + 3, la, size=7.5, cls="m")
        pins[na] = (x - 28, py)
        pins[la] = (x - 28, py)

        d.line(x + w, py, x + w + 28, py, "p")
        d.text(x + w + 14, py - 4, str(ny), size=6.5, cls="ref",
               anchor="middle")
        d.text(x + w - 6, py + 3, ly, size=7.5, cls="m", anchor="end")
        pins[ny] = (x + w + 28, py)
        pins[ly] = (x + w + 28, py)

    # VCC / GND
    d.line(x + 40, y - 26, x + 40, y, "p")
    d.text(x + 35, y - 10, "14", size=6.5, cls="ref", anchor="end")
    d.text(x + 35, y + 12, "VCC", size=7, cls="m", anchor="end")
    pins[14] = (x + 40, y - 26)

    d.line(x + 40, y + h, x + 40, y + h + 26, "p")
    d.text(x + 35, y + h + 22, "7", size=6.5, cls="ref", anchor="end")
    d.text(x + 35, y + h - 6, "GND", size=7, cls="m", anchor="end")
    pins[7] = (x + 40, y + h + 26)

    # /OE pins at the bottom edge, all to GND
    oe = [(1, 90), (4, 115), (10, 140), (13, 165)]
    for num, dx in oe:
        px = x + dx
        d.line(px, y + h, px, y + h + 26, "p")
        d.text(px + 3, y + h - 6, f"{num}OE", size=6.5, cls="ref")
        d.text(px + 3, y + h + 21, str(num), size=6, cls="ref")
    d.line(x + 90, y + h + 40, x + 165, y + h + 40, "w")
    for _, dx in oe:
        d.line(x + dx, y + h + 26, x + dx, y + h + 40, "wt")
    d.gnd(x + 100, y + h + 40)
    d.text(x + 8, y + h + 44, "/OE", size=7, cls="note")
    return pins


def sheet_controller(path: str):
    W, H = 1900, 1450
    d = D()
    frame(d, W, H,
          "Controllerplatine (Raspberry Pi Pico)",
          "Pico + PCF8574/74HC125 + 2 × 74HC151 · 4 Eingangs-/4 Ausgangsanschlüsse · 5-V-Eingang",
          3, 4,
          "Der PCF8574 ist Pflicht: DIR_OUT und die drei MUX-SELECT-Leitungen liegen auf P0..P3.")

    # --------------------------------------------------------------
    #  Pico
    # --------------------------------------------------------------
    p = pico(d, 160, 220)

    left_gp = [(1, 0), (2, 1), (4, 2), (5, 3), (6, 4), (7, 5),
               (9, 6), (10, 7), (11, 8), (12, 9), (14, 10),
               (15, 11), (16, 12), (17, 13), (19, 14), (20, 15)]
    for pin, gp in left_gp:
        x, y = p[pin]
        d.net(x - 4, y, f"OUT{gp + 1}", anchor="end", size=8.5)

    for pin in (3, 8, 13, 18):
        x, y = p[pin]
        d.text(x - 4, y + 3, "GND", size=8, cls="gnd", anchor="end",
               weight=700)

    right_nets = {
        34: ("MUX_Y2", "sig"), 32: ("SDA", "sig"), 31: ("SCL", "sig"),
        29: ("MUX_Y1", "sig"), 27: ("DIR_IN", "sig"),
        26: ("TXD_IN", "sig"), 25: ("RXI4", "sig"), 24: ("RXI3", "sig"),
        22: ("RXI2", "sig"), 21: ("RXI1", "sig"),
    }
    for pin, (name, _) in right_nets.items():
        x, y = p[pin]
        d.wire((x, y), (x + 24, y))
        d.net(x + 28, y, name, size=8.5)

    for pin in (38, 33, 28, 23):
        x, y = p[pin]
        d.text(x + 28, y + 3, "GND", size=8, cls="gnd", weight=700)

    d.wire(p[39], (500, p[39][1]))
    d.pwr(500, p[39][1], "+5V")
    d.wire(p[36], (500, p[36][1]))
    d.pwr(500, p[36][1], "+3V3")
    for pin in (40, 37, 35, 30):
        x, y = p[pin]
        d.nc(x + 4, y, 5)

    # --------------------------------------------------------------
    #  5 V input / protection
    # --------------------------------------------------------------
    t = term2(d, 500, 180, "J8", "+5V_IN", "GND")
    d.wire(t[0], (595, t[0][1]))
    fuse_h(d, 595, t[0][1], "F1", "3 A")
    d.wire((645, t[0][1]), (660, t[0][1]))
    diode_h(d, 660, t[0][1], "D1", "SS34")
    d.wire((704, t[0][1]), (760, t[0][1]))
    d.dot(720, t[0][1])
    d.wire((720, t[0][1]), (720, t[0][1] - 8))
    tvs_v(d, 720, t[0][1], "D2", "SMBJ5.0A")
    d.gnd(720, t[0][1] + 48)
    d.wire(t[1], (600, t[1][1]))
    d.gnd(600, t[1][1])

    d.dot(704, t[0][1])
    d.wire((704, t[0][1]), (704, t[0][1] - 15))
    d.pwr(704, t[0][1] - 15, "+5V")

    d.wire((760, t[0][1]), (760, t[0][1] + 8))
    d.cap_v(760, t[0][1] + 8, "C1", "470 µF", 40)
    d.gnd(760, t[0][1] + 48)

    note_box(d, 60, 110, 370, 96, "5-V-Versorgung", [
        "5 V / 3 A (20 × B0505S-1W).",
        "F1 3 A, D1 SS34 verpolsicher, D2 SMBJ5.0A, C1 470 µF/10 V.",
        "Pico VSYS = +5 V; 3,3 V kommen aus dem Pico-Regler.",
    ], size=7.4, line_gap=12)

    # --------------------------------------------------------------
    #  PCF8574
    # --------------------------------------------------------------
    pcf = ic(d, 820, 190, 170, 240, "U2", "PCF8574",
             left=[(1, "A0"), (2, "A1"), (3, "A2"),
                   (4, "P0"), (5, "P1"), (6, "P2"), (7, "P3")],
             right=[(15, "SDA"), (14, "SCL"), (13, "/INT"),
                    (12, "P7"), (11, "P6"), (10, "P5"), (9, "P4")],
             top=[(16, "VDD")], bottom=[(8, "VSS")],
             sub="I²C 0x20")

    for pin in (1, 2, 3):
        x, y = pcf[pin]
        d.text(x - 4, y + 3, "GND", size=7.5, cls="gnd", anchor="end",
               weight=700)

    d.pwr(pcf[16][0], pcf[16][1], "+3V3")
    d.gnd(pcf[8][0], pcf[8][1])

    pcf_out = {4: "PCF_P0", 5: "PCF_P1", 6: "PCF_P2", 7: "PCF_P3"}
    for pin, name in pcf_out.items():
        x, y = pcf[pin]
        d.net(x - 4, y, name, anchor="end", size=7.8)

    d.wire(pcf[15], (1045, pcf[15][1]))
    d.net(1030, pcf[15][1], "SDA", size=7.8)
    d.wire(pcf[14], (1075, pcf[14][1]))
    d.net(1030, pcf[14][1], "SCL", size=7.8)

    d.nc(pcf[13][0] + 4, pcf[13][1], 5)

    btn_nets = {12: "BTN_BACK", 11: "BTN_OK", 10: "BTN_DOWN",
                9: "BTN_UP"}
    for pin, name in btn_nets.items():
        x, y = pcf[pin]
        d.wire((x, y), (x + 16, y))
        d.net(x + 20, y, name, size=7.8)

    # I2C pull-ups
    d.wire((1045, 150), (1045, 196))
    d.res_v(1045, 150, "R1", "4,7 kΩ", 46)
    d.wire((1045, 196), (1045, pcf[15][1]))
    d.wire((1075, 150), (1075, 196))
    d.res_v(1075, 150, "R2", "4,7 kΩ", 46)
    d.wire((1075, 196), (1075, pcf[14][1]))
    d.wire((1045, 150), (1045, 140), (1075, 140), (1075, 150))
    d.pwr(1060, 140, "+3V3")

    # --------------------------------------------------------------
    #  decoupling capacitors for U2..U5
    # --------------------------------------------------------------
    d.text(1140, 120, "Entkopplung", size=10, cls="t", weight=700)
    d.line(1136, 127, 1400, 127, "wt")
    rail_top, rail_bot = 165, 285
    d.wire((1140, rail_top), (1400, rail_top))
    d.pwr(1270, rail_top, "+3V3")
    for i, ref in enumerate(("C2", "C3", "C4", "C5")):
        x = 1155 + i * 72
        d.dot(x, rail_top)
        d.wire((x, rail_top), (x, 195))
        d.cap_v(x, 195, ref, "100 nF", 30)
        d.wire((x, 225), (x, rail_bot))
        d.dot(x, rail_bot)
    d.wire((1140, rail_bot), (1400, rail_bot))
    d.gnd(1270, rail_bot)
    d.text(1140, 342, "je 100 nF an U2..U5", size=7.5, cls="note")

    # --------------------------------------------------------------
    #  74HC125
    # --------------------------------------------------------------
    buf = buffer_block(d, 820, 560)
    d.pwr(buf[14][0], buf[14][1], "+3V3")
    d.gnd(buf[7][0], buf[7][1])

    for i, pin in enumerate((2, 5, 9, 12)):
        x, y = buf[pin]
        d.net(x - 4, y, f"PCF_P{i}", anchor="end", size=8)

    for pin, name in ((3, "DIR_OUT"), (6, "MUX_A"), (8, "MUX_B"),
                      (11, "MUX_C")):
        x, y = buf[pin]
        d.net(x + 4, y, name, size=8.5)

    # --------------------------------------------------------------
    #  pull-ups on the PCF port pins that drive the buffer
    # --------------------------------------------------------------
    rnet(d, 850, 900, "RN3", ["PCF_P0", "PCF_P1", "PCF_P2", "PCF_P3"],
         val="10 kΩ")

    # --------------------------------------------------------------
    #  RDM return multiplexers
    # --------------------------------------------------------------
    for m in range(2):
        y0 = 740 + m * 330
        mux = mux151(d, 600, y0, f"U{4 + m}")
        d.pwr(mux[16][0], mux[16][1], "+3V3")
        d.gnd(mux[7][0], mux[7][1])

        for k in range(8):
            x, y = mux[f"D{k}"]
            d.net(x - 4, y, f"RDM{8 * m + k + 1}", anchor="end",
                  size=7.8)

        for ch, name in ((10, "MUX_A"), (9, "MUX_B"), (8, "MUX_C")):
            x, y = mux[ch]
            d.net(x - 4, y, name, anchor="end", size=7.8)

        x, y = mux[5]
        d.net(x + 4, y, f"MUX_Y{m + 1}", size=8.5)

        x, y = mux[15]
        d.nc(x + 4, y, 5)

        x, y = mux[6]
        d.wire((x, y), (x + 18, y))
        d.gnd(x + 18, y)

    # --------------------------------------------------------------
    #  pull-up networks for the RDM returns
    # --------------------------------------------------------------
    rnet(d, 120, 860, "RN1", [f"RDM{i}" for i in range(1, 9)])
    rnet(d, 120, 1120, "RN2", [f"RDM{i}" for i in range(9, 17)])

    note_box(d, 350, 785, 170, 300, "I/O-Expander + RDM-Mux", [
        "PCF8574 (0x20):",
        "P0 = DIR_OUT",
        "P1..P3 = MUX A/B/C",
        "P4..P7 = UP/DOWN/OK/BACK",
        "",
        "P0..P3: 10 kΩ nach +3V3",
        "(RN3), dann 74HC125,",
        "/OE = GND.",
        "",
        "U4 (74HC151 #1):",
        "D0..D7 = RDM1..8",
        "Y1 -> GP22",
        "",
        "U5 (74HC151 #2):",
        "D0..D7 = RDM9..16",
        "Y2 -> GP28",
        "",
        "RN1/RN2: 100 kΩ nach",
        "+3V3 halten fehlende",
        "Rückleitungen hoch.",
        "100 nF (C2..C5) an U2..U5.",
    ], size=7.2, line_gap=11.2)

    # --------------------------------------------------------------
    #  connectors
    # --------------------------------------------------------------
    j1_rows = [
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
    conn(d, 1500, 140, "J1", "Eingangsplatine", j1_rows, pitch=18)

    out_conn = [
        ("J2", 1320, 520, 1, 4, 1),
        ("J3", 1720, 520, 5, 8, 5),
        ("J4", 1320, 880, 9, 12, 9),
        ("J5", 1720, 880, 13, 16, 13),
    ]
    for ref, x, y, g0, g1, r0 in out_conn:
        rows = [
            (1, "+3V3", "pwr"), (2, "+3V3", "pwr"),
            (3, "+5V", "pwr"), (4, "+5V", "pwr"),
            (5, "GND", "gnd"), (6, "GND", "gnd"),
            (7, f"OUT{g0}", "sig"), (8, f"OUT{g0 + 1}", "sig"),
            (9, f"OUT{g0 + 2}", "sig"), (10, f"OUT{g1}", "sig"),
            (11, "DIR_OUT", "sig"),
            (12, f"RDM{r0}", "sig"), (13, f"RDM{r0 + 1}", "sig"),
            (14, f"RDM{r0 + 2}", "sig"), (15, f"RDM{r0 + 3}", "sig"),
            (16, "GND", "gnd"),
        ]
        conn(d, x, y, ref, "Ausgangsplatine", rows, pitch=18)

    # --------------------------------------------------------------
    #  optional UI connectors
    # --------------------------------------------------------------
    ui_rows = [(1, "GND", "gnd"), (2, "+3V3", "pwr"),
               (3, "SDA", "sig"), (4, "SCL", "sig")]
    conn(d, 1060, 840, "J6", "OLED (optional)", ui_rows, pitch=22,
         width=84)

    key_rows = [(1, "BTN_UP", "sig"), (2, "BTN_DOWN", "sig"),
                (3, "BTN_OK", "sig"), (4, "BTN_BACK", "sig"),
                (5, "GND", "gnd")]
    conn(d, 1060, 1020, "J7", "Tasten (optional)", key_rows, pitch=22,
         width=84)

    # --------------------------------------------------------------
    #  notes
    # --------------------------------------------------------------
    note_box(d, 1180, 1225, 660, 105, "Hinweise", [
        "GP23/24/25/29 sind beim Pico nicht herausgeführt; DIR_OUT und MUX_SEL liegen deshalb am PCF8574.",
        "I²C1: GP26 = SDA, GP27 = SCL, 400 kHz; Adressen 0x20 (PCF8574) und 0x3C/0x3D (OLED).",
        "5 V / 3 A einspeisen; 3,3 V kommen aus dem Pico (max. ca. 300 mA). USB kann gesteckt bleiben.",
        "Alle GND-Pins des Pico und aller Steckverbinder sind miteinander verbunden.",
    ], size=7.8, line_gap=13)

    # connector description
    d.text(1180, 500, "J2..J5: OUT1..16, DIR_OUT, RDM1..16", size=8,
           cls="note")
    d.text(1180, 514, "J1: gemeinsame TXD_IN/DIR_IN-Leitungen", size=8,
           cls="note")

    d.save(path, W, H)
