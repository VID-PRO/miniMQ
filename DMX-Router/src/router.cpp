#include "router.h"

#include <cstring>

#include "config.h"
#include "dmx_in.h"

void DmxRouter::begin() {
  memset(_out, 0, sizeof(_out));  // start code 0x00, all slots at 0
  _frames = 0;
  _lastUpdateMs = 0;
}

uint8_t DmxRouter::mask(uint8_t out) const {
  return (out < board::NUM_OUTPUTS) ? config.route(out) : 0;
}

uint8_t DmxRouter::activeSources(uint8_t out, const DmxInput *inputs,
                                 uint8_t count) const {
  if (out >= board::NUM_OUTPUTS) return 0;
  uint8_t n = 0;
  uint32_t now = millis();
  uint8_t m = config.route(out);
  for (uint8_t i = 0; i < count && i < board::NUM_INPUTS; i++) {
    if (!(m & (1u << i))) continue;
    if (inputs[i].active(now) && inputs[i].startCode() == 0) n++;
  }
  return n;
}

void DmxRouter::merge(uint8_t out, const DmxInput *inputs, uint8_t count) {
  if (out >= board::NUM_OUTPUTS) return;

  uint8_t m = config.route(out);
  if (m == 0) return;  // hold last frame

  uint32_t now = millis();
  bool use[board::NUM_INPUTS] = {false, false, false, false};
  uint8_t contributors = 0;

  for (uint8_t i = 0; i < count && i < board::NUM_INPUTS; i++) {
    if (!(m & (1u << i))) continue;
    const DmxInput &in = inputs[i];
    // Only normal DMX (start code 0x00) is merged.  Alternate start
    // codes and RDM are not level data.
    if (!in.active(now) || in.startCode() != 0) continue;
    use[i] = true;
    contributors++;
  }

  if (contributors == 0) return;  // hold last frame

  _out[out][0] = 0x00;
  for (uint16_t slot = 1; slot < board::DMX_FRAME; slot++) {
    uint8_t v = 0;
    for (uint8_t i = 0; i < count && i < board::NUM_INPUTS; i++) {
      if (!use[i]) continue;
      uint8_t s = inputs[i].data()[slot];
      if (s > v) v = s;
    }
    _out[out][slot] = v;
  }

  _frames++;
  _lastUpdateMs = now;
}

void DmxRouter::onFrame(const DmxInput *inputs, uint8_t count,
                        uint8_t changed) {
  if (changed >= count) return;
  for (uint8_t out = 0; out < board::NUM_OUTPUTS; out++) {
    if (config.route(out) & (1u << changed)) {
      merge(out, inputs, count);
    }
  }
}

void DmxRouter::rebuild(const DmxInput *inputs, uint8_t count) {
  for (uint8_t out = 0; out < board::NUM_OUTPUTS; out++) {
    merge(out, inputs, count);
  }
}
