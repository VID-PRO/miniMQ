# miniMQ — DIY ChamSys MagicQ Control Suite

A collection of DIY hardware and software projects for controlling **ChamSys MagicQ**
stage-lighting software with a **Raspberry Pi Pico**, an Art-Net over USB DMX bridge,
and a dedicated show PC.

## Projects

| Folder | What it does |
|--------|--------------|
| [ArtNet-USB/](ArtNet-USB/) | Raspberry Pi Pico 4-port **Art-Net ↔ DMX512** bridge over USB. The Pico enumerates as a USB Ethernet (CDC-NCM) adapter on the PC, answers ArtPoll, and drives four DMX ports (GPIO 2–5), each configurable as input or output, with RDM gateway and a config web page (http://10.0.0.10/). PlatformIO / Arduino-Pico. |
| [Enncoder/](Enncoder/) | **ChamSys MagicQ encoder controller**: 8 rotary encoders with push switches plus F5–F8, Shift, Group and FX keys, emulated as a USB HID keyboard. Encoder clicks handled via an MCP23017 I²C expander. PlatformIO / Arduino-Pico. |
| [Fader/](Fader/) | **DIY MagicQ Compact Mini Connect Wing**: 11 linear faders (10 playback + grand master) read through a 74HC4067 multiplexer and a 3×13 button matrix (38 keys), all over USB HID. Includes both a latency-optimized C++ firmware and a CircuitPython variant. |
| [Linux-Installer/](Linux-Installer/) | **Show-PC setup for a Dell Wyse 3040**: Ubuntu + Openbox + MagicQ as a dedicated, boot-to-MagicQ lighting console. One script installs all dependencies, autostart, autologin, boot splashscreen, transparent cursor, USB auto-mount and auto-shutdown. |
| [3D/](3D/) | 3D-printable enclosure (`.3mf`) for a MagicQ control wing. |
| [easyEDA/](easyEDA/) | EasyEDA schematic / PCB design documents for the hardware. |