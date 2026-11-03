#include "ui.h"

#include <Wire.h>
#include <cstdio>
#include <cstring>

#include "board.h"
#include "config.h"
#include "dmx_in.h"
#include "rdm.h"

void RouterUi::begin() {
  Wire.setSDA(board::UI_SDA);
  Wire.setSCL(board::UI_SCL);
  Wire.begin();
  Wire.setClock(400000);

  if (_oled.begin(board::UI_OLED_ADDR)) {
    Serial.printf("[ui] SSD1306 at 0x%02X\n", _oled.address());
  } else {
    Serial.println("[ui] no SSD1306 found - display disabled");
  }

  probePcf();
  if (_pcfReady) {
    Serial.printf("[ui] PCF8574 at 0x%02X\n", _pcfAddr);
  } else {
    Serial.println("[ui] no PCF8574 found - buttons disabled");
  }

  _lastDrawMs = millis() - 1000;
  _dirty = true;
}

void RouterUi::probePcf() {
  for (uint8_t addr = board::UI_PCF_ADDR; addr <= board::UI_PCF_ADDR + 7;
       addr++) {
    Wire.beginTransmission(addr);
    Wire.write(0xFF);  // all pins high = inputs with weak pull-ups
    if (Wire.endTransmission() != 0) continue;

    if (Wire.requestFrom(addr, (uint8_t)1) == 1) {
      while (Wire.available()) (void)Wire.read();
      _pcfAddr = addr;
      _pcfReady = true;
      _lastRaw = 0x0F;
      return;
    }
  }
}

const char *RouterUi::maskText(uint8_t mask, char *buf, size_t len) {
  mask &= 0x0F;
  if (mask == 0) {
    snprintf(buf, len, "HOLD");
    return buf;
  }
  size_t o = 0;
  for (uint8_t i = 0; i < 4 && o + 3 < len; i++) {
    if (!(mask & (1u << i))) continue;
    if (o) buf[o++] = '+';
    buf[o++] = (char)('1' + i);
  }
  buf[o] = 0;
  return buf;
}

void RouterUi::readButtons() {
  if (!_pcfReady) return;

  uint8_t raw = 0x0F;
  if (Wire.requestFrom(_pcfAddr, (uint8_t)1) == 1) {
    if (Wire.available()) raw = (uint8_t)(Wire.read() & 0x0F);
  }

  uint32_t now = millis();
  if (raw != _lastRaw) {
    _lastRaw = raw;
    _lastChangeMs = now;
    return;
  }
  if ((uint32_t)(now - _lastChangeMs) < 25) return;

  uint8_t pressed = (uint8_t)((~raw) & 0x0F);
  uint8_t edge = (uint8_t)(pressed & ~_prevPressed);
  _prevPressed = pressed;
  if (!edge) return;

  if (edge & 0x01) onUp();
  if (edge & 0x02) onDown();
  if (edge & 0x04) onOk();
  if (edge & 0x08) onBack();
}

void RouterUi::onUp() {
  _dirty = true;
  if (_mode == STATUS) {
    _page = (_page + 1) & 1;
  } else if (_mode == SELECT) {
    _selected = (uint8_t)((_selected + 1) % board::NUM_OUTPUTS);
  } else {
    _editMask = (uint8_t)((_editMask + 1) & 0x0F);
  }
}

void RouterUi::onDown() {
  _dirty = true;
  if (_mode == STATUS) {
    _page = (_page + 1) & 1;
  } else if (_mode == SELECT) {
    _selected =
        (uint8_t)((_selected + board::NUM_OUTPUTS - 1) % board::NUM_OUTPUTS);
  } else {
    _editMask = (uint8_t)((_editMask + 15) & 0x0F);
  }
}

void RouterUi::onOk() {
  _dirty = true;
  if (_mode == STATUS) {
    _selected = (uint8_t)(_page * 8);
    _mode = SELECT;
  } else if (_mode == SELECT) {
    _editMask = config.route(_selected);
    _mode = EDIT;
  } else {
    config.setRoute(_selected, _editMask, true);
    Serial.printf("[ui] OUT%u route = 0x%X (saved)\n", _selected + 1,
                  _editMask);
    _mode = SELECT;
  }
}

void RouterUi::onBack() {
  _dirty = true;
  if (_mode == STATUS) {
    config.setRdmEnabled(!config.rdmEnabled());
    rdm.enable(config.rdmEnabled());
    config.save();
    Serial.printf("[ui] RDM %s\n", config.rdmEnabled() ? "on" : "off");
  } else if (_mode == SELECT) {
    _mode = STATUS;
  } else {
    _mode = SELECT;
  }
}

void RouterUi::drawStatus(const DmxInput *inputs, uint8_t count) {
  uint32_t now = millis();
  _oled.text(0, 0, "DMX ROUTER 4>16");
  _oled.hline(0, 8, 128);

  bool a[4] = {false, false, false, false};
  for (uint8_t i = 0; i < count && i < 4; i++) {
    a[i] = inputs[i].active(now) && inputs[i].startCode() == 0;
  }
  _oled.textf(0, 10, "IN 1%c 2%c 3%c 4%c", a[0] ? '*' : '-', a[1] ? '*' : '-',
              a[2] ? '*' : '-', a[3] ? '*' : '-');

  _oled.textf(0, 18, "RDM %s   %u HZ", config.rdmEnabled() ? "ON" : "OFF",
              config.refreshHz());

  _oled.textf(0, 26, "OUT %u-%u", _page ? 9 : 1, _page ? 16 : 8);
  _oled.hline(0, 33, 128);

  // Two columns, four rows each (8 outputs per page).  A 7 px line
  // pitch fits four 5x7 glyph rows exactly above the bottom edge.
  char m[10];
  for (uint8_t row = 0; row < 4; row++) {
    for (uint8_t col = 0; col < 2; col++) {
      uint8_t out = (uint8_t)(_page * 8 + col * 4 + row);
      int x = col ? 64 : 0;
      int y = 36 + row * 7;
      maskText(config.route(out), m, sizeof(m));
      _oled.textf(x, y, "%02u:%s", out + 1, m);
    }
  }
}

void RouterUi::drawSelect() {
  _oled.text(0, 0, "SELECT OUTPUT");
  _oled.hline(0, 9, 128);
  _oled.textf(0, 16, "OUT %02u", _selected + 1);

  char m[10];
  maskText(config.route(_selected), m, sizeof(m));
  _oled.textf(0, 28, "ROUTE %s", m);

  _oled.text(0, 44, "OK=EDIT");
  _oled.text(0, 54, "BACK=EXIT");
}

void RouterUi::drawEdit() {
  _oled.textf(0, 0, "EDIT OUT %02u", _selected + 1);
  _oled.hline(0, 9, 128);
  _oled.textf(0, 14, "MASK %u", _editMask);

  for (uint8_t i = 0; i < 4; i++) {
    int x = 2 + i * 26;
    int y = 26;
    bool on = (_editMask & (1u << i)) != 0;
    _oled.rect(x, y, 22, 15, true);
    if (on) _oled.rect(x + 2, y + 2, 18, 11, true);
    _oled.textf(x + 3, y + 4, "IN%u", i + 1);
  }

  char m[10];
  if (_editMask == 0) {
    _oled.text(0, 46, "OUTPUT HOLDS LAST");
  } else {
    maskText(_editMask, m, sizeof(m));
    _oled.textf(0, 46, "HTP: %s", m);
  }
  _oled.text(0, 56, "OK=SAVE BACK=ESC");
}

void RouterUi::draw(const DmxInput *inputs, uint8_t count) {
  _oled.clear();
  switch (_mode) {
    case STATUS:
      drawStatus(inputs, count);
      break;
    case SELECT:
      drawSelect();
      break;
    case EDIT:
      drawEdit();
      break;
  }
  // Do not block for ~25 ms while the whole framebuffer is transferred.
  // update() feeds it in small chunks instead.
  _oled.showStart();
}

void RouterUi::update(const DmxInput *inputs, uint8_t count) {
  if (!_oled.ready() && !_pcfReady) return;

  readButtons();

  uint32_t now = millis();
  if (_oled.ready()) {
    if (!_oled.showing()) {
      if (_dirty || (uint32_t)(now - _lastDrawMs) >= 300) {
        draw(inputs, count);
        _dirty = false;
        _lastDrawMs = now;
      }
    }
    _oled.showStep();
  }
}
