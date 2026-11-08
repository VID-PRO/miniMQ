#include "rdm_port.h"
#include "rdm_rx.pio.h"

#define RDM_SM_FREQ_HZ 4000000u   // 4 MHz -> 16 PIO cycles per 250kbaud bit

RdmPort::RdmPort() : _tx(nullptr), _rx_pin(0xFF), _de_pin(0xFF), _pio(nullptr),
                     _sm(-1), _started(false), _prgm_offset(0) {}

// One shared copy of the RX program per PIO. All 4 SMs run the same program
// at the same offset (they differ only by their input pin), so a single
// 32-instruction PIO fits all ports. Without this, loading 4 copies would
// overflow program memory.
static uint _shared_program_offset(PIO pio, bool &loaded) {
  static PIO added_pio = nullptr;
  static uint offset = 0;
  if (added_pio == pio && offset != 0) {
    loaded = true;
    return offset;
  }
  if (!pio_can_add_program(pio, &rdm_rx_program)) {
    loaded = false;
    return 0;
  }
  offset = pio_add_program(pio, &rdm_rx_program);
  added_pio = pio;
  loaded = true;
  return offset;
}

bool RdmPort::begin(DmxOut *tx_obj, uint8_t rx_pin, uint8_t de_pin,
                    PIO pio, int sm) {
  _tx = tx_obj;
  _rx_pin = rx_pin;
  _de_pin = de_pin;
  _pio = pio;
  _sm = sm;

  // DE/RE direction pin. Idle high = transmit mode so the normal DMX output
  // stream (via DI) drives the bus; we only drop it low while listening for
  // an RDM response.
  pinMode(_de_pin, OUTPUT);
  digitalWrite(_de_pin, HIGH);

  // RO pin -> PIO input
  pio_gpio_init(_pio, _rx_pin);
  pio_sm_set_consecutive_pindirs(_pio, sm, _rx_pin, 1, false);

  bool loaded = false;
  _prgm_offset = _shared_program_offset(_pio, loaded);
  if (!loaded) return false;

  pio_sm_config sm_conf = rdm_rx_program_get_default_config(_prgm_offset);
  sm_config_set_in_pins(&sm_conf, _rx_pin);
  sm_config_set_jmp_pin(&sm_conf, _rx_pin);

  uint clk_div = clock_get_hz(clk_sys) / RDM_SM_FREQ_HZ;
  sm_config_set_clkdiv(&sm_conf, clk_div);

  pio_sm_init(_pio, sm, _prgm_offset, &sm_conf);
  flush_rx();
  pio_sm_set_enabled(_pio, sm, true);

  _started = true;
  return true;
}

void RdmPort::flush_rx() {
  if (!_started) return;
  // Drain any pending bytes and reset the SM to a clean start-bit-wait.
  pio_sm_set_enabled(_pio, _sm, false);
  pio_sm_restart(_pio, _sm);
  while (!pio_sm_is_rx_fifo_empty(_pio, _sm)) {
    pio_sm_get(_pio, _sm);
  }
  pio_sm_exec(_pio, _sm, pio_encode_jmp(_prgm_offset));
  pio_sm_set_enabled(_pio, _sm, true);
}

bool RdmPort::available() const {
  if (!_started) return false;
  return !pio_sm_is_rx_fifo_empty(_pio, _sm);
}

int RdmPort::readByte() {
  if (!_started || pio_sm_is_rx_fifo_empty(_pio, _sm)) return -1;
  return (int)(uint8_t)pio_sm_get(_pio, _sm);
}

void RdmPort::setReceiveMode() {
  if (_de_pin != 0xFF) digitalWrite(_de_pin, LOW);
}

void RdmPort::setTransmitMode() {
  if (_de_pin != 0xFF) digitalWrite(_de_pin, HIGH);
}

bool RdmPort::transmit_and_receive(const uint8_t *req, int reqlen,
                                   uint8_t *resp, int *resplen) {
  *resplen = 0;
  if (!_started) return false;

  // ---- Transmit the request (break + mab + 0xCC + message) ----
  // Reuse DmxOut: buffer[0] is the start code 0xCC, the rest is the
  // RDM message (SUB-START code + ...).
  uint8_t frame[256];
  frame[0] = RDM_START_CODE;              // 0xCC
  int frame_len = reqlen + 1;
  if (frame_len > (int)sizeof(frame)) frame_len = sizeof(frame);
  memcpy(frame + 1, req, frame_len - 1);

  // DE high = drive the line (transmit mode)
  digitalWrite(_de_pin, HIGH);
  _tx->write(frame, frame_len);

  // Wait for the transmission to finish, then release the line (RX mode).
  unsigned long t0 = millis();
  while (_tx->busy()) {
    if (millis() - t0 > 20) break;   // safety
  }
  while (!pio_sm_is_tx_fifo_empty(_pio, _sm)) { /* drain tx? not used */ }

  // Switch back to receive now that we are no longer driving the line.
  digitalWrite(_de_pin, LOW);

  // ---- Capture the fixture's response on RO ----
  // The response must arrive within 2.7 ms. Sampled bytes land in the RX
  // FIFO; the first byte during BREAK is 0x00 (ignored) then 0xCC starts the
  // packet.
  flush_rx();   // clear anything captured during our own TX/MAB

  uint8_t pkt[256];
  int pkt_len = 0;
  bool in_packet = false;

  unsigned long start = millis();
  while ((millis() - start) < 10) {
    while (!pio_sm_is_rx_fifo_empty(_pio, _sm)) {
      uint8_t b = (uint8_t)pio_sm_get(_pio, _sm);
      if (!in_packet) {
        if (b == RDM_START_CODE) {        // 0xCC starts the RDM packet
          in_packet = true;
          pkt[0] = b;
          pkt_len = 1;
        }
        // ignore lone bytes (0x00 from break, etc.)
      } else {
        if (pkt_len < (int)sizeof(pkt)) {
          pkt[pkt_len++] = b;
        }
      }
    }
    // A complete RDM response with a trailing checksum is short; stop early
    // once we have a SUB-START code + plausibly complete message.
    if (in_packet && pkt_len >= 3) {
      // pkt[1] should be SUB-START 0x01; message length in pkt[2].
      if (pkt[1] == RDM_SUB_START_CODE) {
        uint8_t ml = pkt[2];
        if (pkt_len >= (3 + ml)) break;   // whole message captured
      }
    }
  }

  if (!in_packet || pkt_len < 3) {
    digitalWrite(_de_pin, HIGH);   // restore transmit mode regardless
    return false;
  }

  // Strip the two start codes; hand back the RDM message buffer (which
  // includes the trailing checksum).
  int msg_len = pkt_len - 2;
  memcpy(resp, pkt + 2, msg_len);
  *resplen = msg_len;

  // Back to transmit mode so the DMX output stream keeps driving the line.
  digitalWrite(_de_pin, HIGH);
  return true;
}
