#include "ui.h"

#include <cstdio>
#include <cstring>

#include "artnet_config.h"
#include "board.h"
#include "node.h"

// Key bit masks (bit set = pressed).
enum : uint8_t {
  K_UP = 1 << 0,
  K_DOWN = 1 << 1,
  K_OK = 1 << 2,
  K_BACK = 1 << 3,
};

static const uint8_t KEY_PINS[4] = {board::KEY_UP, board::KEY_DOWN,
                                    board::KEY_OK, board::KEY_BACK};

void NodeUi::begin() {
  for (uint8_t i = 0; i < 4; i++) {
    pinMode(KEY_PINS[i], INPUT_PULLUP);
  }

  // Wire1 is I2C1 (GP26/27) and is initialised in main.cpp before this call.
  if (_oled.begin(board::UI_OLED_ADDR)) {
    Serial.printf("[ui] SSD1306 at 0x%02X\n", (unsigned)_oled.address());
  } else {
    Serial.println("[ui] no SSD1306 found - local display disabled");
  }

  _lastDrawMs = millis() - 1000;
  _dirty = true;
}

void NodeUi::update() {
  readKeys();

  // Keep the status screen fresh (fps, IP, activity).
  if (_mode == STATUS && millis() - _lastDrawMs >= 500) {
    _dirty = true;
  }

  if (_dirty && !_oled.showing()) {
    _dirty = false;
    _lastDrawMs = millis();
    draw();
    _oled.showStart();
  }
  _oled.showStep();
}

void NodeUi::readKeys() {
  uint8_t raw = 0;
  for (uint8_t i = 0; i < 4; i++) {
    if (digitalRead(KEY_PINS[i]) == LOW) raw |= (uint8_t)(1u << i);
  }

  uint8_t pressed = raw & (uint8_t)~_lastRaw;
  _lastRaw = raw;
  if (!pressed) return;
  if (millis() - _lastKeyMs < 30) return;  // debounce
  _lastKeyMs = millis();

  _dirty = true;
  if (pressed & K_UP) {
    onUp();
  } else if (pressed & K_DOWN) {
    onDown();
  } else if (pressed & K_OK) {
    onOk();
  } else if (pressed & K_BACK) {
    onBack();
  }
}

void NodeUi::onUp() {
  if (_mode == STATUS) {
    _page = (uint8_t)((_page + 2) % 3);
  } else if (_mode == MENU) {
    _menu = (uint8_t)((_menu + MENU_ITEMS - 1) % MENU_ITEMS);
    if (_menu < _menuTop) _menuTop = _menu;
    if (_menu >= _menuTop + MENU_VISIBLE) {
      _menuTop = (uint8_t)(_menu - MENU_VISIBLE + 1);
    }
  } else {
    changeValue(+1);
  }
}

void NodeUi::onDown() {
  if (_mode == STATUS) {
    _page = (uint8_t)((_page + 1) % 3);
  } else if (_mode == MENU) {
    _menu = (uint8_t)((_menu + 1) % MENU_ITEMS);
    // Wrapping from the last item back to item 0 must also scroll the
    // window, otherwise the selection would be off-screen.
    if (_menu < _menuTop) {
      _menuTop = _menu;
    } else if (_menu >= _menuTop + MENU_VISIBLE) {
      _menuTop = (uint8_t)(_menu - MENU_VISIBLE + 1);
    }
  } else {
    changeValue(-1);
  }
}

void NodeUi::onOk() {
  if (_mode == STATUS) {
    _mode = MENU;
    _menu = 0;
    _menuTop = 0;
  } else if (_mode == MENU) {
    menuSelect();
  } else {
    _field = (Field)((_field + 1) % F_COUNT);
  }
}

void NodeUi::onBack() {
  if (_mode == MENU) {
    _mode = STATUS;
  } else if (_mode == PORT) {
    config.save();
    _mode = MENU;
  }
}

void NodeUi::menuSelect() {
  if (_menu < board::NUM_PORTS) {
    _port = _menu;
    _field = F_DIR;
    _mode = PORT;
    return;
  }
  switch (_menu) {
    case 4:  // DHCP client on/off
      config.dhcp_enabled = !config.dhcp_enabled;
      config.save();
      break;
    case 5:  // save + reboot
      config.save();
      nodeReboot();
      break;
    case 6:  // factory reset
      config.reset();
      nodeReboot();
      break;
    default:  // back
      _mode = STATUS;
      break;
  }
}

void NodeUi::changeValue(int8_t delta) {
  if (_mode != PORT) return;
  switch (_field) {
    case F_DIR:
      config.direction[_port] =
          config.isInput(_port) ? PORT_OUTPUT : PORT_INPUT;
      nodeApplyDirection(_port);
      break;
    case F_NET:
      config.net[_port] =
          (uint8_t)((config.net[_port] + delta + 128) % 128);
      break;
    case F_SUB:
      config.subnet[_port] =
          (uint8_t)((config.subnet[_port] + delta + 16) % 16);
      break;
    case F_UNI:
      config.universe[_port] =
          (uint8_t)((config.universe[_port] + delta + 16) % 16);
      break;
    default:
      break;
  }
}

void NodeUi::fmtAddr(uint8_t net, uint8_t sub, uint8_t uni, char *buf,
                     size_t len) {
  snprintf(buf, len, "%u/%u/%u", net, sub, uni);
}

void NodeUi::draw() {
  _oled.clear();
  switch (_mode) {
    case STATUS:
      drawStatus();
      break;
    case MENU:
      drawMenu();
      break;
    case PORT:
      drawPort();
      break;
  }
}

void NodeUi::drawStatus() {
  if (_page == 0) {
    char ip[20];
    if (node.ip[0] || node.ip[1] || node.ip[2] || node.ip[3]) {
      snprintf(ip, sizeof(ip), "%u.%u.%u.%u", node.ip[0], node.ip[1],
               node.ip[2], node.ip[3]);
    } else {
      snprintf(ip, sizeof(ip), "---");
    }
    _oled.text(0, 0, "ArtNet-ETH");
    _oled.text(0, 12, "IP");
    _oled.text(18, 12, ip);
    if (node.netReady) {
      _oled.text(0, 24, node.dhcpActive ? "DHCP  link up" : "STATIC link up");
    } else {
      _oled.text(0, 24, node.linkUp ? "link up  init..." : "link down");
    }
    _oled.textf(0, 36, "Art-Net %lu fps", (unsigned long)node.artFps);
    _oled.text(0, 48, "OK = Menue");
  } else if (_page == 1) {
    _oled.textf(0, 0, "Ports        %lu fps", (unsigned long)node.artFps);
    for (uint8_t p = 0; p < board::NUM_PORTS; p++) {
      char addr[16];
      fmtAddr(config.net[p], config.subnet[p], config.universe[p], addr,
              sizeof(addr));
      bool act = config.isInput(p) ? nodeInputActive(p)
                                   : (millis() - node.lastArtMs[p] < 1000);
      _oled.textf(0, (int)(10 + p * 10), "%u %s %-7s %c", (unsigned)(p + 1),
                  config.isInput(p) ? "IN " : "OUT", addr, act ? '*' : ' ');
    }
  } else {
    uint32_t up = node.uptimeMs / 1000;
    uint32_t h = up / 3600;
    uint32_t m = (up / 60) % 60;
    uint32_t s = up % 60;
    _oled.text(0, 0, "System");
    _oled.textf(0, 12, "Up %02lu:%02lu:%02lu", (unsigned long)h,
                (unsigned long)m, (unsigned long)s);
    _oled.textf(0, 24, "Art-Net %lu fps", (unsigned long)node.artFps);
    _oled.text(0, 36, "RDM: ArtRdm");
    _oled.textf(0, 48, "%s", node.dhcpActive ? "DHCP lease" : "statisch");
    _oled.text(0, 56, "UP/DOWN = Seite");
  }
}

void NodeUi::drawMenu() {
  _oled.text(0, 0, "Menue");
  static const char *const labels[MENU_ITEMS] = {
      "Port 1", "Port 2", "Port 3", "Port 4", nullptr, nullptr, nullptr,
      "Zurueck"};
  for (uint8_t row = 0; row < MENU_VISIBLE; row++) {
    uint8_t item = (uint8_t)(_menuTop + row);
    if (item >= MENU_ITEMS) break;
    char line[24];
    if (item == 4) {
      snprintf(line, sizeof(line), "DHCP: %s", config.dhcp_enabled ? "AN" : "AUS");
    } else if (item == 5) {
      snprintf(line, sizeof(line), "Speichern+Neustart");
    } else if (item == 6) {
      snprintf(line, sizeof(line), "Werkseinstellungen");
    } else {
      snprintf(line, sizeof(line), "%s", labels[item]);
    }
    _oled.textf(0, (int)(10 + row * 12), "%c%s", item == _menu ? '>' : ' ',
                line);
  }
}

void NodeUi::drawPort() {
  _oled.textf(0, 0, "Port %u", (unsigned)(_port + 1));
  const char *names[F_COUNT] = {"DIR", "NET", "SUB", "UNI"};
  uint8_t values[F_COUNT] = {
      (uint8_t)(config.isInput(_port) ? 1 : 0), config.net[_port],
      config.subnet[_port], config.universe[_port]};
  for (uint8_t f = 0; f < F_COUNT; f++) {
    if (f == F_DIR) {
      _oled.textf(0, (int)(12 + f * 10), "%c%-3s %s", f == _field ? '>' : ' ',
                  names[f], values[f] ? "IN" : "OUT");
    } else {
      _oled.textf(0, (int)(12 + f * 10), "%c%-3s %u", f == _field ? '>' : ' ',
                  names[f], (unsigned)values[f]);
    }
  }
  _oled.text(0, 56, "OK=Feld BACK=Ende");
}
