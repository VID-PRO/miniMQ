#pragma once

#include <Arduino.h>

#include "board.h"

class DmxInput;
class DmxTxGroup;

// ==================================================================
//  Best-effort RDM store-and-forward relay.
// ==================================================================
//
//  RDM is a half-duplex protocol on the same pair as DMX.  The router
//  cannot be a full RDM controller (the documented hardware has only
//  one common DIR_OUT and one multiplexed receive path), but it can
//  relay messages between a console and the fixtures:
//
//    1. A request arrives on one input (0xCC 0x01 ...).
//    2. It is sent to every output that is routed from that input.
//    3. DIR_OUT is switched to receive, the 74HC151 selects the output
//       and the 74HC151 output is captured by a PIO UART.
//    4. The first valid answer is relayed to the requesting console.
//
//  Discovery is limited to the first responder per branch because the
//  common direction pin cannot listen to all 16 ports at once.  See
//  README.md for the exact limitations.
// ==================================================================

// Additive 16-bit RDM checksum over `slots` bytes (including the 0xCC
// start code, excluding the two checksum bytes themselves).
uint16_t rdmChecksum(const uint8_t *wire, uint16_t slots);

// Validate a complete RDM wire frame.  Returns false for truncated or
// checksum-faulty packets.  An 18-byte discovery response is accepted
// structurally (its 32-bit checksum is encoded, not an additive sum).
bool rdmValidate(const uint8_t *wire, uint16_t len);

bool rdmIsDiscoveryResponse(const uint8_t *wire, uint16_t len);

class RdmManager {
 public:
  void begin(DmxTxGroup *tx0, DmxTxGroup *tx1, DmxInput *muxLow,
             DmxInput *muxHigh);

  void enable(bool on) { _enabled = on; }
  bool enabled() const { return _enabled; }

  bool busy() const { return _active; }

  // Polls the input RDM queues and advances the current relay job.
  // Returns true while the RDM engine needs exclusive use of the output
  // bus; the main loop must not start a DMX frame in that case.
  bool service(DmxInput *inputs, uint8_t count);

  // Queue a raw wire frame for one output (serial console test command).
  bool sendTest(uint8_t out, const uint8_t *wire, uint16_t len);

  uint32_t requests() const { return _requests; }
  uint32_t responses() const { return _responses; }
  uint32_t timeouts() const { return _timeouts; }
  uint32_t invalid() const { return _invalid; }
  const char *status() const { return _status; }

 private:
  static constexpr uint16_t RDM_MAX = 260;
  static constexpr uint32_t RESPONSE_TIMEOUT_US = 12000;
  static constexpr uint32_t NO_ACTIVITY_US = 4000;

  DmxTxGroup *_tx[2] = {nullptr, nullptr};
  DmxInput *_mux[2] = {nullptr, nullptr};

  bool _enabled = true;
  bool _active = false;
  bool _manual = false;
  uint8_t _reqInput = 0xFF;
  uint8_t _req[RDM_MAX];
  uint16_t _reqLen = 0;

  uint8_t _targets[board::NUM_OUTPUTS];
  uint8_t _targetCount = 0;
  uint8_t _targetIndex = 0;

  uint8_t _resp[RDM_MAX];
  uint16_t _respLen = 0;

  uint32_t _requests = 0;
  uint32_t _responses = 0;
  uint32_t _timeouts = 0;
  uint32_t _invalid = 0;

  char _status[48] = "idle";

  void startJob(uint8_t input, const uint8_t *wire, uint16_t len);
  bool transact(uint8_t out, DmxInput *inputs, uint8_t count,
                uint16_t &respLen);
  void selectMux(uint8_t channel);
  void relay(uint8_t input, const uint8_t *wire, uint16_t len,
             DmxInput *inputs, uint8_t count);
};

extern RdmManager rdm;
