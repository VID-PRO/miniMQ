#pragma once

#include <Arduino.h>
#include "hardware/pio.h"
#include "hardware/irq.h"
#include "rdm.h"
#include "DmxOut.h"

// ------------------------------------------------------------------
// One RDM-capable DMX port.
//
// Wire model (per port):
//   DI        -> RS485 driver input        (owned by DmxOut, TX)
//   RO        -> RS485 receiver output     (this driver, RX via PIO)
//   DE/RE     -> direction GPIO            (this driver)
//
// TX of the RDM *request* is done by reusing the port's DmxOut (it emits
// BREAK + MAB + bytes, which is exactly the framing an RDM request needs,
// with buffer[0] = 0xCC start code).
// RX of the fixture's *response* is captured on RO by a PIO uart receiver
// running on pio1 (one SM per port).
// ------------------------------------------------------------------

class RdmPort {
public:
  RdmPort();

  // tx_obj : the DmxOut instance for this DMX port (owns DI pin)
  // rx_pin : RO pin (RS485 receiver output -> Pico GPIO)
  // de_pin : DE/RE direction GPIO
  // pio    : PIO to use for the RX receiver (each port needs its own SM)
  bool begin(DmxOut *tx_obj, uint8_t rx_pin, uint8_t de_pin, PIO pio,
             int sm);

  // Send an RDM request (wire bytes, WITHOUT the two start codes; the start
  // codes are inserted here) to connected fixtures, switch to RX, and capture
  // the first RDM response. Returns true if a response was captured.
  //   req    : RDM message bytes (after SUB-START 0x01), as from rdm_build_*
  //   reqlen : number of bytes in req
  //   resp   : output buffer (>=64) for the response RDM message bytes
  //   resplen: number of response bytes written
  bool transmit_and_receive(const uint8_t *req, int reqlen,
                            uint8_t *resp, int *resplen);

  // Drain any bytes currently in the RX FIFO (call to flush stale data).
  void flush_rx();

  // ---- Raw byte access (used when the port acts as a DMX input) ----

  // True if at least one received byte is waiting in the RX FIFO.
  bool available() const;
  // Pop one received byte from the RX FIFO, or -1 if none is waiting.
  int readByte();

  // Force the transceiver into receive mode (DE low) so RO reflects the bus.
  // Used for input ports, which never stream DMX out.
  void setReceiveMode();
  // Force the transceiver into transmit mode (DE high).
  void setTransmitMode();

  bool started() const { return _started; }

private:
  DmxOut *_tx;
  uint8_t _rx_pin;
  uint8_t _de_pin;
  PIO _pio;
  int _sm;
  bool _started;
  uint _prgm_offset;
};
