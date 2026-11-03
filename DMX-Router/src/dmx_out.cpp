#include "dmx_out.h"

#include "board.h"
#include "dmx_tx.pio.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"

// Each byte on the wire is 11 bit times: 1 start + 8 data + 2 stop.
// 48 slices BREAK + 4 slices MAB + 513 bytes * 11 bits = 5695 slices.
// 48 slices = 192 us, which satisfies both E1.11 (>= 88 us) and the
// longer E1.20 RDM break (176..352 us).  Rounded up to whole words.
static const size_t BREAK_SLICES = 48;
static const size_t MAB_SLICES = 4;
static const size_t BITS_PER_BYTE = 11;
static const size_t MAX_SLICES =
    BREAK_SLICES + MAB_SLICES + (board::DMX_FRAME * BITS_PER_BYTE);
static const size_t MAX_WORDS = (MAX_SLICES + 3) / 4;

bool DmxTxGroup::begin(PIO pio, uint8_t basePin) {
  _pio = pio;
  _basePin = basePin;

  if (!pio_can_add_program(pio, &dmx_tx_program)) return false;
  _offset = pio_add_program(pio, &dmx_tx_program);

  int sm = pio_claim_unused_sm(pio, false);
  if (sm < 0) return false;
  _sm = sm;

  // Map the eight consecutive pins to the PIO and drive them idle-high.
  uint32_t mask = 0xFFu << basePin;
  for (uint8_t i = 0; i < 8; i++) pio_gpio_init(pio, basePin + i);
  pio_sm_set_pins_with_mask(pio, sm, mask, mask);
  pio_sm_set_pindirs_with_mask(pio, sm, mask, mask);

  pio_sm_config c = dmx_tx_program_get_default_config(_offset);
  sm_config_set_out_pins(&c, basePin, 8);
  // Shift right, autopull at 32 bits (four slices per word).
  sm_config_set_out_shift(&c, true, true, 32);
  sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
  sm_config_set_clkdiv(&c, clock_get_hz(clk_sys) / 1000000.0f);  // 1 MHz
  pio_sm_init(pio, sm, _offset, &c);
  pio_sm_set_enabled(pio, sm, false);

  int dma = dma_claim_unused_channel(false);
  if (dma < 0) return false;
  _dma = dma;

  dma_channel_config dc = dma_channel_get_default_config(dma);
  channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
  channel_config_set_read_increment(&dc, true);
  channel_config_set_write_increment(&dc, false);
  channel_config_set_dreq(&dc, pio_get_dreq(pio, sm, true));
  dma_channel_set_write_addr(dma, &pio->txf[sm], false);
  dma_channel_set_config(dma, &dc, false);

  _planes = (uint32_t *)malloc(MAX_WORDS * sizeof(uint32_t));
  if (!_planes) return false;
  _planeCap = MAX_WORDS;

  Serial.printf("[dmx] TX group GP%u..GP%u on PIO%u SM%u (DMA %d)\n",
                basePin, basePin + 7, pio_get_index(pio), sm, dma);
  return true;
}

void DmxTxGroup::end() {
  if (_sm >= 0) {
    pio_sm_set_enabled(_pio, _sm, false);
    pio_sm_unclaim(_pio, _sm);
    _sm = -1;
  }
  if (_dma >= 0) {
    dma_channel_unclaim(_dma);
    _dma = -1;
  }
  if (_planes) {
    free(_planes);
    _planes = nullptr;
    _planeCap = 0;
  }
}

// Transpose eight bytes into eight bit slices.  Input byte p in bits
// [8p..8p+7]; output byte b in bits [8b..8b+7] contains bit b of all
// eight input bytes.  Classic 8x8 bit-matrix transpose.
uint64_t DmxTxGroup::transpose8(uint64_t x) {
  uint64_t t;
  t = (x ^ (x >> 7)) & 0x00AA00AA00AA00AAULL;
  x = x ^ t ^ (t << 7);
  t = (x ^ (x >> 14)) & 0x0000CCCC0000CCCCULL;
  x = x ^ t ^ (t << 14);
  t = (x ^ (x >> 28)) & 0x00000000F0F0F0F0ULL;
  x = x ^ t ^ (t << 28);
  return x;
}

size_t DmxTxGroup::buildPlanes(const uint8_t *frames[8], const uint16_t len[8]) {
  size_t maxLen = 0;
  uint8_t breakMask = 0xFF;
  for (uint8_t p = 0; p < 8; p++) {
    if (len[p] > maxLen) maxLen = len[p];
    // Only ports that actually carry a frame get the break.  During an
    // RDM request just one port of a group is driven; the other seven
    // stay in mark and must not see a spurious break.
    if (len[p] > 0) breakMask &= (uint8_t)~(1u << p);
  }
  // A call without any data would emit a break on every pin; refuse it.
  if (maxLen == 0) return 0;
  if (maxLen > board::DMX_FRAME) maxLen = board::DMX_FRAME;

  size_t slices = BREAK_SLICES + MAB_SLICES + maxLen * BITS_PER_BYTE;
  size_t words = (slices + 3) / 4;
  if (words > _planeCap) return 0;

  uint32_t *w = _planes;
  uint32_t acc = 0;
  uint8_t n = 0;

  auto emit = [&](uint8_t slice) {
    acc |= ((uint32_t)slice) << (8 * n);
    if (++n == 4) {
      *w++ = acc;
      acc = 0;
      n = 0;
    }
  };

  // BREAK (low on active ports only) and MAB (high everywhere).
  for (size_t i = 0; i < BREAK_SLICES; i++) emit(breakMask);
  for (size_t i = 0; i < MAB_SLICES; i++) emit(0xFF);

  for (size_t nByte = 0; nByte < maxLen; nByte++) {
    uint64_t x = 0;
    uint8_t startMask = 0;
    for (uint8_t p = 0; p < 8; p++) {
      // A port without a byte at this index stays in mark (high) for
      // the whole bit time, including the start bit.  This matters for
      // the RDM request, which only drives one port of its group.
      if (nByte < len[p]) {
        x |= (uint64_t)frames[p][nByte] << (8 * p);
      } else {
        startMask |= (uint8_t)(1u << p);
        x |= (uint64_t)0xFF << (8 * p);
      }
    }
    uint64_t y = transpose8(x);
    emit(startMask);  // 0 = start bit, 1 = idle port stays high
    for (uint8_t b = 0; b < 8; b++) emit((uint8_t)(y >> (8 * b)));
    emit(0xFF);  // stop bit 1
    emit(0xFF);  // stop bit 2
  }

  // Pad with mark so the PIO always consumes whole words.
  while (n != 0) emit(0xFF);

  return words;
}

bool DmxTxGroup::write(const uint8_t *frames[8], const uint16_t len[8]) {
  if (busy()) return false;

  size_t words = buildPlanes(frames, len);
  if (words == 0) return false;

  pio_sm_set_enabled(_pio, _sm, false);
  pio_sm_restart(_pio, _sm);
  pio_sm_clear_fifos(_pio, _sm);
  pio_sm_exec(_pio, _sm, pio_encode_jmp(_offset));
  pio_sm_set_enabled(_pio, _sm, true);

  dma_channel_transfer_from_buffer_now(_dma, _planes, words);

  // Words are 16 us on the wire.  The margin covers the last word that
  // is still being shifted out of the PIO FIFO.  It also keeps the next
  // frame from restarting the SM during the stop bits.
  _busyUntil = micros() + (uint32_t)(words * 16) + 40;
  return true;
}

bool DmxTxGroup::busy() const {
  if (_dma >= 0 && dma_channel_is_busy(_dma)) return true;
  return (int32_t)(_busyUntil - micros()) > 0;
}

bool DmxTxGroup::waitIdle(uint32_t timeoutUs) {
  uint32_t t0 = micros();
  while (busy()) {
    if (timeoutUs && (uint32_t)(micros() - t0) > timeoutUs) return false;
    tight_loop_contents();
  }
  return true;
}
