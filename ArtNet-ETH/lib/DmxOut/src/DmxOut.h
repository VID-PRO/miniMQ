/*
 * DmxOut: DMX512 transmitter for the RP2040 PIO.
 *
 * Own PIO timing (see DmxOut.pio): BREAK ~185 us, MAB 16 us, 250 kbaud
 * data bits. Replaces the bundled Pico-DMX DmxOutput so we control the
 * frame timing explicitly.
 */

#ifndef DMX_OUT_H
#define DMX_OUT_H

#if defined(ARDUINO_ARCH_MBED)
  #include <dma.h>
  #include <pio.h>
#else
  #ifdef ARDUINO
    #include <Arduino.h>
  #endif
  #include "hardware/dma.h"
  #include "hardware/pio.h"
#endif

#define DMX_UNIVERSE_SIZE 512
#define DMX_SM_FREQ 1000000   // PIO clk: 1 MHz -> 1 us per cycle

class DmxOut {
  uint _prgm_offset;
  uint _pin;
  uint _sm;
  PIO _pio;
  uint _dma;

public:
  enum return_code {
    SUCCESS = 0,
    ERR_NO_SM_AVAILABLE   = -1,
    ERR_INSUFFICIENT_PRGM_MEM = -2,
    ERR_NO_DMA_AVAILABLE  = -3
  };

  return_code begin(uint pin, PIO pio = pio0);
  void write(uint8_t *universe, uint length);
  bool busy();
  void end();
};

#endif