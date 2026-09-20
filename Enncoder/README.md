# ChamSys MagicQ Custom Controller (Raspberry Pi Pico)

A custom hardware controller for controlling the virtual encoders and windows in **ChamSys MagicQ**, based on a **Raspberry Pi Pico** (RP2040).

The Pico emulates a native keyboard (HID) over USB, so no additional drivers need to be installed.

---

## Table of contents

- [How it works](#how-it-works)
- [MagicQ software setup](#magicq-software-setup)
- [Pin assignments](#pin-assignments)
- [Connection diagrams (ASCII)](#connection-diagrams-ascii)
  - [Rotary encoder (CLK / DT)](#rotary-encoder-clk--dt)
  - [MCP23017 I/O expander for all keys](#mcp23017-io-expander-for-all-keys)
- [Project structure](#project-structure)
- [Build & upload (PlatformIO)](#build--upload-platformio)
- [Bill of materials (BOM)](#bill-of-materials-bom)
- [Notes & limitations](#notes--limitations)

---

## How it works

The project implements **8 rotary encoders** with click function as well as **7 additional keys**.
The 8 rotary encoders (CLK/DT) and the **F keys, Shift, Group and FX** are connected **directly to the GPIOs
of the Pico**. Only the **8 encoder SW** buttons sit on an **MCP23017 I/O expander** (I²C).

All buttons and switches switch directly against **GND** (pull-up resistors are enabled).

| Control | Action | Keyboard character sent |
|---|---|---|
| Encoder **right** | Value + | `<nr>+` (e.g. `1+`) |
| Encoder **left** | Value – | `<nr>-` (e.g. `1-`) |
| Encoder **click** (SW) | Soft button | `<nr>` (e.g. `1`) |
| **Shift** (method A) | Ultra fine adjustment (0.1% steps) | `KEY_LEFT_SHIFT` (held) |
| **Intensity (F5)** | Open INT window | `Ctrl+I` |
| **Position (F6)** | Open POS window | `Ctrl+P` |
| **Colour (F7)** | Open COL window | `Ctrl+K` |
| **Beam (F8)** | Open BEAM window | `Ctrl+J` |
| **Group** | Open Group window | `Ctrl+G` |
| **FX** | Open FX window | `Ctrl+F` |

> **Note:** The attribute keys (F5–F8), Group and FX send **Ctrl combinations** (`Ctrl+I`,
> `Ctrl+P`, `Ctrl+K`, `Ctrl+J`, `Ctrl+G`, `Ctrl+F`) and thus open the corresponding
> MagicQ windows directly. The **Shift key** holds `KEY_LEFT_SHIFT` as a modifier for ultra fine adjustment.

---

## MagicQ software setup

So that the Pico passes the keyboard commands to the software correctly, MagicQ must be put into the right mode:

1. Navigate to: **Setup > View Settings > Keypad Encoders**.
2. Set the option **MagicQ PC Keyboard Mode** to **Programming Shortcuts**.

Afterwards the encoder numbers (`1`–`8`) can be assigned directly to the desired windows or soft buttons.

---

## Pin assignments

The **8 rotary encoders (CLK/DT)** and the **F keys, Shift, Group and FX** are connected directly to the
GPIOs of the Pico. The **MCP23017** (I²C, SDA = GP16, SCL = GP17) only handles the **encoder SW**.

### Encoder direct connection (Pico GPIO)

| Component | CLK / DT |
|---|---|
| **Encoder 1** | GP0 / GP1 |
| **Encoder 2** | GP2 / GP3 |
| **Encoder 3** | GP4 / GP5 |
| **Encoder 4** | GP6 / GP7 |
| **Encoder 5** | GP8 / GP9 |
| **Encoder 6** | GP10 / GP11 |
| **Encoder 7** | GP12 / GP13 |
| **Encoder 8** | GP14 / GP15 |

### Direct keys on the Pico (GPIO, against GND)

| Function | Pico pin | Shortcut |
|---|---|---|
| **Intensity (F5)** | GP18 | `Ctrl+I` |
| **Position (F6)** | GP19 | `Ctrl+P` |
| **Colour (F7)** | GP20 | `Ctrl+K` |
| **Beam (F8)** | GP21 | `Ctrl+J` |
| **Shift** (method A) | GP22 | held `KEY_LEFT_SHIFT` |
| **Group** | GP26 | `Ctrl+G` |
| **FX** | GP27 | `Ctrl+F` |

### Keys on the MCP23017 (I/O expander, address `0x20`)

The MCP23017 is connected to the Pico via I²C (address `0x20`). It only takes the
**encoder SW** (8 of 16 pins); all switch against GND (pull-up in the MCP enabled).

| Expander pin | Function | Shortcut |
|---|---|---|
| **GPA0** (pin 21) | Encoder 1 SW | `1` |
| **GPA1** (pin 22) | Encoder 2 SW | `2` |
| **GPA2** (pin 23) | Encoder 3 SW | `3` |
| **GPA3** (pin 24) | Encoder 4 SW | `4` |
| **GPA4** (pin 25) | Encoder 5 SW | `5` |
| **GPA5** (pin 26) | Encoder 6 SW | `6` |
| **GPA6** (pin 27) | Encoder 7 SW | `7` |
| **GPA7** (pin 28) | Encoder 8 SW | `8` |
| GPB0–GPB7 | free | – |

> The MCP23017 is configured with pull-ups; each key switches an expander pin against **GND**.

---

## Connection diagrams (ASCII)

### Rotary encoder (CLK / DT)

Incremental rotary encoder with integrated push button (SW). **CLK/DT** go directly to the Pico,
**SW** (common) to the MCP23017:

```
        Rotary encoder (e.g. EC11)
        ┌─────────────────────┐
        │       ◯ (shaft)     │
        │                     │
        │  SW   C   A   B     │
        └──┬────┬───┬───┬─────┘
           │    │   │   │
           │    │   │   └─────► Pico GPIO (DT)  e.g. GP0
           │    │   │
           │    │   └─────────► Pico GPIO (CLK) e.g. GP1
           │    │
           │    └─────────────► MCP23017 GPA0 (Encoder 1 SW)
           │
         ┌─┴─┐
         │GND│◄──────────────── GND
         └───┘

   CLK/DT → Pico GPIO (direct)
   SW     → MCP23017 I/O pin (against GND, pull-up in the expander)
```

> For encoders with a common connection (C), connect **C to GND**; A/B (CLK/DT) to the Pico GPIOs,
> the push button (SW) is internally switched against C and goes to the MCP23017.

### MCP23017 I/O expander (encoder SW only)

Connection Pico ↔ MCP23017 (I²C, address 0x20):

```
  Raspberry Pi Pico                MCP23017 (DIP-28)
  ─────────────────               ─────────────────
       3V3 ─────────────────────► VDD (pin 9)
       GND ─────────────────────► VSS (pin 10)
       GND ─────────────────────► A0 (pin 15)      (address 0x20)
       GND ─────────────────────► A1 (pin 16)
       GND ─────────────────────► A2 (pin 17)
       GP16 (SDA) ──────────────► SDA (pin 13)     [via 2.2kΩ → VDD optional]
       GP17 (SCL) ──────────────► SCL (pin 12)     [via 2.2kΩ → VDD optional]
       GND ─────────────────────► RESET (pin 18)   [pull-up → VDD recommended]


  Keys on the MCP23017 (switch against GND):
   Encoder-SW 1..8  → GPA0..GPA7  (pins 21..28)

   Button                    MCP23017 (INPUT_PULLUP)
   ──────                    ─────────────────────
    ──║── (momentary)        ──► e.g. GPA0
      │                       │
     GND───────────────────-──┘
                            Pin = LOW  → pressed
```

### Direct keys on the Pico (F5–F8, Shift, Group, FX)

Applies to all keys connected directly to the Pico – all switch against GND:
(F5=GP18, F6=GP19, F7=GP20, F8=GP21, Shift=GP22, Group=GP26, FX=GP27)

```
         Button (momentary, normally open)
         ──║──            (or toggle switch for Shift)
           │
           ├──────────────► Pico GPIO  (e.g. GP18 for F5)
           │
        ┌──┴──┐
        │ GND │◄───────────── GND
        └─────┘

   Software: pinMode(gpio, INPUT_PULLUP)
   GPIO = LOW  → key pressed
   GPIO = HIGH → key released
```

---

## Project structure

```
chamsys-encoder/
├── chamsys-encoder.pdf   # Original documentation (source code + pin info)
├── platformio.ini        # PlatformIO configuration
├── README.md             # This file
└── src/
    └── main.cpp          # Firmware for the Raspberry Pi Pico
```

---

## Build & upload (PlatformIO)

Prerequisite: [PlatformIO Core](https://platformio.org/) installed.

```bash
# Switch to the project directory
cd chamsys-encoder

# Compile the firmware
pio run

# Compile + upload to the Pico (hold the BOOTSEL button before plugging in)
pio run -t upload

# Serial console (debug output)
pio device monitor
```

**Important:** For upload with the earlephilhower core, the Pico normally only needs to be put
into bootloader mode via **BOOTSEL** (hold, plug in USB, release) the first time.
Later uploads work directly over USB.

**Note on libraries:** The USB keyboard (`Keyboard`) comes from the earlephilhower core itself.
The `lib_ldf_mode = chain+` in `platformio.ini` ensures that the required core library
`tusb-hid` is built automatically. **No** external TinyUSB package is needed.

---

## Bill of materials (BOM)

| Qty | Component | Note |
|---|---|---|
| 1 | Raspberry Pi Pico (RP2040) | with Micro-USB |
| 1 | MCP23017 I/O expander (DIP-28) | only for the 8 encoder SW |
| 8 | Mech. rotary encoders with push button (e.g. EC11) | 20 detents, 3-pin variant |
| 4 | Buttons (momentary) | F5–F8 (directly on the Pico) |
| 1 | Button or toggle switch | Shift (directly on the Pico, GP22) |
| 2 | Buttons (momentary) | Group, FX (directly on the Pico) |
| 2 | Resistors 2.2kΩ (optional) | pull-up for SDA/SCL |
| 1 | Resistor 10kΩ (recommended) | pull-up for the MCP23017 RESET |
| - | hook-up wires / stranded wire | for wiring |
| - | optional: pin headers / breadboards | |

> **Pico pin usage (25 of 27 GPIOs):**
> - **GP0–GP15** = 8 encoders (CLK/DT)
> - **GP16/GP17** = I²C for the MCP23017
> - **GP18–GP21** = F5–F8
> - **GP22** = Shift
> - **GP26** = Group, **GP27** = FX
>
> The **MCP23017** only takes the **8 encoder SW**. **GP28** remains as a free reserve.

---

## Notes & limitations

- **Almost everything directly on the Pico:** The **F keys (F5–F8, GP18–GP21), Shift (GP22), Group (GP26) and FX
  (GP27)** are connected **directly to the Pico**. Only the **8 encoder SW** sit on the single **MCP23017**,
  so that all 8 encoders including the click function fit.
- **Reserve pin:** **GP28** stays free – e.g. another key (e.g. Layout 1)
  could be connected there.
- **Layout keys (Lay 1–3):** These were removed. If desired, they can easily be connected to
  GP28 or to the free MCP23017 pins (GPB0–GPB7) and added in the code.
- **Ctrl combinations:** F5–F8 (INT/POS/COL/BEAM), Group and FX send `Ctrl+<key>` and thus open
  the respective MagicQ windows directly.
- **USB-HID:** The Pico appears to the PC as a keyboard. Key presses are only sent
  after the USB-HID device has been mounted.
- **I²C address:** The MCP23017 is set to address `0x20` with A0/A1/A2 = GND (standard).
- The **serial output (115200)** is only for debugging and is not required for operation.