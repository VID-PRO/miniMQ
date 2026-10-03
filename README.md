# MagicQ miniQ Suite

A collection of DIY hardware and software projects for controlling **ChamSys MagicQ**
stage-lighting software with a **Raspberry Pi Pico**, an Art-Net over USB DMX bridge,
and a dedicated show PC.

> **Note:** Every project is independent and has its own `platformio.ini` and handbook.
> The three firmware projects share the same platform (`maxgerhardt/platform-raspberrypi`),
> board (`rpipico`) and core (`earlephilhower`), so one PlatformIO installation and one
> toolchain setup is enough for all of them.

## Table of contents

- [1. The layout (physical arrangement)](#1-the-layout-physical-arrangement)
  - [1.1 Projects](#11-projects)
  - [1.2 Which project fits which goal](#12-which-project-fits-which-goal)
- [2. Hardware overview](#2-hardware-overview)
- [3. Build & upload (PlatformIO)](#3-build-upload-platformio)
- [Project structure](#project-structure)

## 1. The layout (physical arrangement)

The suite consists of three Pico firmwares, one show-PC image and two design folders:

```
  [ Enncoder ]        [ Fader ]            [ ArtNet-USB ]        [ Linux-Installer ]
   8 encoders         11 faders            4x DMX ports          Wyse 3040
   + 7 keys           + 3x13 key grid      Art-Net over USB      Ubuntu + MagicQ
      |                   |                     |                    (boot-to-MagicQ)
      +---------+---------+                     |                        |
                |                               |                        |
        [ magicq wing.3mf ]  <---- 3D enclosure + easyEDA/ design files
```

### 1.1 Projects

| Folder | What it does |
|--------|--------------|
| [ArtNet-USB/](ArtNet-USB/) | Raspberry Pi Pico 4-port **Art-Net ↔ DMX512** bridge over USB. The Pico enumerates as a USB Ethernet (CDC-NCM) adapter on the PC, answers ArtPoll, and drives four DMX ports (GPIO 2–5), each configurable as input or output, with RDM gateway and a config web page (http://10.0.0.10/). PlatformIO / Arduino-Pico. |
| [Enncoder/](Enncoder/) | **ChamSys MagicQ encoder controller**: 8 rotary encoders with push switches plus F5–F8, Shift, Group and FX keys, emulated as a USB HID keyboard. Encoder clicks handled via an MCP23017 I²C expander. PlatformIO / Arduino-Pico. |
| [Fader/](Fader/) | **DIY MagicQ Compact Mini Connect Wing**: 11 linear faders (10 playback + grand master) read through a 74HC4067 multiplexer and a 3×13 button matrix (38 keys), all over USB HID. Includes both a latency-optimized C++ firmware and a CircuitPython variant. |
| [Linux-Installer/](Linux-Installer/) | **Show-PC setup for a Dell Wyse 3040**: Ubuntu + Openbox + MagicQ as a dedicated, boot-to-MagicQ lighting console. One script installs all dependencies, autostart, autologin, boot splashscreen, transparent cursor, USB auto-mount and auto-shutdown. |
| [3D/](3D/) | 3D-printable enclosure (`.3mf`) for a MagicQ control wing. |
| [easyEDA/](easyEDA/) | EasyEDA schematic / PCB design documents for the hardware. |

### 1.2 Which project fits which goal

| Goal | Project |
|------|---------|
| Faders on MagicQ's Full Panel + flash/GO keys | [Fader/](Fader/) |
| Turn / click the MagicQ on-screen encoders | [Enncoder/](Enncoder/) |
| Feed DMX512 fixtures from the PC over USB | [ArtNet-USB/](ArtNet-USB/) |
| Boot a small PC straight into MagicQ | [Linux-Installer/](Linux-Installer/) |
| Print the enclosure / edit the PCB | [3D/](3D/), [easyEDA/](easyEDA/) |

## 2. Hardware overview

| Peripheral | Used by | Connected to |
|------------|---------|--------------|
| 74HC4067 16-channel analog multiplexer | Fader | GP27 (COM) + GP17–GP20 (S0–S3), fader wipers on C0–C10 |
| 3×13 key matrix (dioded) | Fader | rows GP2–GP4, columns GP6–GP16, GP21, GP22 |
| MCP23017 I/O expander (I²C) | Enncoder | GP16 (SDA) / GP17 (SCL), carries the 8 encoder switches |
| MAX485 / MAX3485 transceivers | ArtNet-USB | DMX ports on GPIO 2–5 via PIO |
| Dell Wyse 3040 (eMMC) | Linux-Installer | — (runs MagicQ as show computer) |

## 3. Build & upload (PlatformIO)

Prerequisite: [PlatformIO Core](https://platformio.org/) installed.

```bash
# Switch to the project you want to build
cd Fader          # or Enncoder / ArtNet-USB

# Compile the firmware
pio run

# Compile + upload to the Pico (hold the BOOTSEL button before plugging in)
pio run -t upload

# Serial console (debug output)
pio device monitor
```

**Important:** For upload with the earlephilhower core, the Pico normally only needs to be put
into bootloader mode via **BOOTSEL** (hold, plug in USB, release) the first time. Later uploads
work directly over USB. The resulting file is at `.pio/build/pico/firmware.uf2` and can also be
dragged onto the `RPI-RP2` drive.

> **Note:** `Fader` and `Enncoder` set `board_build.filesystem_size = 64`. That 64 KB **LittleFS**
> partition is what their runtime calibrations write to (`/fcal.bin`, `/ecal.bin`) — without it
> `LittleFS.begin()` fails and nothing is saved. `ArtNet-USB` needs no filesystem.

## Project structure

```
miniMQ/
├── README.md             # This overview
├── LICENSE
├── Makefile
├── platformio.ini        # Workspace-level PlatformIO config
├── ArtNet-USB/           # Art-Net over USB -> 4x DMX512 (Pico firmware)
├── Enncoder/             # 8 encoders + 7 keys -> MagicQ (Pico firmware)
├── Fader/                # 11 faders + 3x13 keys -> MagicQ (Pico firmware + CircuitPython)
├── Linux-Installer/      # Dell Wyse 3040 show-PC image setup
├── 3D/                   # 3D-printable enclosure
├── easyEDA/              # EasyEDA schematic / PCB documents
├── scripts/              # Helper scripts
└── .github/              # CI workflows
```