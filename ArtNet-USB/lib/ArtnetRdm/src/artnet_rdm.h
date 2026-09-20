#pragma once

#include <Arduino.h>
#include "rdm_port.h"
#include "WiFiUDP.h"

class ArtnetConfig;

// ------------------------------------------------------------------
// ArtRdm bridge: turns MagicQ's ArtRdm (opcode 0x00CC) into an RDM
// command on the target DMX port and returns the fixture's reply.
//
// ArtRdm (Art-Net 4) byte layout (offsets from packet start):
//   0-7   "Art-Net"
//   8-9   OpCode 0x00CC  (low byte first)
//   10-11 ProtVer = 14
//   12    RdmVer = 0x01
//   13-19 Filler[7]
//   20    Net            (top 7 bits of 15-bit Port-Address)
//   21    Command        (0x00 = ArProcess / process RDM packet)
//   22-23 Filler
//   24    Address        (low 8 bits of Port-Address)
//   25+   RdmPacket (the RDM message, WITHOUT the DMX/RDM start code)
// ------------------------------------------------------------------

class ArtnetRdm {
public:
  // cfg: the ArtnetConfig whose per-port net/subnet/universe triples map
  // ArtRdm Port-Addresses to the physical RdmPort handling them.
  ArtnetRdm(WiFiUDP &udp, RdmPort *ports, const ArtnetConfig *cfg);

  // Handle an incoming UDP datagram already read into 'pkt' (len 'n').
  // Returns true if it was (at least attempted as) ArtRdm.
  bool handlePacket(const uint8_t *pkt, int n, const IPAddress &src);

private:
  WiFiUDP &_udp;
  RdmPort *_ports;
  const ArtnetConfig *_cfg;
};
