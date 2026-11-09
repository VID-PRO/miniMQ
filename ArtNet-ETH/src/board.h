#pragma once

#include <Arduino.h>

// ==================================================================
//  ArtNet-ETH board map
//
//  U5  WIZnet W5100S-EVB-Pico (RP2040 + W5100S Ethernet + RJ45)
//  U11..U14  MAX485 half-duplex RS485 transceivers (the schematic label
//            says MAX3485EPA, but the fitted part is the 5 V MAX485).
//            VCC = 5 V in schematic revision V1.0, which is correct for
//            the MAX485; RO then needs level shifting (see below).
//  U1..U4    CT3-12AFAP-EP 3-pin XLR female sockets (output gender);
//            input mode needs a male-male adapter
//  OLED      128x64 SSD1306 on I2C1
//  KEYS      four buttons to GND on a 1x6 header
//
//  Pin map verified against the EasyEDA netlist
//  (hardware/ProPrj_miniQ_2026-11-08.epro2).
//
//  IMPORTANT: on the W5100S-EVB-Pico GP16..GP21 are hard-wired to the
//  on-board W5100S (SPI0 MISO/CS/SCK/MOSI + RST/INT).  Never use them
//  for keys or anything else.
// ==================================================================

// Network transport.  Both the on-board W5100S and USB CDC-NCM are
// compiled into the same firmware.  At boot the node uses USB NCM when a
// USB host enumerates the composite device, and falls back to the W5100S
// when USB is only supplying power (no data host).
namespace board {

constexpr uint8_t NUM_PORTS = 4;

// How long to wait at boot for a USB data host to enumerate the
// CDC+NCM composite device before falling back to the W5100S/RJ45 port.
constexpr uint32_t USB_DETECT_TIMEOUT_MS = 2000;

// --- DMX / RDM ports ----------------------------------------------
// DI   -> transceiver driver input       (PIO TX, one SM per port)
// RO   -> transceiver receiver output    (PIO RX, one SM per port)
// DE/RE-> direction, HIGH = transmit, LOW = receive
//
// The RO lines are powered from the transceiver VCC (5 V in V1.0) and
// reach the RP2040 through 10 k series resistors R40..R43.  Those limit
// clamp current but are not a proper level shifter; use a level shifter
// on RO, or supply the transceiver from 3.3 V.
constexpr uint8_t DMX_TX_PINS[NUM_PORTS]  = {2, 3, 4, 5};
constexpr uint8_t DMX_RX_PINS[NUM_PORTS]  = {10, 11, 12, 13};
constexpr uint8_t DMX_DIR_PINS[NUM_PORTS] = {6, 7, 8, 9};

// --- Local UI -----------------------------------------------------
// SSD1306 on I2C1 (GP26 = SDA, GP27 = SCL).  Wire1 is I2C1 on the
// Arduino-Pico core.  OLED header: 1 = 3.3 V, 2 = GND, 3 = SCL,
// 4 = SDA.
constexpr uint8_t UI_SDA      = 26;
constexpr uint8_t UI_SCL      = 27;
constexpr uint8_t UI_OLED_ADDR = 0x3C;

// Four keys to GND.  Internal pull-ups are enabled, so a pressed key
// reads LOW.  These are the free pins on the W5100S-EVB-Pico; GP16..GP21
// are left unconnected on the carrier board because they go to the
// on-board W5100S.
//
// KEYS header (1x6): 1 = GP28, 2 = GP22, 3 = GP14, 4 = GP15, 5 = NC,
// 6 = GND.  Swap the constants below if the keypad is wired in a
// different order.
constexpr uint8_t KEY_UP   = 14;
constexpr uint8_t KEY_DOWN = 15;
constexpr uint8_t KEY_OK   = 22;
constexpr uint8_t KEY_BACK = 28;

constexpr uint8_t STATUS_LED = PIN_LED;   // GP25

// --- On-board W5100S Ethernet -------------------------------------
// These pins are hard-wired on the W5100S-EVB-Pico.  SPI0 uses
// GP16 (MISO), GP18 (SCK), GP19 (MOSI); GP17 is chip select, GP20 is
// the W5100S reset and GP21 its interrupt line.
constexpr uint8_t ETH_CS  = 17;
constexpr uint8_t ETH_RST = 20;
constexpr uint8_t ETH_IRQ = 21;

// --- USB CDC-NCM transport ----------------------------------------
// Used when a USB data host is detected at boot.  The MAC is fixed
// because it must match the NCM descriptor registered by the vendored
// lwIP_USB_NCM library; the host sees the same address with the last
// byte inverted.  The W5100S pins above are unused in this mode.
constexpr uint8_t NCM_MAC[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};

// How long a DHCP lease is awaited before falling back to the static
// address from the configuration (W5100S/RJ45 transport only).
constexpr uint32_t DHCP_TIMEOUT_MS = 8000;

// --- Protocol constants -------------------------------------------
constexpr uint16_t DMX_SLOTS = 512;       // without start code
constexpr uint16_t ARTNET_UDP_PORT = 6454;

// An input port without a new frame for this long counts as idle.
constexpr uint32_t INPUT_TIMEOUT_MS = 1000;

}  // namespace board
