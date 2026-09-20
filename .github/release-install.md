## Installation

All three controllers communicate with your PC over USB with **no drivers to install**:

- **miniMQ Fader** and **miniMQ Encoder** present themselves as a USB keyboard (HID).
- **miniMQ ArtNet-USB** presents itself as a USB Ethernet adapter (NCM) and drives up to four Art-Net ↔ DMX universes.

### Flashing a Raspberry Pi Pico (any variant, first time)

1. Hold the **BOOTSEL** button on the Pico while plugging it into USB, then release.
2. A mass-storage drive named `RPI-RP2` appears.
3. Drag the matching `.uf2` file onto the drive. The Pico reboots and runs the firmware.
4. Later updates can be flashed directly over USB (e.g. `pio run -t upload` from the project folder).

If you ever need to recover the factory state, hold BOOTSEL again and re-flash the `.uf2`.

---

### 1. Fader — `miniMQ-fader.uf2`

DIY fader wing compatible with the MagicQ Compact layouts: 10 faders + grand master (+ 3×13 key grid in the full Mini Connect layout).

1. Flash `miniMQ-fader.uf2` to the wing's Pico.
2. In MagicQ: **Setup → View Settings → Keypad Encoders**.
3. Set **MagicQ PC Keyboard Mode** to **Playback shortcuts**.
4. Use the **US keyboard layout** on the operating system so special characters are typed correctly.

Notes:

- Flash keys (F1–F10) send MagicQ **Test keys** so they work on Windows/Linux/Mac as momentary flashes.
- macOS: if the Mac's physical Shift key occasionally drops out while the Pico is connected, unplug the Pico while typing only on the Mac keyboard (known macOS multi-HID behavior).
- A CircuitPython variant of the wing firmware is not included in this repository — use the compiled `.uf2`.

### 2. Encoder — `miniMQ-encoder.uf2`

8 rotary encoders (with click) + F5–F8, Shift, Group and FX keys for MagicQ virtual encoders and windows.

1. Flash `miniMQ-encoder.uf2` to the encoder box's Pico.
2. In MagicQ: **Setup → View Settings → Keypad Encoders**.
3. Set **MagicQ PC Keyboard Mode** to **Programming Shortcuts**.
4. Assign encoder numbers `1`–`8` to the desired windows/soft buttons.

The F5–F8 keys open INT/POS/COL/BEAM (`Ctrl+I/P/K/J`), Group opens the Group window, FX the FX window, and holding Shift gives 0.1 % ultra-fine adjustment.

### 3. ArtNet-USB — `miniMQ-artnet.uf2`

4-port Art-Net ↔ DMX bridge. The Pico appears to the lighting PC as a USB Ethernet adapter.

1. Flash `miniMQ-artnet.uf2` to the node's Pico.
2. Plug it into the lighting PC with a USB cable. A "USB Ethernet / NCM" adapter appears — **nothing to configure**: the node runs a DHCP server and assigns the adapter an address in `10.0.0.1–10.0.0.9` (leave the adapter on "Obtain an IP address automatically"). The node itself is at **10.0.0.10**.
3. Config web page: open **http://10.0.0.10/** (per-port direction output/input, IP/netmask/DHCP, Art-Net net/subnet/universe).
4. In MagicQ: add an Art-Net output pointing at **10.0.0.10**. The node answers ArtPoll, so MagicQ auto-discovers all four DMX ports.

Defaults: all four ports output, universes `0/0/0`–`0/0/3` (net/sub/universe) on DMX GPIO2–GPIO5.

---

Verify the download integrity with `SHA256SUMS`. A combined archive of all firmwares is in `miniMQ-all-firmwares.zip`. Wiring, setup details and MagicQ mapping tables are in each project's README (`Fader/`, `Enncoder/`, `ArtNet-USB/`).