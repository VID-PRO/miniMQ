#include "ssd1306.h"

#include <Wire.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "font5x7.h"

static const uint8_t SSD1306_INIT[] = {
    0xAE,        // display off
    0x20, 0x00,  // horizontal addressing
    0xB0,        // page start
    0xC8,        // COM scan direction remapped
    0x00, 0x10,  // column 0
    0x40,        // display start line 0
    0x81, 0xCF,  // contrast
    0xA1,        // segment remap
    0xA6,        // normal (not inverted)
    0xA8, 0x3F,  // multiplex 1/64
    0xA4,        // display follows RAM
    0xD3, 0x00,  // display offset 0
    0xD5, 0x80,  // clock divide
    0xD9, 0xF1,  // pre-charge
    0xDA, 0x12,  // COM pins
    0xDB, 0x40,  // VCOMH
    0x8D, 0x14,  // charge pump on
    0xAF,        // display on
};

bool Ssd1306::probe(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

bool Ssd1306::begin(uint8_t preferredAddr) {
  _ready = false;

  if (probe(preferredAddr)) {
    _addr = preferredAddr;
  } else if (probe(0x3D)) {
    _addr = 0x3D;
  } else if (probe(0x3C) && preferredAddr != 0x3C) {
    _addr = 0x3C;
  } else {
    return false;
  }

  if (!init()) return false;
  _ready = true;
  clear();
  show();
  return true;
}

void Ssd1306::cmd(uint8_t c) {
  Wire.beginTransmission(_addr);
  Wire.write(0x00);  // command stream
  Wire.write(c);
  Wire.endTransmission();
}

void Ssd1306::cmdList(const uint8_t *cmds, size_t n) {
  Wire.beginTransmission(_addr);
  Wire.write(0x00);
  for (size_t i = 0; i < n; i++) Wire.write(cmds[i]);
  Wire.endTransmission();
}

bool Ssd1306::init() {
  cmdList(SSD1306_INIT, sizeof(SSD1306_INIT));
  return true;
}

void Ssd1306::clear() {
  memset(_buf, 0, sizeof(_buf));
}

void Ssd1306::pixel(int x, int y, bool on) {
  if (x < 0 || y < 0 || x >= WIDTH || y >= HEIGHT) return;
  uint16_t idx = (uint16_t)(y >> 3) * WIDTH + (uint16_t)x;
  uint8_t bit = (uint8_t)(1u << (y & 7));
  if (on) {
    _buf[idx] |= bit;
  } else {
    _buf[idx] &= (uint8_t)~bit;
  }
}

void Ssd1306::hline(int x, int y, int w, bool on) {
  for (int i = 0; i < w; i++) pixel(x + i, y, on);
}

void Ssd1306::vline(int x, int y, int h, bool on) {
  for (int i = 0; i < h; i++) pixel(x, y + i, on);
}

void Ssd1306::rect(int x, int y, int w, int h, bool on) {
  if (w <= 0 || h <= 0) return;
  hline(x, y, w, on);
  hline(x, y + h - 1, w, on);
  vline(x, y, h, on);
  vline(x + w - 1, y, h, on);
}

void Ssd1306::fillRect(int x, int y, int w, int h, bool on) {
  for (int row = 0; row < h; row++) hline(x, y + row, w, on);
}

int Ssd1306::textWidth(const char *s) {
  if (!s) return 0;
  return (int)strlen(s) * charWidth();
}

int Ssd1306::text(int x, int y, const char *s, bool on) {
  if (!s) return x;
  int cx = x;
  for (const char *p = s; *p; p++) {
    const FontGlyph *g = fontLookup(*p);
    if (g) {
      for (int row = 0; row < 7; row++) {
        uint8_t bits = g->row[row];
        for (int col = 0; col < 5; col++) {
          if (bits & (uint8_t)(0x10u >> col)) {
            pixel(cx + col, y + row, on);
          }
        }
      }
    }
    cx += charWidth();
  }
  return cx;
}

int Ssd1306::textf(int x, int y, const char *fmt, ...) {
  char tmp[96];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(tmp, sizeof(tmp), fmt, ap);
  va_end(ap);
  return text(x, y, tmp);
}

void Ssd1306::showStart() {
  _showing = false;
  _txOffset = 0;
  if (!_ready) return;

  // Whole-screen window in horizontal addressing mode.
  cmd(0x21);  // column address
  cmd(0x00);
  cmd(0x7F);
  cmd(0x22);  // page address
  cmd(0x00);
  cmd(0x07);

  _showing = true;
}

bool Ssd1306::showStep() {
  if (!_showing) return false;
  if (_txOffset >= sizeof(_buf)) {
    _showing = false;
    return false;
  }

  // Eight data bytes + control byte take about 225 us at 400 kHz.  The
  // main loop polls every PIO FIFO between two calls, so even an
  // 8-word receiver FIFO cannot overflow.
  const size_t chunk = 8;
  size_t n = sizeof(_buf) - _txOffset;
  if (n > chunk) n = chunk;

  Wire.beginTransmission(_addr);
  Wire.write(0x40);  // data stream
  Wire.write(&_buf[_txOffset], n);
  Wire.endTransmission();

  _txOffset += n;
  if (_txOffset >= sizeof(_buf)) {
    _showing = false;
    return false;
  }
  return true;
}

void Ssd1306::show() {
  showStart();
  while (showStep()) {
  }
}
