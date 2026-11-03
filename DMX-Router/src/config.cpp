#include "config.h"

#include <LittleFS.h>
#include <cstring>

RouterConfig config;

static const char *CONFIG_FILE = "/router.cfg";
static const uint32_t CONFIG_MAGIC = 0x52544D58u;  // 'RTMX'
static const uint16_t CONFIG_VERSION = 1;

uint32_t RouterConfig::checksum(const RouterConfigData &d) {
  // FNV-1a over everything except the checksum field itself.
  const uint8_t *p = reinterpret_cast<const uint8_t *>(&d);
  size_t n = offsetof(RouterConfigData, checksum);
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < n; i++) {
    h ^= p[i];
    h *= 16777619u;
  }
  return h;
}

void RouterConfig::sanitize() {
  for (uint8_t i = 0; i < board::NUM_OUTPUTS; i++) {
    _data.route[i] &= 0x0F;  // only IN1..IN4 exist
  }
  if (_data.refreshHz < 5) _data.refreshHz = 5;
  if (_data.refreshHz > 43) _data.refreshHz = 43;
  _data.rdmEnabled = _data.rdmEnabled ? 1 : 0;
  _data.magic = CONFIG_MAGIC;
  _data.version = CONFIG_VERSION;
  _data.checksum = checksum(_data);
}

bool RouterConfig::valid(const RouterConfigData &d) const {
  if (d.magic != CONFIG_MAGIC) return false;
  if (d.version != CONFIG_VERSION) return false;
  if (d.refreshHz < 5 || d.refreshHz > 43) return false;
  for (uint8_t i = 0; i < board::NUM_OUTPUTS; i++) {
    if (d.route[i] & 0xF0) return false;
  }
  return d.checksum == checksum(d);
}

void RouterConfig::reset() {
  memset(&_data, 0, sizeof(_data));
  // 4-way splitter: four outputs per input.
  for (uint8_t out = 0; out < board::NUM_OUTPUTS; out++) {
    _data.route[out] = (uint8_t)(1u << (out / 4));
  }
  _data.refreshHz = 40;
  _data.rdmEnabled = 1;
  sanitize();
}

void RouterConfig::begin() {
  reset();

  _fsReady = LittleFS.begin();
  if (!_fsReady) {
    Serial.println("[cfg] LittleFS unavailable - using defaults (not persisted)");
    return;
  }

  File f = LittleFS.open(CONFIG_FILE, "r");
  if (!f) {
    Serial.println("[cfg] no /router.cfg - using defaults");
    return;
  }

  RouterConfigData tmp;
  size_t got = f.read(reinterpret_cast<uint8_t *>(&tmp), sizeof(tmp));
  f.close();

  if (got == sizeof(tmp) && valid(tmp)) {
    _data = tmp;
    Serial.println("[cfg] loaded /router.cfg");
  } else {
    Serial.println("[cfg] /router.cfg invalid - using defaults");
  }
}

bool RouterConfig::save() {
  if (!_fsReady) {
    _fsReady = LittleFS.begin();
    if (!_fsReady) return false;
  }

  sanitize();

  File f = LittleFS.open(CONFIG_FILE, "w");
  if (!f) return false;
  size_t put = f.write(reinterpret_cast<const uint8_t *>(&_data), sizeof(_data));
  f.close();
  return put == sizeof(_data);
}

uint8_t RouterConfig::route(uint8_t out) const {
  return (out < board::NUM_OUTPUTS) ? _data.route[out] : 0;
}

void RouterConfig::setRoute(uint8_t out, uint8_t mask, bool persist) {
  if (out >= board::NUM_OUTPUTS) return;
  _data.route[out] = (uint8_t)(mask & 0x0F);
  if (persist) save();
}

void RouterConfig::setAllRoutes(uint8_t mask, bool persist) {
  for (uint8_t i = 0; i < board::NUM_OUTPUTS; i++) {
    _data.route[i] = (uint8_t)(mask & 0x0F);
  }
  if (persist) save();
}

uint8_t RouterConfig::refreshHz() const { return _data.refreshHz; }

void RouterConfig::setRefreshHz(uint8_t hz) {
  if (hz < 5) hz = 5;
  if (hz > 43) hz = 43;
  _data.refreshHz = hz;
}

bool RouterConfig::rdmEnabled() const { return _data.rdmEnabled != 0; }

void RouterConfig::setRdmEnabled(bool on) { _data.rdmEnabled = on ? 1 : 0; }
