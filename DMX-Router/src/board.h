#pragma once

#include <Arduino.h>

// ==================================================================
//  Pin map of the DMX-Router (RP2040 / Raspberry Pi Pico)
// ==================================================================
//
//  The RP2040 has 30 GPIOs.  This map fits the complete router into
//  them without port expanders:
//
//    - 16 outputs are driven as two contiguous 8-pin PIO groups
//      (one state machine + one DMA channel per group).
//    - The four input-card TXD lines are tied together to one UART TX
//      pin.  RDM responses are only sent to the console that asked;
//      the other consoles simply ignore a response for a foreign UID.
//    - DIR_IN is common to all four input ports (see README).
//    - The two 74HC151 outputs share three select lines and are read
//      by two PIO receivers.
//    - The local UI (SSD1306 + PCF8574) uses the I2C bus; the four
//      buttons live on the PCF8574, so no GPIO is wasted.
//
//  Everything is still configurable through the USB serial console if
//  the OLED/key board is not populated.
// ------------------------------------------------------------------

namespace board {

// --- DMX output ports 1..16 ---------------------------------------
// Two groups of eight *consecutive* GPIOs. PIO `out pins, 8` can only
// drive a contiguous range, so do not reorder these without also
// changing DmxTxGroup::begin().
constexpr uint8_t TX_GROUP0_PIN = 0;   // OUT 1..8  -> GP0..GP7
constexpr uint8_t TX_GROUP1_PIN = 8;   // OUT 9..16 -> GP8..GP15

// --- DMX input ports 1..4 -----------------------------------------
constexpr uint8_t RX_IN[4] = {16, 17, 18, 19};

// --- Input-card direction / RDM response --------------------------
// TX_IN is UART1 TX and is wired to the TXD pin of all four input
// transceivers.  DIR_IN is a plain GPIO that drives the common
// DE/RE pair: low = receive (console -> router), high = transmit
// (router -> console RDM response).
constexpr uint8_t TX_IN  = 20;
constexpr uint8_t DIR_IN = 21;

// --- Output-card RDM return multiplexer (2x 74HC151) -------------
constexpr uint8_t MUX_SEL[3] = {22, 23, 24};  // A, B, C (shared)
constexpr uint8_t MUX_Y[2]   = {26, 27};      // Y1 = OUT 1..8, Y2 = OUT 9..16

// --- Common output direction -------------------------------------
// GP25 also carries the on-board LED of a Pico.  Using the direction
// signal here keeps the LED mostly steady (on = DMX transmit) and
// avoids putting a switching DMX line on the LED pin.
constexpr uint8_t DIR_OUT = 25;  // high = transmit DMX, low = receive RDM

// --- Local UI (optional hardware) --------------------------------
constexpr uint8_t UI_SDA = 28;
constexpr uint8_t UI_SCL = 29;
constexpr uint8_t UI_OLED_ADDR = 0x3C;  // 0x3D is probed as a fallback
constexpr uint8_t UI_PCF_ADDR  = 0x20;  // 0x20..0x27 are probed

// --- Fixed protocol constants ------------------------------------
constexpr uint8_t  NUM_OUTPUTS = 16;
constexpr uint8_t  NUM_INPUTS  = 4;
constexpr uint16_t DMX_SLOTS   = 512;        // without start code
constexpr uint16_t DMX_FRAME   = DMX_SLOTS + 1;  // start code + slots

constexpr uint8_t RDM_START_CODE     = 0xCC;
constexpr uint8_t RDM_SUB_START_CODE = 0x01;

// Milliseconds without a new input frame before an input is considered
// absent for HTP merging.
constexpr uint32_t INPUT_TIMEOUT_MS = 1000;

}  // namespace board
