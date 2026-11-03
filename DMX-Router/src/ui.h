#pragma once

#include <Arduino.h>

#include "ssd1306.h"

class DmxInput;

// ==================================================================
//  Optional local user interface: 128x64 SSD1306 OLED + buttons.
// ==================================================================
//
//  The four buttons are wired to the PCF8574 pins P4..P7 to GND:
//      P4 = UP, P5 = DOWN, P6 = OK, P7 = BACK
//
//  P0..P3 of the same PCF8574 drive DIR_OUT and the shared 74HC151
//  select lines (see ioexp.h), so the expander is required for the
//  output direction even if no OLED is fitted.
//
//  The OLED is optional.  If it is not found the router still works
//  and is configured through the USB serial console.
// ==================================================================

class RouterUi {
 public:
  void begin();
  bool present() const { return _oled.ready(); }
  bool buttons() const;

  void update(const DmxInput *inputs, uint8_t count);

 private:
  enum Mode : uint8_t { STATUS, SELECT, EDIT };

  Ssd1306 _oled;

  Mode _mode = STATUS;
  uint8_t _page = 0;       // status page: outputs 1..8 or 9..16
  uint8_t _selected = 0;   // 0-based output index
  uint8_t _editMask = 0;

  uint8_t _lastRaw = 0x00;
  uint8_t _prevPressed = 0;
  uint32_t _lastChangeMs = 0;
  uint32_t _lastDrawMs = 0;
  bool _dirty = true;

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
