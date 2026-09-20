#pragma once

#include <Arduino.h>

// EEPROM layout:
//   0        magic 0xAA
//   1        version (5)
//   2..5     net[4]       (Art-Net net    0..127 per port)
//   6..9     subnet[4]    (Art-Net subnet 0..15  per port)
//   10..13   universe[4]  (Art-Net universe 0..15 per port)
//   14..17   direction[4] (0 = output, 1 = input per port)
//   18..21   ip[4]        (static IP of the node)
//   22..25   mask[4]      (netmask)
//   26       dhcp_enabled (1/0)
//   27..44   short_name[18] (NUL-terminated, ArtPollReply ShortName)
//   45..108  long_name[64]  (NUL-terminated, ArtPollReply LongName)
#define CFG_MAGIC        0xAA
#define CFG_VERSION      5
#define CFG_NUM_PORTS    4
#define CFG_EEPROM_SZ    128
#define CFG_SHORT_NAME_SZ 18
#define CFG_LONG_NAME_SZ  64

// Per-port signal direction.
enum : uint8_t { PORT_OUTPUT = 0, PORT_INPUT = 1 };

// Persisted Art-Net configuration (EEPROM-backed, survives reboots).
//
// The full Art-Net 15-bit address of a port is:
//     address = (net << 8) | (subnet << 4) | universe
class ArtnetConfig {
public:
  // Per-DMX-output Art-Net addressing.
  uint8_t net[CFG_NUM_PORTS];
  uint8_t subnet[CFG_NUM_PORTS];
  uint8_t universe[CFG_NUM_PORTS];

  // Per-port direction: PORT_OUTPUT (ArtDmx -> DMX out) or
  // PORT_INPUT (DMX in -> ArtDmx).
  uint8_t direction[CFG_NUM_PORTS];

  // Static node IP and netmask (on the USB NCM link).
  uint8_t ip[4];
  uint8_t mask[4];

  // Serve DHCP so the host auto-configures within ip/mask.
  bool dhcp_enabled;

  // ArtNode names advertised in ArtPollReply (ShortName 18, LongName 64).
  char short_name[CFG_SHORT_NAME_SZ];
  char long_name[CFG_LONG_NAME_SZ];

  // Load from EEPROM; falls back to defaults on first boot / bad layout.
  void begin();
  // Restore factory defaults (net0 sub0 uni0..3, all outputs, 10.0.0.10/255.0.0.0, DHCP on).
  void reset();
  // Write current values to EEPROM. Returns true on success.
  bool save();

  bool isInput(uint8_t p) const { return direction[p] == PORT_INPUT; }

  // 15-bit Art-Net address currently assigned to port 'p' (0..3).
  uint16_t addressForPort(uint8_t p) const {
    return (uint16_t)((net[p] << 8) | (subnet[p] << 4) | universe[p]);
  }
  // Physical port index assigned to address 'a', or -1 if unassigned.
  int portForAddress(uint16_t a) const {
    for (int i = 0; i < CFG_NUM_PORTS; i++) {
      if (addressForPort(i) == a) return i;
    }
    return -1;
  }
  // Port index assigned to address 'a' that is an INPUT, or -1.
  int inputPortForAddress(uint16_t a) const {
    for (int i = 0; i < CFG_NUM_PORTS; i++) {
      if (isInput(i) && addressForPort(i) == a) return i;
    }
    return -1;
  }
  // Port index assigned to address 'a' that is an OUTPUT, or -1.
  int outputPortForAddress(uint16_t a) const {
    for (int i = 0; i < CFG_NUM_PORTS; i++) {
      if (!isInput(i) && addressForPort(i) == a) return i;
    }
    return -1;
  }

  // Convenience helpers built from the stored octets.
  IPAddress ipAddr() const { return IPAddress(ip[0], ip[1], ip[2], ip[3]); }
  IPAddress maskAddr() const { return IPAddress(mask[0], mask[1], mask[2], mask[3]); }
  // Directed broadcast of ip/mask, e.g. 10.255.255.255 for /8.
  IPAddress broadcastAddr() const {
    return IPAddress((uint8_t)(ip[0] | ~mask[0]),
                     (uint8_t)(ip[1] | ~mask[1]),
                     (uint8_t)(ip[2] | ~mask[2]),
                     (uint8_t)(ip[3] | ~mask[3]));
  }
};

extern ArtnetConfig config;