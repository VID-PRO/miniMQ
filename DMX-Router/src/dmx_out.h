#pragma once

#include <Arduino.h>
#include "hardware/pio.h"

// ==================================================================
//  DMX512 transmitter for eight output ports per PIO state machine.
// ==================================================================
//
//  The RP2040 has only eight PIO state machines, and four of them are
//  already needed for the four DMX inputs, so one SM per output is
//  impossible.  This driver uses the "bit slice" method instead:
//
//    slice n (8 bits) = bit n of every port (bit 0 -> base pin, ...)
//
//  Four consecutive slices are packed into one 32-bit word and written
//  to the PIO TX FIFO by DMA.  The PIO emits four `out pins, 8`
//  instructions (4 us each), i.e. 16 us per word.  One SM therefore
//  drives eight DMX ports in parallel with a single DMA channel.
//
//  Frame format (see dmx_tx.pio):
//    BREAK ~192 us, MAB ~16 us, 513 bytes x (1 start + 8 data + 2 stop).
//  The complete frame takes ~22.8 ms -> ~43 Hz maximum refresh.
// ==================================================================

class DmxTxGroup {
 public:
  bool begin(PIO pio, uint8_t basePin);
  void end();

  // Start a frame.  frames[p] points at the raw DMX/RDM bytes for output
  // (base + p); len[p] is the number of bytes (including the start code).
  // len[p] == 0 keeps that port in mark for the whole frame, including
  // the break (needed for a single-port RDM request).
  // Returns false if the previous frame is still on the wire.
  bool write(const uint8_t *frames[8], const uint16_t len[8]);

  bool busy() const;
  bool waitIdle(uint32_t timeoutUs = 60000);

  uint8_t pins() const { return _basePin; }

 private:
  PIO _pio = nullptr;
  int _sm = -1;
  uint8_t _basePin = 0;
  int _dma = -1;
  uint _offset = 0;
  uint32_t *_planes = nullptr;
  size_t _planeCap = 0;       // in 32-bit words
  uint32_t _busyUntil = 0;    // micros() deadline of the current frame

  size_t buildPlanes(const uint8_t *frames[8], const uint16_t len[8]);
  static uint64_t transpose8(uint64_t x);
};
