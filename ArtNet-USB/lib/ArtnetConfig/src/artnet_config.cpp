#include "artnet_config.h"
#include <EEPROM.h>

ArtnetConfig config;

void ArtnetConfig::begin() {
  EEPROM.begin(CFG_EEPROM_SZ);

  if (EEPROM.read(0) == CFG_MAGIC && EEPROM.read(1) == CFG_VERSION) {
    bool ok = true;
    for (int i = 0; ok && i < CFG_NUM_PORTS; i++) {
      net[i]      = EEPROM.read(2 + i);
      subnet[i]   = EEPROM.read(6 + i);
      universe[i] = EEPROM.read(10 + i);
      direction[i] = EEPROM.read(14 + i);
      if (net[i] > 127 || subnet[i] > 15 || universe[i] > 15) ok = false;
      if (direction[i] > 1) ok = false;
    }
    for (int i = 0; ok && i < 4; i++) {
      ip[i]   = EEPROM.read(18 + i);
      mask[i] = EEPROM.read(22 + i);
    }
    // Reject only an all-zero (uninitialised) address/netmask. Zero octets
    // are perfectly valid, e.g. the default 10.0.0.10/255.0.0.0.
    bool ip_zero = true, mask_zero = true;
    for (int i = 0; i < 4; i++) {
      if (ip[i]   != 0) ip_zero   = false;
      if (mask[i] != 0) mask_zero = false;
    }
    if (ip_zero || mask_zero) ok = false;   // invalid network config
    dhcp_enabled = EEPROM.read(26) != 0;
    // Node names (may be empty/uninitialised -> keep defaults below).
    EEPROM.get(27, short_name);
    EEPROM.get(45, long_name);
    if (ok) return;
    // fall through: corrupted or stale layout -> defaults
  }
  reset();
}

void ArtnetConfig::reset() {
  for (int i = 0; i < CFG_NUM_PORTS; i++) {
    net[i]       = 0;
    subnet[i]    = 0;
    universe[i]  = i;
    direction[i] = PORT_OUTPUT;
  }
  const uint8_t def_ip[4]   = {10, 0, 0, 10};
  const uint8_t def_mask[4] = {255, 0, 0, 0};
  memcpy(ip, def_ip, 4);
  memcpy(mask, def_mask, 4);
  dhcp_enabled = true;
  strncpy(short_name, "MagicQ Compact", sizeof(short_name) - 1);
  short_name[sizeof(short_name) - 1] = '\0';
  strncpy(long_name, "MagicQ Compact Mini Connect", sizeof(long_name) - 1);
  long_name[sizeof(long_name) - 1] = '\0';
  save();
}

bool ArtnetConfig::save() {
  EEPROM.write(0, CFG_MAGIC);
  EEPROM.write(1, CFG_VERSION);
  for (int i = 0; i < CFG_NUM_PORTS; i++) {
    EEPROM.write(2 + i,   net[i]);
    EEPROM.write(6 + i,   subnet[i]);
    EEPROM.write(10 + i,  universe[i]);
    EEPROM.write(14 + i,  direction[i]);
  }
  for (int i = 0; i < 4; i++) {
    EEPROM.write(18 + i, ip[i]);
    EEPROM.write(22 + i, mask[i]);
  }
  EEPROM.write(26, dhcp_enabled ? 1 : 0);
  EEPROM.put(27, short_name);
  EEPROM.put(45, long_name);
  return EEPROM.commit();
}