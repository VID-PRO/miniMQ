#pragma once

#include <Arduino.h>

// ==================================================================
//  ArtNet-ETH board map
//
//  U5  WIZnet W5100S-EVB-Pico (RP2040 + W5100S Ethernet + RJ45)
//  U11..U14  MAX3485ESA half-duplex RS485 transceivers
//  U1..U4    CT3-12AFAP-EP 3-pin XLR female outputs
//  OLED      128x64 SSD1306 on I2C1
//  KEYS      four buttons to GND
//
//  IMPORTANT: on the W5100S-EVB-Pico GP16..GP21 are hard-wired to the
//  on-board W5100S (SPI0 MISO/CS/SCK/MOSI + RST/INT).  Never use them
//  for keys or anything else.
// ==================================================================

namespace board {

constexpr uint8_t NUM_PORTS = 4;

// --- DMX / RDM ports ----------------------------------------------
// DI   -> transceiver driver input       (PIO TX, one SM per port)
// RO   -> transceiver receiver output    (PIO RX, one SM per port)
// DE/RE-> direction, HIGH = transmit, LOW = receive
//
// The RO lines carry 5 V from a MAX485-style part through a 10 k
// series resistor.  With a real MAX3485 (3.3 V) the resistor is still
// harmless.
constexpr uint8_t DMX_TX_PINS[NUM_PORTS]  = {2, 3, 4, 5};
constexpr uint8_t DMX_RX_PINS[NUM_PORTS]  = {10, 11, 12, 13};
constexpr uint8_t DMX_DIR_PINS[NUM_PORTS] = {6, 7, 8, 9};

// --- Local UI -----------------------------------------------------
// SSD1306 on I2C1 (GP26 = SDA, GP27 = SCL).  Wire1 is I2C1 on the
// Arduino-Pico core.
constexpr uint8_t UI_SDA      = 26;
constexpr uint8_t UI_SCL      = 27;
constexpr uint8_t UI_OLED_ADDR = 0x3C;

// Four keys to GND.  Internal pull-ups are enabled, so a pressed key
// reads LOW.  These are the free pins on the W5100S-EVB-Pico; the
// "KEYS" header in the first schematic revision was wired to GP17..21
// and must be moved here.
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

// How long a DHCP lease is awaited before falling back to the static
// address from the configuration.
constexpr uint32_t DHCP_TIMEOUT_MS = 8000;

// --- Protocol constants -------------------------------------------
constexpr uint16_t DMX_SLOTS = 512;       // without start code
constexpr uint16_t ARTNET_UDP_PORT = 6454;

// An input port without a new frame for this long counts as idle.
constexpr uint32_t INPUT_TIMEOUT_MS = 1000;

}  // namespace board
