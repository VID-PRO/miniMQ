#pragma once

#include <Arduino.h>

#include "ssd1306.h"

// ==================================================================
//  Local UI: 128x64 SSD1306 (I2C1) + four keys to GND.
//
//    UP    - previous page / previous menu item / increment value
//    DOWN  - next page / next menu item / decrement value
//    OK    - open menu / execute / next field
//    BACK  - leave menu / finish port editor
//
//  The web UI remains the primary configuration interface; the local
//  menu can edit each port's direction and Art-Net address, toggle the
//  DHCP client, save/reboot and restore factory defaults.
// ==================================================================

class NodeUi {
 public:
  void begin();
  bool present() const { return _oled.ready(); }

  // Poll the keys and advance the non-blocking display transfer.
  // Call from loop() as often as possible.
  void update();

 private:
  enum Mode : uint8_t { STATUS, MENU, PORT };
  enum Field : uint8_t { F_DIR, F_NET, F_SUB, F_UNI, F_COUNT };

  static constexpr uint8_t MENU_ITEMS = 8;
  static constexpr uint8_t MENU_VISIBLE = 4;

  Ssd1306 _oled;

  Mode _mode = STATUS;
  uint8_t _page = 0;        // status page 0..2
  uint8_t _menu = 0;        // selected menu item
  uint8_t _menuTop = 0;     // first visible menu item
  uint8_t _port = 0;        // port being edited
  Field _field = F_DIR;

  uint8_t _lastRaw = 0;
  uint32_t _lastKeyMs = 0;
  uint32_t _lastDrawMs = 0;
  bool _dirty = true;

  void readKeys();
  void onUp();
  void onDown();
  void onOk();
  void onBack();

  void draw();
  void drawStatus();
  void drawMenu();
  void drawPort();

  void menuSelect();
  void changeValue(int8_t delta);

  static void fmtAddr(uint8_t net, uint8_t sub, uint8_t uni, char *buf,
                      size_t len);
};
