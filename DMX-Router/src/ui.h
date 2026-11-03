#pragma once

#include <Arduino.h>

#include "ssd1306.h"

class DmxInput;

// ==================================================================
//  Optional local user interface: 128x64 SSD1306 OLED + PCF8574.
// ==================================================================
//
//  The four buttons are wired from PCF8574 pins P0..P3 to GND:
//      P0 = UP, P1 = DOWN, P2 = OK, P3 = BACK
//
//  Both chips are optional.  If they are not found the router still
//  works and is configured through the USB serial console.
// ==================================================================

class RouterUi {
 public:
  void begin();
  bool present() const { return _oled.ready(); }
  bool buttons() const { return _pcfReady; }

  void update(const DmxInput *inputs, uint8_t count);

 private:
  enum Mode : uint8_t { STATUS, SELECT, EDIT };

  Ssd1306 _oled;
  bool _pcfReady = false;
  uint8_t _pcfAddr = 0x20;

  Mode _mode = STATUS;
  uint8_t _page = 0;       // status page: outputs 1..8 or 9..16
  uint8_t _selected = 0;   // 0-based output index
  uint8_t _editMask = 0;

  uint8_t _lastRaw = 0x0F;
  uint8_t _prevPressed = 0;
  uint32_t _lastChangeMs = 0;
  uint32_t _lastDrawMs = 0;
  bool _dirty = true;

  void probePcf();
  void readButtons();
  void onUp();
  void onDown();
  void onOk();
  void onBack();

  void draw(const DmxInput *inputs, uint8_t count);
  void drawStatus(const DmxInput *inputs, uint8_t count);
  void drawSelect();
  void drawEdit();

  static const char *maskText(uint8_t mask, char *buf, size_t len);
};
