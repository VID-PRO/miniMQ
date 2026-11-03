#pragma once

#include <Arduino.h>
#include "board.h"

// ==================================================================
//  Persistent router configuration (LittleFS /router.cfg)
// ==================================================================
//
//  route[i] is a bit mask of the inputs that feed output i:
//      bit 0 = IN1, bit 1 = IN2, bit 2 = IN3, bit 3 = IN4.
//  The selected inputs are merged HTP (highest takes precedence).
//      route[i] == 0 -> output holds its last frame.
//
//  Default: a 4-way splitter (OUT 1..4 <- IN1, 5..8 <- IN2, ...).
// ==================================================================

struct RouterConfigData {
  uint32_t magic;
  uint16_t version;
  uint8_t  route[board::NUM_OUTPUTS];
  uint8_t  refreshHz;
  uint8_t  rdmEnabled;
  uint8_t  reserved[3];
  uint32_t checksum;
} __attribute__((packed));

class RouterConfig {
 public:
  void begin();
  bool save();
  void reset();                 // defaults in RAM (call save() to persist)

  uint8_t route(uint8_t out) const;
  void setRoute(uint8_t out, uint8_t mask, bool persist = false);
  void setAllRoutes(uint8_t mask, bool persist = false);

  uint8_t refreshHz() const;
  void setRefreshHz(uint8_t hz);

  bool rdmEnabled() const;
  void setRdmEnabled(bool on);

  const RouterConfigData &raw() const { return _data; }

 private:
  RouterConfigData _data;
  bool _fsReady = false;

  void sanitize();
  bool valid(const RouterConfigData &d) const;
  static uint32_t checksum(const RouterConfigData &d);
};

extern RouterConfig config;
