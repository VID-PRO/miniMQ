# ChamSys MagicQ Custom Controller (Raspberry Pi Pico)

Ein eigener Hardware-Controller zur Steuerung der virtuellen Encoder und Fenster in **ChamSys MagicQ**, basierend auf einem **Raspberry Pi Pico** (RP2040).

Der Pico emuliert über USB eine native Tastatur (HID), wodurch keine Treiber zusätzlich installiert werden müssen.

---

## Inhaltsverzeichnis

- [Funktionsweise](#funktionsweise)
- [MagicQ Software-Einrichtung](#magicq-software-einrichtung)
- [Pin-Belegung](#pin-belegung)
- [Anschluss-Schemata (ASCII)](#anschluss-schemata-ascii)
  - [Drehencoder (CLK / DT)](#drehencoder-clk--dt)
  - [MCP23017 I/O-Expander für alle Tasten](#mcp23017-io-expander-für-alle-tasten)
- [Projektstruktur](#projektstruktur)
- [Build & Upload (PlatformIO)](#build--upload-platformio)
- [Stückliste (BOM)](#stückliste-bom)
- [Hinweise & Einschränkungen](#hinweise--einschränkungen)

---

## Funktionsweise

Das Projekt setzt **8 Drehencoder** mit Klick-Funktion sowie **7 weitere Tasten** um.
Die 8 Drehencoder (CLK/DT) sowie die **F-Tasten, Shift, Group und FX** hängen **direkt an den GPIOs
des Pico**. Nur die **8 Encoder-SW** liegen auf einem **MCP23017 I/O-Expander** (I²C).

Alle Taster und Schalter schalten direkt gegen **GND** (Pullup-Widerstände sind aktiviert).

| Bedienelement | Aktion | Gesendetes Tastaturzeichen |
|---|---|---|
| Encoder **rechts** | Wert + | `<nr>+` (z.B. `1+`) |
| Encoder **links** | Wert – | `<nr>-` (z.B. `1-`) |
| Encoder **Klick** (SW) | Soft-Button | `<nr>` (z.B. `1`) |
| **Shift** (Methode A) | Ultra-Feinjustierung (0,1%-Schritte) | `KEY_LEFT_SHIFT` (gehalten) |
| **Intensity (F5)** | INT-Fenster öffnen | `Ctrl+I` |
| **Position (F6)** | POS-Fenster öffnen | `Ctrl+P` |
| **Colour (F7)** | COL-Fenster öffnen | `Ctrl+K` |
| **Beam (F8)** | BEAM-Fenster öffnen | `Ctrl+J` |
| **Group** | Group-Fenster öffnen | `Ctrl+G` |
| **FX** | FX-Fenster öffnen | `Ctrl+F` |

> **Hinweis:** Die Attribut-Tasten (F5–F8), Group und FX senden **Strg-Kombinationen** (`Ctrl+I`,
> `Ctrl+P`, `Ctrl+K`, `Ctrl+J`, `Ctrl+G`, `Ctrl+F`) und öffnen damit direkt die entsprechenden
> MagicQ-Fenster. Die **Shift-Taste** hält `KEY_LEFT_SHIFT` als Modifier für die Ultra-Feinjustierung.

---

## MagicQ Software-Einrichtung

Damit der Pico die Tastaturbefehle korrekt an die Software übergibt, muss MagicQ in den richtigen Modus versetzt werden:

1. Navigieren zu: **Setup > View Settings > Keypad Encoders**.
2. Option **MagicQ PC Keyboard Mode** auf **Programming Shortcuts** stellen.

Danach können die Encoder-Nummern (`1`–`8`) direkt den gewünschten Fenstern bzw. Soft-Buttons zugeordnet werden.

---

## Pin-Belegung

Die **8 Drehencoder (CLK/DT)** sowie die **F-Tasten, Shift, Group und FX** hängen direkt an den
GPIOs des Pico. Der **MCP23017** (I²C, SDA = GP16, SCL = GP17) übernimmt nur noch die **Encoder-SW**.

### Encoder-Direktverbindung (Pico GPIO)

| Bauteil | CLK / DT |
|---|---|
| **Encoder 1** | GP0 / GP1 |
| **Encoder 2** | GP2 / GP3 |
| **Encoder 3** | GP4 / GP5 |
| **Encoder 4** | GP6 / GP7 |
| **Encoder 5** | GP8 / GP9 |
| **Encoder 6** | GP10 / GP11 |
| **Encoder 7** | GP12 / GP13 |
| **Encoder 8** | GP14 / GP15 |

### Direkte Tasten am Pico (GPIO, gegen GND)

| Funktion | Pico-Pin | Shortcut |
|---|---|---|
| **Intensity (F5)** | GP18 | `Ctrl+I` |
| **Position (F6)** | GP19 | `Ctrl+P` |
| **Colour (F7)** | GP20 | `Ctrl+K` |
| **Beam (F8)** | GP21 | `Ctrl+J` |
| **Shift** (Methode A) | GP22 | gehalten `KEY_LEFT_SHIFT` |
| **Group** | GP26 | `Ctrl+G` |
| **FX** | GP27 | `Ctrl+F` |

### Tasten am MCP23017 (I/O-Expander, Adresse `0x20`)

Der MCP23017 wird über I²C (Adresse `0x20`) mit dem Pico verbunden. Er nimmt nur noch die
**Encoder-SW** (8 von 16 Pins) auf; alle schalten gegen GND (Pullup im MCP aktiviert).

| Expander-Pin | Funktion | Shortcut |
|---|---|---|
| **GPA0** (Pin 21) | Encoder 1 SW | `1` |
| **GPA1** (Pin 22) | Encoder 2 SW | `2` |
| **GPA2** (Pin 23) | Encoder 3 SW | `3` |
| **GPA3** (Pin 24) | Encoder 4 SW | `4` |
| **GPA4** (Pin 25) | Encoder 5 SW | `5` |
| **GPA5** (Pin 26) | Encoder 6 SW | `6` |
| **GPA6** (Pin 27) | Encoder 7 SW | `7` |
| **GPA7** (Pin 28) | Encoder 8 SW | `8` |
| GPB0–GPB7 | frei | – |

> Der MCP23017 wird mit Pullups konfiguriert; jede Taste schaltet einen Expander-Pin gegen **GND**.

---

## Anschluss-Schemata (ASCII)

### Drehencoder (CLK / DT)

Inkremental-Drehencoder mit integriertem Druckknopf (SW). **CLK/DT** gehen direkt an den Pico,
**SW** (Common) an den MCP23017:

```
        Drehencoder (z.B. EC11)
        ┌─────────────────────┐
        │       ◯ (Welle)     │
        │                     │
        │  SW   C   A   B     │
        └──┬────┬───┬───┬─────┘
           │    │   │   │
           │    │   │   └─────► Pico GPIO (DT)  z.B. GP0
           │    │   │
           │    │   └─────────► Pico GPIO (CLK) z.B. GP1
           │    │
           │    └─────────────► MCP23017 GPA0 (Encoder 1 SW)
           │
         ┌─┴─┐
         │GND│◄──────────────── GND
         └───┘

   CLK/DT → Pico GPIO (direkt)
   SW     → MCP23017 I/O-Pin (gegen GND, Pullup im Expander)
```

> Bei Encodern mit Common-Anschluss (C) den **C auf GND** legen; A/B (CLK/DT) auf die Pico-GPIOs,
> der Druckknopf (SW) ist intern gegen C geschaltet und geht an den MCP23017.

### MCP23017 I/O-Expander (nur Encoder-SW)

Verbindung Pico ↔ MCP23017 (I²C, Adresse 0x20):

```
  Raspberry Pi Pico                MCP23017 (DIP-28)
  ─────────────────               ─────────────────
       3V3 ─────────────────────► VDD (Pin 9)
       GND ─────────────────────► VSS (Pin 10)
       GND ─────────────────────► A0 (Pin 15)      (Adresse 0x20)
       GND ─────────────────────► A1 (Pin 16)
       GND ─────────────────────► A2 (Pin 17)
       GP16 (SDA) ──────────────► SDA (Pin 13)     [über 2,2kΩ → VDD optional]
       GP17 (SCL) ──────────────► SCL (Pin 12)     [über 2,2kΩ → VDD optional]
       GND ─────────────────────► RESET (Pin 18)   [Pullup → VDD empfohlen]


  Tasten am MCP23017 (schalten gegen GND):
   Encoder-SW 1..8  → GPA0..GPA7  (Pins 21..28)

   Taster                    MCP23017 (INPUT_PULLUP)
   ──────                    ─────────────────────
    ──║── (momentan)        ──► z.B. GPA0
      │                       │
     GND───────────────────-──┘
                            Pin = LOW  → gedrückt
```

### Direkte Tasten am Pico (F5–F8, Shift, Group, FX)

Gilt für alle direkt am Pico angeschlossenen Tasten – alle schalten gegen GND:
(F5=GP18, F6=GP19, F7=GP20, F8=GP21, Shift=GP22, Group=GP26, FX=GP27)

```
         Taster (momentan, Normal open)
         ──║──            (oder Kippschalter für Shift)
           │
           ├──────────────► Pico GPIO  (z.B. GP18 für F5)
           │
        ┌──┴──┐
        │ GND │◄───────────── GND
        └─────┘

   Software: pinMode(gpio, INPUT_PULLUP)
   GPIO = LOW  → Taste gedrückt
   GPIO = HIGH → Taste losgelassen
```

---

## Projektstruktur

```
chamsys-encoder/
├── chamsys-encoder.pdf   # Original-Dokumentation (Quellcode + Pin-Info)
├── platformio.ini        # PlatformIO-Konfiguration
├── README.md             # Diese Datei
└── src/
    └── main.cpp          # Firmware für den Raspberry Pi Pico
```

---

## Build & Upload (PlatformIO)

Voraussetzung: [PlatformIO Core](https://platformio.org/) installiert.

```bash
# Zum Projektverzeichnis wechseln
cd chamsys-encoder

# Firmware kompilieren
pio run

# Kompilieren + auf den Pico hochladen (BOOTSEL-Halteknopf vor dem Einstecken)
pio run -t upload

# Serielle Konsole (Debug-Ausgaben)
pio device monitor
```

**Wichtig:** Für den Upload mit dem earlephilhower-Core muss der Pico normalerweise nur beim
ersten Mal per **BOOTSEL** (halten, USB einstecken, loslassen) in den Bootloader-Modus versetzt
werden. Spätere Uploads funktionieren direkt über USB.

**Hinweis zu Bibliotheken:** Die USB-Tastatur (`Keyboard`) kommt aus dem earlephilhower-Core selbst.
Der `lib_ldf_mode = chain+` in `platformio.ini` sorgt dafür, dass die benötigte Core-Library
`tusb-hid` automatisch mitgebaut wird. Es wird **kein** externes TinyUSB-Paket benötigt.

---

## Stückliste (BOM)

| Menge | Bauteil | Bemerkung |
|---|---|---|
| 1 | Raspberry Pi Pico (RP2040) | mit Micro-USB |
| 1 | MCP23017 I/O-Expander (DIP-28) | nur für die 8 Encoder-SW |
| 8 | Mech. Drehencoder mit Druckknopf (z.B. EC11) | 20 Detents, 3-Pin Variante |
| 4 | Taster (momentan) | F5–F8 (direkt am Pico) |
| 1 | Taster oder Kippschalter | Shift (direkt am Pico, GP22) |
| 2 | Taster (momentan) | Group, FX (direkt am Pico) |
| 2 | Widerstände 2,2kΩ (optional) | Pullup für SDA/SCL |
| 1 | Widerstand 10kΩ (empfohlen) | Pullup für RESET des MCP23017 |
| - | bedrahtete Kabel / Litzen | für Verdrahtung |
| - | optional: Pinleisten / Steckboards | |

> **Pico-Pin-Verwendung (25 von 27 GPIOs):**
> - **GP0–GP15** = 8 Encoder (CLK/DT)
> - **GP16/GP17** = I²C für MCP23017
> - **GP18–GP21** = F5–F8
> - **GP22** = Shift
> - **GP26** = Group, **GP27** = FX
>
> Der **MCP23017** nimmt nur noch die **8 Encoder-SW** auf. **GP28** bleibt als freie Reserve übrig.

---

## Hinweise & Einschränkungen

- **Fast alles direkt am Pico:** Die **F-Tasten (F5–F8, GP18–GP21), Shift (GP22), Group (GP26) und FX
  (GP27)** hängen **direkt am Pico**. Nur die **8 Encoder-SW** liegen auf dem einzelnen **MCP23017**,
  damit alle 8 Encoder samt Klick-Funktion untergebracht werden können.
- **Reserve-Pin:** **GP28** bleibt frei – dort ließe sich z.B. noch eine weitere Taste (z.B. Layout 1)
  anschließen.
- **Layout-Tasten (Lay 1–3):** Diese wurden entfernt. Falls gewünscht, lassen sie sich problemlos an
  GP28 oder an die freien MCP23017-Pins (GPB0–GPB7) anschließen und im Code ergänzen.
- **Strg-Kombinationen:** F5–F8 (INT/POS/COL/BEAM), Group und FX senden `Ctrl+<Taste>` und öffnen
  damit die jeweiligen MagicQ-Fenster direkt.
- **USB-HID:** Der Pico erscheint am PC als Tastatur. Erst nach dem Mounten des USB-HID-Geräts
  werden Tastendrücke gesendet.
- **I²C-Adresse:** Der MCP23017 wird mit A0/A1/A2 = GND auf Adresse `0x20` gesetzt (Standard).
- Die **Serial-Ausgabe (115200)** dient nur zum Debuggen und ist nicht für den Betrieb erforderlich.
