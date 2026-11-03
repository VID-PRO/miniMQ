#pragma once

#include <Arduino.h>

#include "board.h"

class DmxInput;

// ==================================================================
//  Routing matrix and HTP merge.
// ==================================================================
//
//  Every output owns a complete 513-byte DMX frame (start code + 512
//  slots).  The frame is rebuilt whenever one of its source inputs
//  delivers a new frame, and periodically to age out inputs that have
//  disappeared (INPUT_TIMEOUT_MS).
//
//  Outputs with mask 0, or whose inputs are all inactive, keep their
//  last frame ("hold last look").  This is the behaviour of a hardware
//  DMX merger and avoids a visible blackout when a console is unplugged.
// ==================================================================

class DmxRouter {
 public:
  void begin();

  // Called by the main loop when input `changed` has a new DMX frame.
  void onFrame(const DmxInput *inputs, uint8_t count, uint8_t changed);

  // Re-evaluate every output (timeouts, configuration changes).
  void rebuild(const DmxInput *inputs, uint8_t count);

  const uint8_t *frame(uint8_t out) const { return _out[out]; }
  uint16_t length(uint8_t out) const { (void)out; return board::DMX_FRAME; }

  uint8_t mask(uint8_t out) const;
  uint32_t frames() const { return _frames; }
  uint32_t lastUpdateMs() const { return _lastUpdateMs; }

  // Highest number of active sources that currently contribute to an
  // output.  Used by the status display / serial console.
  uint8_t activeSources(uint8_t out, const DmxInput *inputs,
                        uint8_t count) const;

 private:
  uint8_t _out[board::NUM_OUTPUTS][board::DMX_FRAME];
  uint32_t _frames = 0;
  uint32_t _lastUpdateMs = 0;

  void merge(uint8_t out, const DmxInput *inputs, uint8_t count);
};
