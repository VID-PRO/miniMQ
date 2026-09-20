 # Raspberry Pi Pico Art-Net over USB (PlatformIO)

4-port Art-Net <-> DMX bridge. The Pico (RP2040) appears to your stage-lighting PC
(MagicQ & co.) as a **USB network adapter**, exchanges Art-Net over that USB link,
and drives four DMX ports on GPIO2, GPIO3, GPIO4 and GPIO5. Each port is
individually configurable as an **output** (ArtDmx -> DMX out) or an **input**
(DMX in -> ArtDmx).

## How it works

- **USB networking**: the RP2040's native USB runs the **CDC-NCM** "Ethernet over USB"
  class via the Arduino-Pico core's `NCMEthernetlwIP` (TinyUSB + lwIP). Modern
  Windows 10/11, macOS and Linux mount an NCM network adapter with **no driver install**.
  The Pico has no Wi-Fi or wired Ethernet of its own — the USB link *is* the network.
- The device gets a **static IP 10.0.0.10** on the USB link by default, and can
  run a **DHCP server** that hands the host an automatic address in
  **10.0.0.1–10.0.0.9** — no manual IP configuration needed on the PC. IP,
  netmask and the DHCP server are configurable on the embedded web page
  (http://10.0.0.10/).
- **DMX**: a custom `DmxOut` library (`lib/DmxOut/`) drives each output with a
  PIO state machine + DMA. Four universes on GPIO 2/3/4/5, one SM per universe on
  `pio0`. It emits spec-compliant framing for strict receivers: BREAK ~185 µs,
  MAB 16 µs, 250 kbaud data bits (the bundled Pico-DMX's 8 µs MAB was rejected by
  some fixtures).

## Project layout

```
platformio.ini      build config (RP2040, Arduino-Pico / earlephilhower core)
lib/DmxOut/          custom DMX512 TX PIO (BREAK ~185 µs, MAB 16 µs) + DMA driver
lib/ArtnetRdm/       RDM gateway: E1.20 protocol, per-port RDM RX (pio1), ArtRdm bridge
lib/ArtnetConfig/    persisted per-port direction + Art-Net address map (EEPROM-backed)
src/main.cpp
    - DMX driver (DmxOut, PIO + DMA)
    - USB CDC-NCM Ethernet (NCMEthernetlwIP + lwIP, static IP 10.0.0.10)
    - DHCP server for the USB link (10.0.0.1-9)
    - Art-Net OpDmx parser over a raw UDP socket (no external Art-Net lib)
    - ArtPoll/ArtPollReply responder -> MagicQ auto-discovers all 4 universes
      (note: MagicQ declares a short ArtDmx length but always sends 512 slots;
      the node uses the real datagram size, so all channels pass through)
    - DMX input ports publish received frames as ArtDmx broadcasts
    - Config web page at http://10.0.0.10/ (WebServer; per-port direction + net/subnet/universe)
```

## Build & flash

```bash
pio run              # build -> .pio/build/pico/firmware.uf2
```

Or just build and copy the UF2 manually:

1. Hold the **BOOTSEL** button on the Pico, plug in USB, and release.
2. A mass-storage drive named `RPI-RP2` appears. Copy
   `.pio/build/pico/firmware.uf2` onto it. The Pico reboots and runs the firmware.

Alternatively with a debug probe / PIO upload:

```bash
pio run -t upload    # needs a UF2/OpenOCD/dbgprobe upload_protocol configured
```

## Wiring

| DMX port | Pico GPIO | MAX485 signal |
|----------|-----------|---------------|
| Universe 1 | GPIO2 (`DI`) / GPIO10 (`RO`, via 10 kΩ) / GPIO6 (`DE`/`RE`) | MAX485 DI / RO / direction |
| Universe 2 | GPIO3 (`DI`) / GPIO11 (`RO`, via 10 kΩ) / GPIO7 (`DE`/`RE`) | MAX485 DI / RO / direction |
| Universe 3 | GPIO4 (`DI`) / GPIO12 (`RO`, via 10 kΩ) / GPIO8 (`DE`/`RE`) | MAX485 DI / RO / direction |
| Universe 4 | GPIO5 (`DI`) / GPIO13 (`RO`, via 10 kΩ) / GPIO9 (`DE`/`RE`) | MAX485 DI / RO / direction |

### Transceivers (MAX485 vs MAX3485)

Each port uses a half-duplex RS485 transceiver:
- `DI` → Pico GPIO (TX): DMX **and** RDM transmit data.
- `RO` → Pico GPIO (RX): RDM receive data; on a port configured as an **input**
  this is the DMX receive path and must be wired (via the 10 kΩ series resistor).
  Not needed on an output-only port.
- `DE`/`RE` → Pico GPIO (direction): driven high to transmit, low to receive.
  These are toggled automatically when RDM is used; on an **input** port `DE` is
  held low permanently so the receiver is always enabled.

#### Auto-direction transceivers ("automatic flow control")

Auto-direction RS485 modules (e.g. MAX13487E-class) derive the direction from the
data line instead of a `DE`/`RE` input, saving those wires. Assessment for this node:

- **DMX output only**: mostly works, but the driver goes high-impedance during
  the idle *mark* (TXD high). DMX has no bias resistors by design, so the ~60 Ω
  combined termination cannot reliably hold a valid mark → some fixtures glitch
  or drop out. If you must use one, add bias resistors (pull-up on A, pull-down
  on B) near the transceiver and re-verify with your fixtures (the node's picky
  4-ch par is a good test case).
- **RDM (all 4 ports here)**: not recommended. RDM needs the bus actively driven
  through BREAK/MAB and the ~11-bit response window, plus a clean bidirectional
  handoff — exactly what a real `DE`/`RE` pin provides. Auto-flow cannot do this
  reliably.

So keep the `DE`/`RE` wiring for all 4 ports if RDM stays enabled; drop the
direction pins only if a port is made DMX-only and you accept the bias caveat.

Two options for the line driver:

- **MAX485 (5 V) — recommended for picky fixtures.** `VCC` = 5 V from VBUS
  (Pico pin 40); bus levels swing to ~5 V differential. Its logic side is TTL,
  so the 3.3 V GPIOs drive `DI`/`DE` directly; `RO` is 5 V into a non-tolerant
  GPIO, handled with a 10 kΩ series resistor (see below).
- **MAX3485 (3.3 V).** Simplest wiring (can run off 3.3 V), but the lower bus
  voltage (~2.5 V) is rejected by some fixtures.

### Pico ↔ MAX485 signal levels

The Pico IOs are 3.3 V and **not 5 V tolerant**. The MAX485's logic side is TTL:

- **`DI` (TX) and `DE`/`RE` (direction)** are driven **directly** by the 3.3 V
  GPIOs — TTL inputs switch at ~2.0 V, so a 3.3 V high is guaranteed valid. No
  level shifting needed.
- **`RO` (RX)** is a push-pull output swinging 0–5 V. Feed it into the GPIO
  through a **10 kΩ series resistor**: the Pico's internal ESD clamp limits the
  line to ~3.6 V while the 10 kΩ keeps the clamp current at ~170 µA (safe), and
  the resulting RC (10 kΩ into the sub-20 pF input) settles in ~200 ns — far
  shorter than the 4 µs RDM bit time, so 250 kbaud reception is unaffected.

(A TXS0108-based alternative was considered for full 5 V→3.3 V level shifting on
all eight signals; the direct `DI`/`DE` + 10 kΩ-on-`RO` wiring above replaces it.)

### Termination

A 120 Ω resistor **across A and B** (differential, not in series with A) at each
port's transceiver — one per port:

```
A (pin7) ─────────────── XLR pin3 (+)
            |
          120 Ω
            |
B (pin6) ─────────────── XLR pin2 (−)
```

If the far-end fixture already terminates (usual), the two in parallel load to
~60 Ω — fine for short runs; drop the node-side one if a fixture misbehaves.

DMX/RDM share the same 2-conductor differential bus over a 5-pin XLR. RDM is
half-duplex at 250 kbaud with a BREAK + MAB frame; the gateway relays Art-RDM
commands from MagicQ to the fixtures and bridges their responses back.

GPIO numbers match the Arduino pin numbers on the plain Pico. RDM RX runs on
`pio1` (one state machine per port); the DMX outputs continue to use `pio0`.

### DMX input mode

A port configured as an **input** disables its DMX transmitter (pico drives `DE`
low) and listens on `RO`. The same `pio1` receiver used for RDM captures the
incoming DMX stream; a small state machine resynchronises on BREAK, checks the
`0x00` start code, collects the 512 slots and broadcasts the frame as ArtDmx on
the port's configured `net`/`subnet`/`universe`. Input ports ignore incoming
ArtDmx and do not bridge RDM — they only publish frames. An input port with
signal within the last second is advertised in ArtPollReply with a DMX-input
PortType, `GoodInput` "data received" and its `SwIn` set, so controllers can
discover it.

## Host setup

1. Connect the Pico to the PC with a USB cable.
2. The PC enumerates a "USB Ethernet / RNDIS/NCM" adapter.
3. **Nothing to configure** — the Pico's DHCP server gives the adapter an address
   in **10.0.0.1–10.0.0.9** automatically (just ensure the adapter is set to
   "Obtain an IP address automatically", the default). The Pico answers on
   **10.0.0.10**.
4. In MagicQ, add an Art-Net output and point it at **10.0.0.10**. The node
   answers ArtPoll, so MagicQ auto-discovers the ports — no manual mapping
   needed (see the config page for remapping universes).

## Config web page

Open **http://10.0.0.10/** in a browser (host is on the USB link, or any
machine that can route to the node's IP). The page follows the classic node
layout with two tabs:

- **Channels** — live view of all 512 DMX channel values of the selected port
  (intensity grid, search, "active only" filter). Polls `/api/status` ~3×/s.
- **Settings** — configure and store (flash/EEPROM):
  - **Network**: static IP, netmask, and an on/off switch for the DHCP server.
  - **Art-Net ports**: for each of the 4 DMX ports choose the **direction**
    (`output` = ArtDmx -> DMX out, `input` = DMX in -> ArtDmx) and set the full
    Art-Net address, `net` (0–127), `subnet` (0–15), `universe` (0–15). The 15-bit
    address a port uses is `net<<8 | subnet<<4 | universe`; it applies to
    ArtDmx data (and ArtRdm traffic for output ports).
  - **Node identity**: **Short name** (max 17 chars) and **Long name** (max 63
    chars) advertised in the ArtPollReply. MagicQ shows them in its output node
    list; the short name also appears in the web header pill. Both are stored in
    flash alongside the network settings.
  - **Save &amp; reboot** applies everything (node reboots, then re-announces
    via ArtPollReply so MagicQ picks up the new mapping).

All settings — direction, per-port address, IP/netmask/DHCP, node names — are
stored in flash (EEPROM, layout v5) and survive power cycles and reboots.

A save triggers a warm reboot. Before resetting, the node intentionally drops
the USB link so the host always sees the device unplug and re-enumerates the
NCM interface; this takes a few seconds. If the config page does not return
immediately, give the host a moment to re-run DHCP, then reopen
`http://<new-ip>/`.

The same API the page uses: `GET /api/status?port=N`, `GET/POST /api/config`,
`POST /api/reboot`, `POST /api/factory`. `/api/config` carries the address
triples as arrays `net`, `subnet`, `universe` plus a `direction` array
(0 = output, 1 = input) and the node names `name`/`longname`; the POST form
uses fields `n0..n3`, `s0..s3`, `u0..u3`, `d0..d3`, `name` and `longname`.
`/api/status` also returns `direction`, a per-port `input_active` flag and the
active `name`.

Defaults: IP `10.0.0.10`, netmask `255.0.0.0`, DHCP **on** (pool
`<ip>.1–.9`), all four ports **output**, addresses `0/0/0`, `0/0/1`, `0/0/2`,
`0/0/3` (net/sub/universe).
If you change the subnet or disable DHCP, either reach the new IP from a
manually-configured host, or hold BOOTSEL and re-flash the factory firmware.

MagicQ mapping with the defaults: MagicQ Uni 1 -> address 0/0/0 -> DMX GPIO2,
Uni 2 -> 0/0/1 -> GPIO3, Uni 3 -> 0/0/2 -> GPIO4, Uni 4 -> 0/0/3 -> GPIO5. The
node advertises `SwOut = subnet<<4|universe` per port (exact for net 0, the
typical setup).

## Notes & hardware verification

- **Serial logs**: with USB used for NCM networking, the standard `Serial` USB-CDC
  and an NCM interface can share the USB port. In the earlephilhower core
  `Serial` (USB CDC) works alongside the NCM class. If you prefer a hardware UART,
  use `Serial1` on the UART0 pins (GPIO0 TX / GPIO1 RX) after `Serial1.begin(...)`.
- The NCM link is confirmed on the Pico side via `eth.linkStatus() == LinkON`
  (i.e. the host has mounted the USB NCM interface). The node only reports
  `READY` once that is true; if the host is slow to enumerate it keeps checking
  and recovers without a re-plug. If `ping 10.0.0.10` fails, first check that
  the host has picked up the NCM adapter and is on the 10.0.0.x subnet.
- **Warm reboot / Save &amp; reboot**: a plain watchdog reset can leave the USB
  pull-up asserted, so the host never notices the disconnect and fails to
  re-enumerate the NCM interface (the page then only works after a cold boot).
  `rebootNode()` therefore calls `USB.disconnect()` (blocking ~500 ms) before
  resetting, and `bringUpNetwork()` waits for the host to re-enumerate.
- **Testing**: to verify without a lighting rig, drive an LED from one universe's
  output, or scope the RS485 TX line — a DMX output idles high after each frame.

## Pin the exact platform/version (recommended)

`platform = https://github.com/maxgerhardt/platform-raspberrypi.git` tracks the
latest. For a reproducible build pin a specific tag, e.g.:

```ini
platform = https://github.com/maxgerhardt/platform-raspberrypi.git#1.10.0
```
