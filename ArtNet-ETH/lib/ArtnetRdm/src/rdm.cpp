#include "rdm.h"
#include <cstring>

uint16_t rdm_crc16(const uint8_t *data, size_t len) {
  uint16_t crc = 0x0000;
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (int b = 0; b < 8; b++) {
      if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
      else              crc = (crc << 1);
    }
  }
  return crc;
}

uint16_t rdm_message_length(uint8_t param_len) {
  // Dest(6)+Src(6)+Txn(1)+CC/RT(1)+MsgCount(2)+SubDev(2)+PID(2)+PDL(1)
  // + paramLen + checksum(2)
  return RDM_MSG_HEADER_FIXED + param_len + 2;
}

int rdm_parse(const uint8_t *buf, size_t len, RdmHeader &h) {
  if (len < RDM_MSG_HEADER_FIXED + 2) return -1;

  uint8_t ml = buf[0];
  if (ml < RDM_MSG_HEADER_FIXED + 2) return -1;
  if ((size_t)ml > len) return -1;      // frame too short for declared length

  // Validate trailing checksum over bytes [1 .. ml]
  uint16_t want = (uint16_t)(buf[ml - 2] << 8) | buf[ml - 1];
  uint16_t got  = rdm_crc16(buf + 1, ml - 3);
  if (want != got) return -1;

  h.message_length = ml;
  memcpy(h.dest_uid, buf + 1, 6);
  memcpy(h.src_uid,  buf + 7, 6);
  h.txn          = buf[13];
  h.resp_type    = buf[14];
  h.message_count= (uint16_t)((buf[16] << 8) | buf[15]);
  h.sub_device   = (uint16_t)((buf[18] << 8) | buf[17]);
  h.param_id     = (uint16_t)((buf[20] << 8) | buf[19]);
  h.param_data_len = buf[21];

  if (h.param_data_len > 231) return -1;
  return (int)h.param_data_len;
}

int rdm_build_response(uint8_t *out, const uint8_t *dest_uid,
                       const uint8_t *src_uid, uint8_t txn,
                       uint8_t resp_type, uint16_t sub_device,
                       uint16_t param_id, const uint8_t *data, uint8_t dlen) {
  int o = 0;
  out[o++] = RDM_MSG_HEADER_FIXED + dlen + 2;   // message length
  memcpy(out + o, dest_uid, 6); o += 6;
  memcpy(out + o, src_uid,  6); o += 6;
  out[o++] = txn;
  out[o++] = resp_type;
  out[o++] = 0; out[o++] = 0;                    // message count (low,high)
  out[o++] = sub_device & 0xFF; out[o++] = sub_device >> 8;
  out[o++] = param_id & 0xFF;   out[o++] = param_id >> 8;
  out[o++] = dlen;
  if (dlen) { memcpy(out + o, data, dlen); o += dlen; }
  uint16_t crc = rdm_crc16(out + 1, o - 1);
  out[o++] = crc >> 8;
  out[o++] = crc & 0xFF;
  return o;
}
