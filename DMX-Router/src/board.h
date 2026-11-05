#pragma once

#include <Arduino.h>

// ==================================================================
//  Pin map of the DMX-Router (RP2040 / Raspberry Pi Pico)
// ==================================================================
//
//  The RP2040 has 30 GPIOs, but a standard Pico only brings out
//  GP0..GP22 and GP26..GP28 (26 usable pins).  GP23/GP25 are test
//  pads only; GP24 (VBUS sense) and GP29 (VSYS sense) are not
//  available.  The fast signals therefore stay on the Pico and the
//  slow control and UI signals move to a PCF8574:
//
//    - 16 outputs are driven as two contiguous 8-pin PIO groups
//      (one state machine + one DMA channel per group).
//    - The four input-card TXD lines are tied together to one UART TX
//      pin.  RDM responses are only sent to the console that asked;
//      the other consoles simply ignore a response for a foreign UID.
//    - DIR_IN is common to all four input ports (see README).
//    - The two 74HC151 outputs are read by two PIO receivers; their
//      select lines and the common output direction come from the
//      PCF8574 and are buffered by a 74HC125.
//    - PCF8574 P4..P7 read the four buttons; the OLED is optional.
//
//  The PCF8574 is mandatory: without it DIR_OUT and the mux select
//  lines cannot be controlled.  RN3 pulls them high, so the outputs
//  keep transmitting but the RDM return path sticks on channel 7.
//  Configuration is always available through the USB serial console.
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
// Only the two multiplexer outputs need a PIO receiver.  A standard
// Raspberry Pi Pico does not bring out GP23..GP25 or GP29 on its
// headers, so the three select lines and the common output direction
// are driven through a PCF8574 I/O expander (see below).
constexpr uint8_t MUX_Y[2] = {22, 28};  // Y1 = OUT 1..8, Y2 = OUT 9..16

// --- PCF8574 I/O expander / local UI ------------------------------
// I2C1: GP26 = SDA, GP27 = SCL.  GP28 is free for MUX_Y2.
constexpr uint8_t UI_SDA = 26;
constexpr uint8_t UI_SCL = 27;
constexpr uint8_t UI_OLED_ADDR = 0x3C;  // 0x3D is probed as a fallback
constexpr uint8_t UI_PCF_ADDR  = 0x20;

// PCF8574 pin assignment.  P0..P3 are buffered by a 74HC125 and drive
// the output direction and the shared 74HC151 select lines.  P4..P7
// are inputs with the four UI buttons to GND.
constexpr uint8_t PCF_DIR_OUT  = 0;  // P0: high = transmit, low = receive
constexpr uint8_t PCF_MUX_A    = 1;  // P1: 74HC151 A (LSB)
constexpr uint8_t PCF_MUX_B    = 2;  // P2: 74HC151 B
constexpr uint8_t PCF_MUX_C    = 3;  // P3: 74HC151 C (MSB)
constexpr uint8_t PCF_BTN_UP   = 4;  // P4: UP
constexpr uint8_t PCF_BTN_DOWN = 5;  // P5: DOWN
constexpr uint8_t PCF_BTN_OK   = 6;  // P6: OK
constexpr uint8_t PCF_BTN_BACK = 7;  // P7: BACK

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

// Bitmask of detected output banks (bit 0 -> outputs 1-4 / J2, bit 1 -> 5-8 / J3,
// bit 2 -> 9-12 / J4, bit 3 -> 13-16 / J5).  Populated at boot by board
// detection.  All bits set if detection has not run or failed.
extern uint8_t detected_out_banks;

}  // namespace board
