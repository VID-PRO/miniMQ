#include "artnet_rdm.h"
#include "artnet_config.h"

#define ARTRDM_PORT 6454

// ArtRdm opcode 0x00CC -> in the little-endian opcode field p[8]/p[9]
#define ARTRDM_OPCODE 0x00CC

ArtnetRdm::ArtnetRdm(WiFiUDP &udp, RdmPort *ports, const ArtnetConfig *cfg)
    : _udp(udp), _ports(ports), _cfg(cfg) {}

bool ArtnetRdm::handlePacket(const uint8_t *pkt, int n, const IPAddress &src) {
  // Ensure a well formed ArtRdm packet: >= 25 bytes header + at least an
  // RDM message length byte + checksum.
  if (n < 27) return false;

  // "Art-Net"
  if (memcmp(pkt, "Art-Net", 7) != 0 || pkt[7] != 0) return false;

  // OpCode 0x00CC (little-endian)
  uint16_t opcode = (uint16_t)(pkt[9] << 8) | pkt[8];
  if (opcode != ARTRDM_OPCODE) return false;

  // Protocol version 14
  if (pkt[10] != 0x00 || pkt[11] != 0x0E) return false;

  // RDM version 0 or 1
  if (pkt[12] != 0x00 && pkt[12] != 0x01) return false;

  uint8_t net     = pkt[20] & 0x7F;
  uint8_t command = pkt[21];
  uint8_t addr    = pkt[24];

  if (command != 0x00) return true;   // ArProcess only; ignore others

  // Map the 15-bit Port-Address (net<<8 | addr) to one of our OUTPUT ports
  // via the configured net/subnet/universe mapping. Input ports do not
  // bridge RDM.
  uint16_t address = (uint16_t)((net << 8) | addr);
  int port = _cfg->outputPortForAddress(address);
  if (port < 0) return true;

  // The RDM message (after byte 25) is the RDM frame WITHOUT the start code.
  const uint8_t *rdm = pkt + 25;
  int rdm_len = n - 25;

  // Transmit and wait for the fixture's reply.
  uint8_t resp[256];
  int resp_len = 0;
  bool ok = _ports[port].transmit_and_receive(rdm, rdm_len, resp, &resp_len);
  if (!ok || resp_len <= 0) return true;   // no response; nothing to send back

  // Wrap the response RDM message into an ArtRdm reply to the controller.
  uint8_t out[32 + 256];
  int o = 0;
  memcpy(out + o, "Art-Net", 7); o += 7;
  out[o++] = 0x00;
  out[o++] = 0xCC;              // OpCode low byte
  out[o++] = 0x00;              // OpCode high byte
  out[o++] = 0x00;              // ProtVerHi
  out[o++] = 0x0E;              // ProtVerLo = 14
  out[o++] = 0x01;              // RdmVer
  memset(out + o, 0, 7); o += 7; // Filler[7]
  out[o++] = net;               // Net (echo back)
  out[o++] = 0x00;              // Command = ArProcess
  out[o++] = 0x00;              // Filler
  out[o++] = 0x00;              // Filler
  out[o++] = addr;              // Address (echo back)
  memcpy(out + o, resp, resp_len); o += resp_len;

  _udp.beginPacket(src, ARTRDM_PORT);
  _udp.write(out, o);
  _udp.endPacket();
  return true;
}
