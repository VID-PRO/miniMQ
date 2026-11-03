#pragma once

#include <Arduino.h>

// ==================================================================
//  Small self-contained SSD1306 128x64 I2C driver.
// ==================================================================
//
//  No external library is used, so the project builds offline and the
//  flash footprint stays small.  The driver keeps a 1 KB framebuffer
//  and pushes it in small I2C chunks.
// ==================================================================

class Ssd1306 {
 public:
  bool begin(uint8_t preferredAddr = 0x3C);
  bool ready() const { return _ready; }
  uint8_t address() const { return _addr; }

  void clear();
  void pixel(int x, int y, bool on = true);
  void hline(int x, int y, int w, bool on = true);
  void vline(int x, int y, int h, bool on = true);
  void rect(int x, int y, int w, int h, bool on = true);
  void fillRect(int x, int y, int w, int h, bool on = true);

  // Draw text with the built-in 5x7 font.  Returns the x position
  // after the last character.
  int text(int x, int y, const char *s, bool on = true);
  int textf(int x, int y, const char *fmt, ...);

  static int charWidth() { return 6; }
  static int textWidth(const char *s);

  // Blocking full-screen update (used during boot).
  void show();

  // Non-blocking update: showStart() sets the address window and
  // showStep() sends one small I2C chunk.  The main loop calls
  // showStep() repeatedly so the PIO receivers keep getting serviced.
  void showStart();
  bool showStep();
  bool showing() const { return _showing; }

 private:
  static constexpr uint8_t WIDTH = 128;
  static constexpr uint8_t HEIGHT = 64;
  static constexpr uint8_t PAGES = HEIGHT / 8;

  uint8_t _addr = 0x3C;
  bool _ready = false;
  uint8_t _buf[WIDTH * PAGES];
  size_t _txOffset = 0;
  bool _showing = false;

  bool probe(uint8_t addr);
  bool init();
  void cmd(uint8_t c);
  void cmdList(const uint8_t *cmds, size_t n);
};
