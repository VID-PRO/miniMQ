#include "rdm.h"

#include <cstdio>
#include <cstring>

#include "config.h"
#include "dmx_in.h"
#include "dmx_out.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"

RdmManager rdm;

uint16_t rdmChecksum(const uint8_t *wire, uint16_t slots) {
  uint32_t sum = 0;
  for (uint16_t i = 0; i < slots; i++) sum += wire[i];
  return (uint16_t)(sum & 0xFFFFu);
}

bool rdmIsDiscoveryResponse(const uint8_t *wire, uint16_t len) {
  return len == 18 && wire[0] == board::RDM_START_CODE &&
         wire[1] == board::RDM_SUB_START_CODE;
}

bool rdmValidate(const uint8_t *wire, uint16_t len) {
  if (rdmIsDiscoveryResponse(wire, len)) return true;

  if (len < 26) return false;
  if (wire[0] != board::RDM_START_CODE) return false;
  if (wire[1] != board::RDM_SUB_START_CODE) return false;

  uint8_t slots = wire[2];  // slots from 0xCC through last parameter byte
  if (slots < 24) return false;
  if ((uint16_t)slots + 2 > len) return false;

  uint16_t want = (uint16_t)((wire[slots] << 8) | wire[slots + 1]);
  return rdmChecksum(wire, slots) == want;
}

void RdmManager::begin(DmxTxGroup *tx0, DmxTxGroup *tx1, DmxInput *muxLow,
                       DmxInput *muxHigh) {
  _tx[0] = tx0;
  _tx[1] = tx1;
  _mux[0] = muxLow;
  _mux[1] = muxHigh;

  // Multiplexer select lines and the common output direction.
  for (uint8_t i = 0; i < 3; i++) {
    pinMode(board::MUX_SEL[i], OUTPUT);
    digitalWrite(board::MUX_SEL[i], LOW);
  }
  pinMode(board::DIR_OUT, OUTPUT);
  digitalWrite(board::DIR_OUT, HIGH);  // default: transmit DMX

  pinMode(board::DIR_IN, OUTPUT);
  digitalWrite(board::DIR_IN, LOW);    // default: receive from console

  _enabled = config.rdmEnabled();
  snprintf(_status, sizeof(_status), "ready");
  Serial.println("[rdm] store-and-forward relay ready");
}

void RdmManager::selectMux(uint8_t channel) {
  channel &= 0x07;
  for (uint8_t i = 0; i < 3; i++) {
    digitalWrite(board::MUX_SEL[i], (channel & (1u << i)) ? HIGH : LOW);
  }
}

void RdmManager::startJob(uint8_t input, const uint8_t *wire, uint16_t len) {
  _requests++;
  _reqInput = input;
  _reqLen = (len > RDM_MAX) ? RDM_MAX : len;
  memcpy(_req, wire, _reqLen);

  _targetCount = 0;
  uint8_t bit = (uint8_t)(1u << input);
  for (uint8_t out = 0; out < board::NUM_OUTPUTS; out++) {
    if (config.route(out) & bit) {
      _targets[_targetCount++] = out;
    }
  }

  _targetIndex = 0;
  _manual = false;

  if (_targetCount == 0) {
    _active = false;
    snprintf(_status, sizeof(_status), "IN%u: no routed output", input + 1);
    return;
  }

  _active = true;
  snprintf(_status, sizeof(_status), "IN%u -> %u output(s)", input + 1,
           _targetCount);
}

bool RdmManager::sendTest(uint8_t out, const uint8_t *wire, uint16_t len) {
  if (out >= board::NUM_OUTPUTS || len < 3 || len > RDM_MAX) return false;
  if (_active) return false;

  memcpy(_req, wire, len);
  _reqLen = len;
  _reqInput = 0xFF;
  _manual = true;
  _targets[0] = out;
  _targetCount = 1;
  _targetIndex = 0;
  _active = true;
  _requests++;
  snprintf(_status, sizeof(_status), "manual test OUT%u", out + 1);
  return true;
}

bool RdmManager::service(DmxInput *inputs, uint8_t count) {
  // Always drain all input RDM queues, even while a transaction is in
  // progress, so the PIO FIFOs and the two-deep queues cannot overflow.
  uint8_t buf[RDM_MAX];
  for (uint8_t i = 0; i < count && i < board::NUM_INPUTS; i++) {
    uint16_t n = 0;
    while (inputs[i].takeRdm(buf, sizeof(buf), n)) {
      if (!_enabled) {
        _invalid++;
        snprintf(_status, sizeof(_status), "RDM disabled");
        continue;
      }
      if (_active) {
        // One request at a time; the console retries.  Draining here is
        // more important than queueing a second request.
        continue;
      }
      if (!rdmValidate(buf, n)) {
        _invalid++;
        snprintf(_status, sizeof(_status), "IN%u: invalid RDM frame",
                 i + 1);
        continue;
      }
      startJob(i, buf, n);
    }
  }

  if (!_active) return false;

  // If a DMX frame is still being shifted out, let it finish.  The main
  // loop sees `true` and will not start another frame.
  if ((_tx[0] && _tx[0]->busy()) || (_tx[1] && _tx[1]->busy())) {
    return true;
  }

  if (_targetIndex >= _targetCount) {
    _active = false;
    _manual = false;
    return false;
  }

  uint8_t out = _targets[_targetIndex++];
  uint16_t respLen = 0;
  bool got = transact(out, inputs, count, respLen);

  if (got) {
    _responses++;
    _respLen = respLen;
    if (_manual) {
      Serial.printf("[rdm] OUT%u response (%u bytes):", out + 1, respLen);
      for (uint16_t i = 0; i < respLen; i++) {
        if ((i % 16) == 0) Serial.print("\n  ");
        Serial.printf("%02X ", _resp[i]);
      }
      Serial.println();
      snprintf(_status, sizeof(_status), "test OUT%u: answer", out + 1);
    } else {
      relay(_reqInput, _resp, respLen, inputs, count);
      snprintf(_status, sizeof(_status), "OUT%u answered -> IN%u", out + 1,
               _reqInput + 1);
    }
    _active = false;
    _manual = false;
    return false;
  }

  _timeouts++;
  if (_targetIndex >= _targetCount) {
    if (_manual) {
      Serial.printf("[rdm] OUT%u: no response\n", out + 1);
    }
    snprintf(_status, sizeof(_status), "OUT%u: no response", out + 1);
    _active = false;
    _manual = false;
    return false;
  }

  return true;
}

bool RdmManager::transact(uint8_t out, DmxInput *inputs, uint8_t count,
                          uint16_t &respLen) {
  if (out >= board::NUM_OUTPUTS || !_tx[0] || !_tx[1]) return false;

  DmxTxGroup *tx = _tx[out >> 3];
  DmxInput *mux = _mux[out >= 8 ? 1 : 0];
  if (!tx || !mux) return false;

  // Select the receive channel before switching the bus direction.
  selectMux(out & 0x07);

  const uint8_t *frames[8] = {nullptr, nullptr, nullptr, nullptr,
                              nullptr, nullptr, nullptr, nullptr};
  uint16_t lens[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  frames[out & 0x07] = _req;
  lens[out & 0x07] = _reqLen;

  if (!tx->write(frames, lens)) return false;

  // The longest relayed request is 257 bytes (~11.5 ms on the wire).
  // Wait for the complete frame before releasing DIR_OUT, but keep
  // polling the console inputs so their 8-word PIO FIFOs do not
  // overflow while the request is being shifted out.
  uint32_t txStart = micros();
  while (tx->busy()) {
    for (uint8_t i = 0; i < count && i < board::NUM_INPUTS; i++) {
      inputs[i].poll();
    }
    if ((uint32_t)(micros() - txStart) > 15000) break;
    tight_loop_contents();
  }

  // The transceiver echoes our own request on RX while DIR is high.
  // Drain that echo before we start listening to the responder.
  // (DmxInput::flush also resets the parser / queues.)
  mux->poll();
  mux->flush();

  // Release the line: all output transceivers switch to receive, but
  // only the selected 74HC151 channel is actually decoded.
  gpio_put(board::DIR_OUT, 0);

  uint32_t t0 = micros();
  uint32_t act0 = mux->activity();
  bool got = false;
  respLen = 0;

  while ((uint32_t)(micros() - t0) < RESPONSE_TIMEOUT_US) {
    mux->poll();
    // The four console inputs keep running at 40 Hz.  Poll them here so
    // their 8-deep PIO FIFOs do not overflow during the wait.
    for (uint8_t i = 0; i < count && i < board::NUM_INPUTS; i++) {
      inputs[i].poll();
    }

    if (mux->takeRdm(_resp, RDM_MAX, respLen)) {
      got = true;
      break;
    }

    // No edge at all after a few milliseconds: there is no responder.
    if ((uint32_t)(micros() - t0) > NO_ACTIVITY_US &&
        mux->activity() == act0) {
      break;
    }
    tight_loop_contents();
  }

  gpio_put(board::DIR_OUT, 1);  // back to DMX transmit / idle

  if (got && !rdmValidate(_resp, respLen)) {
    got = false;
  }

  mux->flush();
  return got;
}

void RdmManager::relay(uint8_t input, const uint8_t *wire, uint16_t len,
                       DmxInput *inputs, uint8_t count) {
  (void)input;
  if (len == 0) return;

  // Drive the console side.  All four input transceivers share TX_IN and
  // DIR_IN; consoles ignore a response whose destination UID is not
  // theirs.
  //
  // A half-duplex transceiver echoes everything it drives back into its
  // own receiver, so the PIO would see our response as a new request on
  // all four inputs.  Suspend the parsers before enabling the driver;
  // poll() keeps draining their FIFOs while suspended.
  for (uint8_t i = 0; i < count && i < board::NUM_INPUTS; i++) {
    inputs[i].suspend();
  }

  digitalWrite(board::DIR_IN, HIGH);
  delayMicroseconds(10);

  Serial2.flush();
  uart_set_break(uart1, true);   // RDM break: 176..352 us
  uint32_t breakStart = micros();
  while ((uint32_t)(micros() - breakStart) < 200) {
    for (uint8_t i = 0; i < count && i < board::NUM_INPUTS; i++) {
      inputs[i].poll();
    }
    tight_loop_contents();
  }
  uart_set_break(uart1, false);
  delayMicroseconds(20);         // MAB >= 12 us

  // Write byte by byte and keep the console receivers drained.  A
  // blocking write of the whole frame would stall polling for up to
  // ~12 ms, which is longer than the 8-word PIO FIFOs can buffer.
  for (uint16_t n = 0; n < len; n++) {
    while (Serial2.availableForWrite() <= 0) {
      for (uint8_t i = 0; i < count && i < board::NUM_INPUTS; i++) {
        inputs[i].poll();
      }
      tight_loop_contents();
    }
    Serial2.write(wire[n]);
  }

  // Wait until the last character has left the shift register so that
  // DIR_IN is only released after a complete, valid frame.  The echo
  // received during these last milliseconds is discarded by resume().
  Serial2.flush();

  delayMicroseconds(10);
  digitalWrite(board::DIR_IN, LOW);
  delayMicroseconds(10);

  for (uint8_t i = 0; i < count && i < board::NUM_INPUTS; i++) {
    inputs[i].resume();
  }
}
