#pragma once

#include <Arduino.h>
#include "hardware/pio.h"
#include "board.h"

// ==================================================================
//  DMX/RDM input port (250 kbaud, 8N2) on its own PIO state machine.
// ==================================================================
//
//  dmx_rx.pio samples nine bits per character (8 data + first stop bit)
//  with a right-shifting ISR.  In the raw FIFO word they sit in
//  bits 31..23; DmxInput::poll() extracts them as:
//    bit 8 = stop bit (1 = valid byte, 0 = break / framing error)
//    bits 7..0 = data
//
//  A break marker resets the frame assembler.  The start code selects
//  DMX (0x00 ...) or RDM (0xCC 0x01 ...).  Complete DMX frames land in
//  data(); complete RDM frames are queued for the RDM engine.  RDM
//  discovery responses have no length byte (18 bytes on the wire) and
//  are queued when the following break is seen.
// ==================================================================

class DmxInput {
 public:
  bool begin(PIO pio, uint8_t pin, uint8_t index);
  void end();

  // Drain the PIO FIFO and advance the frame state machine.
  // Must be called often (the FIFO is only eight words deep).
  void poll();

  // True once per complete DMX frame; clears the flag.
  bool takeFrame();

  // Last *complete* DMX frame (start code + 512 slots).  A separate
  // assembly buffer is used so the router never reads a half-received
  // frame while another input triggers a merge.
  const uint8_t *data() const { return _frame; }
  uint8_t startCode() const { return _frame[0]; }
  uint32_t lastFrameMs() const { return _lastFrameMs; }
  uint32_t frames() const { return _frames; }
  uint32_t breaks() const { return _breaks; }
  uint8_t index() const { return _index; }

  bool active(uint32_t now) const {
    return _frames > 0 && (uint32_t)(now - _lastFrameMs) < board::INPUT_TIMEOUT_MS;
  }

  // Pop one complete RDM wire frame (0xCC, 0x01, length, ..., checksum
  // or an 18-byte discovery response).
  bool takeRdm(uint8_t *out, uint16_t maxLen, uint16_t &outLen);

  // Forget any queued / partially received RDM frame.  Used by the RDM
  // engine before it starts listening on a multiplexer channel.
  void clearRdm();

  // Drain the PIO FIFO and reset the parser / RDM queues.  Used on the
  // multiplexed receive inputs before a transaction starts.
  void flush();

  // Mute the parser while the shared input transceivers are driving the
  // console side (RDM response).  Our own echo must not be assembled
  // into a new DMX frame or a new RDM request.  poll() still drains the
  // PIO FIFO so it cannot overflow.
  void suspend();
  void resume();

  // Increments on every pushed character or break.  The RDM engine uses
  // this to distinguish "no answer at all" from a slowly arriving frame.
  uint32_t activity() const { return _activity; }

  static constexpr uint16_t RDM_MAX = 260;

 private:
  enum State : uint8_t { WAIT_START, DMX_DATA, RDM_SUB, RDM_MSG };

  PIO _pio = nullptr;
  int _sm = -1;
  uint8_t _pin = 0;
  uint8_t _index = 0;
  uint _offset = 0;

  uint8_t _dmx[board::DMX_FRAME];    // frame being assembled
  uint8_t _frame[board::DMX_FRAME];  // last complete frame
  uint16_t _idx = 0;
  State _state = WAIT_START;

  uint8_t _rdm[RDM_MAX];
  uint16_t _rdmLen = 0;

  static constexpr uint8_t RDM_QUEUE_DEPTH = 2;
  uint8_t _rdmQueue[RDM_QUEUE_DEPTH][RDM_MAX];
  uint16_t _rdmLens[RDM_QUEUE_DEPTH] = {0, 0};
  uint8_t _rdmHead = 0;
  uint8_t _rdmTail = 0;

  uint32_t _lastFrameMs = 0;
  uint32_t _frames = 0;
  uint32_t _breaks = 0;
  uint32_t _activity = 0;
  bool _updated = false;
  bool _suspended = false;

  void onBreak();
  void onByte(uint8_t b);
  void pushRdm();
  void finalizeDmx();
};
