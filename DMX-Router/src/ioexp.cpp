#include "ioexp.h"

#include <Wire.h>

#include "board.h"

namespace ioexp {
namespace {

uint8_t s_addr = board::UI_PCF_ADDR;
bool s_ready = false;
uint8_t s_latch = 0xF1;  // P0=1 (transmit), P1..P3=0, P4..P7=1

}  // namespace

bool begin(uint8_t addr) {
  s_addr = addr;
  s_ready = false;

  // All pins high: P0..P3 idle high, P4..P7 inputs with weak pull-ups.
  Wire1.beginTransmission(s_addr);
  Wire1.write((uint8_t)0xFF);
  if (Wire1.endTransmission() != 0) return false;

  // A read is the cheapest way to distinguish a PCF8574 from a device
  // that merely acknowledges its address.
  if (Wire1.requestFrom(s_addr, (uint8_t)1) != 1) return false;
  while (Wire1.available()) (void)Wire1.read();

  s_ready = true;
  s_latch = 0xF1;  // DIR_OUT high, mux channel 0, buttons released
  write(s_latch);
  return true;
}

bool present() { return s_ready; }

bool write(uint8_t value) {
  s_latch = (uint8_t)(value | 0xF0);  // keep P4..P7 as inputs
  if (!s_ready) return false;

  Wire1.beginTransmission(s_addr);
  Wire1.write(s_latch);
  return Wire1.endTransmission() == 0;
}

uint8_t read() {
  if (!s_ready) return 0xFF;
  if (Wire1.requestFrom(s_addr, (uint8_t)1) != 1) return 0xFF;
  if (!Wire1.available()) return 0xFF;
  return (uint8_t)Wire1.read();
}

void setDirOut(bool transmit) {
  if (transmit) {
    s_latch |= (uint8_t)(1u << board::PCF_DIR_OUT);
  } else {
    s_latch &= (uint8_t)~(1u << board::PCF_DIR_OUT);
  }
  write(s_latch);
}

void selectMux(uint8_t channel) {
  channel &= 0x07;
  s_latch &= (uint8_t)~0x0E;
  s_latch |= (uint8_t)(channel << 1);
  write(s_latch);
}

uint8_t buttons() {
  uint8_t raw = read();
  // P4..P7 are active-low; return them as bits 0..3, active-high.
  return (uint8_t)(((~raw) >> 4) & 0x0F);
}

}  // namespace ioexp
