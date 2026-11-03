#pragma once

#include <Arduino.h>

// ==================================================================
//  PCF8574 I/O expander
// ==================================================================
//
//  A standard Raspberry Pi Pico does not bring out GP23, GP24, GP25 or
//  GP29 on its headers.  The 4->16 router therefore moves the slow
//  control and UI signals onto a PCF8574 on I2C1:
//
//      P0 = DIR_OUT      (buffered by 74HC125)
//      P1 = MUX_SEL A    (buffered by 74HC125)
//      P2 = MUX_SEL B    (buffered by 74HC125)
//      P3 = MUX_SEL C    (buffered by 74HC125)
//      P4 = button UP
//      P5 = button DOWN
//      P6 = button OK
//      P7 = button BACK
//
//  P0..P3 are written as outputs.  P4..P7 are kept at 1 so the pins
//  act as weak-pull-up inputs; the buttons pull them to GND.
// ==================================================================

namespace ioexp {

// Probe and initialise the expander.  Returns false if no PCF8574
// answers.  The caller must have started Wire1 on board::UI_SDA/SCL.
bool begin(uint8_t addr);

// True if the expander answered during begin().
bool present();

// Write the complete output latch.  Bits 4..7 are forced high so the
// button inputs keep their weak pull-ups.
bool write(uint8_t value);

// Read the raw PCF8574 pin register.
uint8_t read();

// P0: output direction.  true = transmit DMX, false = receive RDM.
void setDirOut(bool transmit);

// P1..P3: shared 74HC151 channel 0..7.
void selectMux(uint8_t channel);

// P4..P7 as an active-high pressed mask:
//   bit 0 = UP, bit 1 = DOWN, bit 2 = OK, bit 3 = BACK.
uint8_t buttons();

}  // namespace ioexp
