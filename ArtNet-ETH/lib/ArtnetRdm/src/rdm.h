#pragma once

#include <stdint.h>
#include <stddef.h>

// ------------------------------------------------------------------
// E1.20 (RDM) constants and helpers for an Art-Net RDM gateway/proxy.
//
// The Pico is a *gateway*: MagicQ sends ArtRdm to it, it transmits the
// RDM request over the DMX line to connected fixtures, receives their
// reply on RO, and wraps it back into an ArtRdm response to MagicQ.
// ------------------------------------------------------------------

// RDM start codes (wire)
#define RDM_START_CODE        0xCC
#define RDM_SUB_START_CODE    0x01

// Command class (in Request CC byte)
#define RDM_CC_DISCOVERY      0x10
#define RDM_CC_GET            0x20
#define RDM_CC_SET            0x30
#define RDM_DISC_COMMAND      0x70   // Discovery command (request)
#define RDM_DISC_RESPONSE     0x71   // Discovery response (on wire, full)

// Response types (a responder replies with a Response Type)
#define RDM_RESPONSE_ACK      0x00
#define RDM_RESPONSE_ACK_TIMER 0x01
#define RDM_RESPONSE_NACK     0x02
#define RDM_RESPONSE_ACK_OVERFLOW 0x03

// Port IDs / Sub-device
#define RDM_ROOT_DEVICE       0x0000

// Discovery response payload bit: the DISC_UNIQUE_BRANCH responder toggles
// the MSB of its Source UID to signal "I may be in the list".

struct RdmHeader {
  uint16_t message_length;      // bytes following the length byte (incl. checksum)
  uint8_t  dest_uid[6];
  uint8_t  src_uid[6];
  uint8_t  txn;                 // transaction number
  uint8_t  resp_type;           // cc (requests) or response type (replies)
  uint16_t message_count;       // low byte high byte -> for responses
  uint16_t sub_device;          // port id / sub-device
  uint16_t param_id;
  uint8_t  param_data_len;
  // param data follows, then 2-byte checksum
};

// Minimum fixed portion of an RDM message that carries these headers.
// (after start code + sub start code removed)
#define RDM_MSG_HEADER_FIXED (6 + 6 + 1 + 1 + 2 + 2 + 2 + 1) // 21 bytes

// Build a 16-bit CCITT (poly 0x1021) checksum over 'len' bytes at 'data'.
uint16_t rdm_crc16(const uint8_t *data, size_t len);

// Return the "Message Length" value to place in the length byte: the number
// of bytes from (including) the Dest UID through (including) the checksum.
// i.e. 6+6+1+1+2+2+2+1 + param_len + 2
uint16_t rdm_message_length(uint8_t param_len);

// Parse an RDM request from a captured wire frame (without start codes).
// Appends the trailing checksum into the buffer as well.
// Returns number of payload bytes, or -1 on error.
int rdm_parse(const uint8_t *buf, size_t len, RdmHeader &h);

// Build an RDM *response* message (the wire bytes after SUB-START 0x01) into
// 'out' (must hold up to 64 bytes). Fills header + param data + checksum.
// Returns total length (excluding the two start codes).
int rdm_build_response(uint8_t *out, const uint8_t *dest_uid,
                       const uint8_t *src_uid, uint8_t txn,
                       uint8_t resp_type, uint16_t sub_device,
                       uint16_t param_id, const uint8_t *data, uint8_t dlen);
