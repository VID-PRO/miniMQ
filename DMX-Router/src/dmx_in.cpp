#include "dmx_in.h"

#include "dmx_rx.pio.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"

// All input ports share one copy of the RX program (12 instructions).
static PIO s_rxPio = nullptr;
static uint s_rxOffset = 0;

bool DmxInput::begin(PIO pio, uint8_t pin, uint8_t index) {
  _pio = pio;
  _pin = pin;
  _index = index;

  if (s_rxPio != pio) {
    if (!pio_can_add_program(pio, &dmx_rx_program)) return false;
    s_rxOffset = pio_add_program(pio, &dmx_rx_program);
    s_rxPio = pio;
  }
  _offset = s_rxOffset;

  int sm = pio_claim_unused_sm(pio, false);
  if (sm < 0) return false;
  _sm = sm;

  pio_gpio_init(pio, pin);
  pio_sm_set_consecutive_pindirs(pio, sm, pin, 1, false);
  gpio_pull_up(pin);  // idle-high bus / safe when no transceiver is fitted

  pio_sm_config c = dmx_rx_program_get_default_config(_offset);
  sm_config_set_in_pins(&c, pin);
  sm_config_set_jmp_pin(&c, pin);
  // Shift right, no autopush (we push manually after the stop-bit check).
  sm_config_set_in_shift(&c, true, false, 32);
  sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
  sm_config_set_clkdiv(&c, clock_get_hz(clk_sys) / 2000000.0f);  // 8 cyc/bit
  pio_sm_init(pio, sm, _offset, &c);
  pio_sm_clear_fifos(pio, sm);
  pio_sm_exec(pio, sm, pio_encode_jmp(_offset));
  pio_sm_set_enabled(pio, sm, true);

  _state = WAIT_START;
  Serial.printf("[dmx] IN%u on GP%u (PIO%u SM%u)\n", index + 1, pin,
                pio_get_index(pio), sm);
  return true;
}

void DmxInput::end() {
  if (_sm >= 0) {
    pio_sm_set_enabled(_pio, _sm, false);
    pio_sm_unclaim(_pio, _sm);
    _sm = -1;
  }
}

void DmxInput::poll() {
  if (_sm < 0) return;

  while (!pio_sm_is_rx_fifo_empty(_pio, _sm)) {
    uint32_t w = pio_sm_get(_pio, _sm);
    if (_suspended) continue;

    // The RX program shifts right and samples 9 bits (8 data + first
    // stop bit).  They end up in bits 31..23 of the pushed word:
    //   bit 31 = stop bit, bits 30..23 = data bits 7..0.
    uint16_t v = (uint16_t)((w >> 23) & 0x1FFu);
    _activity++;
    if (!(v & 0x100u)) {
      onBreak();
    } else {
      onByte((uint8_t)(v & 0xFFu));
    }
  }
}

void DmxInput::suspend() {
  _suspended = true;
  _state = WAIT_START;
  _idx = 0;
  _rdmLen = 0;
}

void DmxInput::resume() {
  if (_sm >= 0) {
    while (!pio_sm_is_rx_fifo_empty(_pio, _sm)) {
      (void)pio_sm_get(_pio, _sm);
    }
  }
  _state = WAIT_START;
  _idx = 0;
  _rdmLen = 0;
  _rdmHead = _rdmTail = 0;
  _suspended = false;
}

void DmxInput::finalizeDmx() {
  memcpy(_frame, _dmx, board::DMX_FRAME);
  _lastFrameMs = millis();
  _frames++;
  _updated = true;
}

void DmxInput::onBreak() {
  _breaks++;

  // A discovery response has no length byte.  It is exactly 18 bytes on
  // the wire (0xCC, 0x01, 12 encoded UID bytes, 4 encoded checksum
  // bytes) and is terminated by the next break.
  if (_state == RDM_MSG && _rdmLen == 18) {
    pushRdm();
  }

  // DMX allows fewer than 512 slots.  A short frame is finalised by the
  // following break; the slots that were not transmitted read as zero.
  if (_state == DMX_DATA && _idx > 0) {
    for (uint16_t s = _idx; s < board::DMX_SLOTS; s++) _dmx[1 + s] = 0;
    finalizeDmx();
  }

  _state = WAIT_START;
  _idx = 0;
  _rdmLen = 0;
}

void DmxInput::pushRdm() {
  uint8_t next = (uint8_t)((_rdmHead + 1) % RDM_QUEUE_DEPTH);
  if (next == _rdmTail) {
    // Queue full: drop the oldest frame.
    _rdmTail = (uint8_t)((_rdmTail + 1) % RDM_QUEUE_DEPTH);
  }
  memcpy(_rdmQueue[_rdmHead], _rdm, _rdmLen);
  _rdmLens[_rdmHead] = _rdmLen;
  _rdmHead = next;
  _rdmLen = 0;
}

bool DmxInput::takeRdm(uint8_t *out, uint16_t maxLen, uint16_t &outLen) {
  if (_rdmTail == _rdmHead) return false;
  uint16_t n = _rdmLens[_rdmTail];
  if (n > maxLen) n = maxLen;
  memcpy(out, _rdmQueue[_rdmTail], n);
  outLen = n;
  _rdmTail = (uint8_t)((_rdmTail + 1) % RDM_QUEUE_DEPTH);
  return true;
}

void DmxInput::clearRdm() {
  _rdmHead = _rdmTail = 0;
  _rdmLens[0] = _rdmLens[1] = 0;
  _rdmLen = 0;
  _state = WAIT_START;
  _idx = 0;
}

void DmxInput::flush() {
  if (_sm >= 0) {
    while (!pio_sm_is_rx_fifo_empty(_pio, _sm)) {
      (void)pio_sm_get(_pio, _sm);
    }
  }
  clearRdm();
  _activity = 0;
  _updated = false;
}

void DmxInput::onByte(uint8_t b) {
  switch (_state) {
    case WAIT_START:
      // 0xCC starts an RDM frame, everything else is a DMX start code
      // (normally 0x00).  A break marker has already reset us.
      if (b == board::RDM_START_CODE) {
        _rdm[0] = board::RDM_START_CODE;
        _rdmLen = 1;
        _state = RDM_SUB;
      } else {
        _dmx[0] = b;
        _idx = 0;
        _state = DMX_DATA;
      }
      break;

    case DMX_DATA:
      if (_idx < board::DMX_SLOTS) {
        _dmx[1 + _idx] = b;
        _idx++;
      }
      if (_idx >= board::DMX_SLOTS) {
        finalizeDmx();
        _state = WAIT_START;
        _idx = 0;
      }
      break;

    case RDM_SUB:
      if (b == board::RDM_SUB_START_CODE) {
        _rdm[1] = b;
        _rdmLen = 2;
        _state = RDM_MSG;
      } else {
        _state = WAIT_START;
        _rdmLen = 0;
      }
      break;

    case RDM_MSG:
      if (_rdmLen >= sizeof(_rdm)) {
        _state = WAIT_START;
        _rdmLen = 0;
        break;
      }
      _rdm[_rdmLen++] = b;

      // The RDM length byte counts every slot from the 0xCC start code
      // through the last parameter data byte (not the 2 checksum bytes).
      // Minimum length is 24 (empty parameter data).
      if (_rdmLen >= 3) {
        uint16_t msgLen = _rdm[2];
        if (msgLen >= 24 && msgLen <= 255 && _rdmLen >= (uint16_t)(msgLen + 2)) {
          pushRdm();
          _state = WAIT_START;
        }
      }
      break;
  }
}

bool DmxInput::takeFrame() {
  if (!_updated) return false;
  _updated = false;
  return true;
}
