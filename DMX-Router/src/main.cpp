// ------------------------------------------------------------------
//  DMX-Router 4 -> 16
//
//  4 DMX/RDM inputs, 16 DMX outputs, HTP merge, best-effort RDM
//  relay, PCF8574 for direction/mux/buttons, optional SSD1306 UI and
//  a USB serial configuration console.
//
//  Hardware and pin map: see README.md and board.h.
// ------------------------------------------------------------------

#include <Arduino.h>
#include <RP2040Support.h>
#include <Wire.h>
#include <cstdlib>
#include <cstring>

#include "board.h"
#include "config.h"
#include "dmx_in.h"
#include "dmx_out.h"
#include "ioexp.h"
#include "rdm.h"
#include "router.h"
#include "ui.h"

// --- Global objects ------------------------------------------------
static DmxInput inputs[board::NUM_INPUTS];
static DmxInput muxInputs[2];  // 74HC151 returns: OUT 1..8 / OUT 9..16
static DmxTxGroup txGroups[2];
static DmxRouter router;
static RouterUi ui;

static bool s_hardwareOk = true;
static uint32_t s_lastRefreshMs = 0;
static uint32_t s_lastRebuildMs = 0;

// --- Serial console ------------------------------------------------
static char s_line[96];
static uint8_t s_lineLen = 0;
static uint32_t s_lastInputMs = 0;

// ------------------------------------------------------------------
//  Output frame scheduling
// ------------------------------------------------------------------

// Start both eight-port groups.  They share the same PIO clock and are
// started back-to-back, so their breaks are aligned.  Returns false if
// the previous frame is still on the wire.
static bool sendOutputFrames() {
  if (txGroups[0].busy() || txGroups[1].busy()) return false;

  const uint8_t *frames[8];
  uint16_t lens[8];

  for (uint8_t i = 0; i < 8; i++) {
    frames[i] = router.frame(i);
    lens[i] = board::DMX_FRAME;
  }
  bool okLow = txGroups[0].write(frames, lens);

  for (uint8_t i = 0; i < 8; i++) {
    frames[i] = router.frame(8 + i);
    lens[i] = board::DMX_FRAME;
  }
  bool okHigh = txGroups[1].write(frames, lens);

  return okLow && okHigh;
}

// ------------------------------------------------------------------
//  Serial console helpers
// ------------------------------------------------------------------

static void formatMask(uint8_t mask, char *buf, size_t len) {
  mask &= 0x0F;
  if (mask == 0) {
    snprintf(buf, len, "HOLD");
    return;
  }
  size_t o = 0;
  for (uint8_t i = 0; i < 4 && o + 4 < len; i++) {
    if (!(mask & (1u << i))) continue;
    if (o) buf[o++] = '+';
    buf[o++] = (char)('1' + i);
  }
  buf[o] = 0;
}

static void printRoutes() {
  char maskA[16];
  char maskB[16];
  Serial.println("out  route     out  route");
  for (uint8_t row = 0; row < 8; row++) {
    uint8_t a = row;
    uint8_t b = (uint8_t)(row + 8);
    formatMask(config.route(a), maskA, sizeof(maskA));
    formatMask(config.route(b), maskB, sizeof(maskB));
    Serial.printf("%2u   %-8s  %2u   %-8s\n", a + 1, maskA, b + 1, maskB);
  }
}

static void printStatus() {
  uint32_t now = millis();

  Serial.println();
  Serial.println("=== DMX-Router 4>16 status ===");
  Serial.printf("uptime %lu s   refresh %u Hz   rdm %s\n",
                (unsigned long)(now / 1000), config.refreshHz(),
                config.rdmEnabled() ? "on" : "off");
  Serial.printf("router frames %lu   rdm req %lu  resp %lu  timeout %lu  invalid %lu\n",
                (unsigned long)router.frames(), (unsigned long)rdm.requests(),
                (unsigned long)rdm.responses(), (unsigned long)rdm.timeouts(),
                (unsigned long)rdm.invalid());
  Serial.printf("rdm engine: %s\n", rdm.status());

  Serial.println("in  active frames breaks  age");
  for (uint8_t i = 0; i < board::NUM_INPUTS; i++) {
    bool act = inputs[i].active(now) && inputs[i].startCode() == 0;
    if (inputs[i].frames()) {
      Serial.printf("%2u  %-6s %6lu %6lu  %4lu ms\n", i + 1,
                    act ? "yes" : "no", (unsigned long)inputs[i].frames(),
                    (unsigned long)inputs[i].breaks(),
                    (unsigned long)(now - inputs[i].lastFrameMs()));
    } else {
      Serial.printf("%2u  %-6s %6lu %6lu    ---\n", i + 1, "no", 0UL, 0UL);
    }
  }

  printRoutes();
  Serial.println();
}

static bool parseMaskArg(const char *arg, uint8_t &mask) {
  if (!arg || !*arg) return false;

  if (!strcmp(arg, "hold") || !strcmp(arg, "off") || !strcmp(arg, "none")) {
    mask = 0;
    return true;
  }

  // A plain number is the 4-bit input mask: 0..15, hex 0x0F, ...
  if (!strchr(arg, '+') && !strchr(arg, ',')) {
    char *end = nullptr;
    unsigned long v = strtoul(arg, &end, 0);
    if (end == arg || *end != 0 || v > 15) return false;
    mask = (uint8_t)v;
    return true;
  }

  // "1+3" style input list.
  uint8_t m = 0;
  const char *p = arg;
  bool any = false;
  while (*p) {
    while (*p == '+' || *p == ',') p++;
    if (!*p) break;
    char *end = nullptr;
    unsigned long v = strtoul(p, &end, 0);
    if (end == p || v < 1 || v > 4) return false;
    m |= (uint8_t)(1u << (v - 1));
    any = true;
    p = end;
    if (*p && *p != '+' && *p != ',') return false;
  }
  if (!any) return false;
  mask = m;
  return true;
}

static int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static bool parseHexBytes(const char *s, uint8_t *out, uint16_t max,
                          uint16_t &len) {
  len = 0;
  int hi = -1;
  for (const char *p = s; *p; p++) {
    if (*p == ' ' || *p == '\t' || *p == ':' || *p == '-' || *p == ',') {
      continue;
    }
    int n = hexNibble(*p);
    if (n < 0) return false;
    if (hi < 0) {
      hi = n;
    } else {
      if (len >= max) return false;
      out[len++] = (uint8_t)((hi << 4) | n);
      hi = -1;
    }
  }
  return hi < 0 && len > 0;
}

static void printHelp() {
  Serial.println();
  Serial.println("commands:");
  Serial.println("  help                     this text");
  Serial.println("  status                   counters, inputs, routing");
  Serial.println("  route                    show routing table");
  Serial.println("  route <out>              show one output (1..16)");
  Serial.println("  route <out> <mask>       set one output");
  Serial.println("  route all <mask>         set all outputs");
  Serial.println("  refresh <hz>             refresh rate 5..43 Hz");
  Serial.println("  rdm [status]             show RDM state");
  Serial.println("  rdm on | off             enable/disable RDM relay");
  Serial.println("  rdm test <out> <hex>     send a raw RDM wire frame");
  Serial.println("  save                     write /router.cfg");
  Serial.println("  defaults                 restore defaults and save");
  Serial.println("  reboot                   restart the RP2040");
  Serial.println();
  Serial.println("mask: 0=hold, 1..15=bit mask (IN1=bit0), or 1+2+4 style list");
  Serial.println("example: route 5 1+2   -> OUT5 = HTP(IN1, IN2)");
  Serial.println();
}

static void handleRdmCommand(char *save) {
  char *sub = strtok_r(nullptr, " \t", &save);
  if (!sub || !strcmp(sub, "status")) {
    Serial.printf("RDM %s, engine: %s\n", config.rdmEnabled() ? "on" : "off",
                  rdm.status());
    return;
  }

  if (!strcmp(sub, "on")) {
    config.setRdmEnabled(true);
    rdm.enable(true);
    Serial.println("RDM enabled (use 'save' to persist)");
    return;
  }
  if (!strcmp(sub, "off")) {
    config.setRdmEnabled(false);
    rdm.enable(false);
    Serial.println("RDM disabled (use 'save' to persist)");
    return;
  }

  if (!strcmp(sub, "test")) {
    char *outArg = strtok_r(nullptr, " \t", &save);
    if (!outArg) {
      Serial.println("usage: rdm test <out> <hex bytes>");
      return;
    }
    char *end = nullptr;
    unsigned long out = strtoul(outArg, &end, 0);
    if (end == outArg || *end || out < 1 || out > board::NUM_OUTPUTS) {
      Serial.println("invalid output (1..16)");
      return;
    }

    // Collect every remaining token into one hex string; spaces and
    // common separators are ignored by parseHexBytes().
    //
    // Static (not on the stack): the RP2040 core-0 stack is only 2 KB
    // in this core, and hex + wire would together use over 1 KB.
    static char hex[3 * DmxInput::RDM_MAX];
    size_t h = 0;
    char *tok;
    while ((tok = strtok_r(nullptr, " \t", &save)) != nullptr) {
      size_t n = strlen(tok);
      if (h + n + 1 >= sizeof(hex)) {
        Serial.println("hex string too long");
        return;
      }
      memcpy(hex + h, tok, n);
      h += n;
      hex[h] = 0;
    }
    if (h == 0) {
      Serial.println("usage: rdm test <out> <hex bytes>");
      return;
    }

    static uint8_t wire[DmxInput::RDM_MAX];
    uint16_t len = 0;
    if (!parseHexBytes(hex, wire, sizeof(wire), len)) {
      Serial.println("invalid hex string");
      return;
    }
    if (!rdmValidate(wire, len)) {
      Serial.println("frame is not a valid RDM message (checksum?)");
      return;
    }
    if (rdm.sendTest((uint8_t)(out - 1), wire, len)) {
      Serial.printf("RDM test frame queued for OUT%lu\n", out);
    } else {
      Serial.println("RDM engine busy");
    }
    return;
  }

  Serial.println("usage: rdm [status|on|off|test <out> <hex>]");
}

static void handleCommand(char *line) {
  char *save = nullptr;
  char *cmd = strtok_r(line, " \t", &save);
  if (!cmd) return;

  if (!strcmp(cmd, "help") || !strcmp(cmd, "?")) {
    printHelp();
    return;
  }

  if (!strcmp(cmd, "status") || !strcmp(cmd, "stat")) {
    printStatus();
    return;
  }

  if (!strcmp(cmd, "route")) {
    char *arg1 = strtok_r(nullptr, " \t", &save);
    if (!arg1) {
      printRoutes();
      return;
    }

    if (!strcmp(arg1, "all")) {
      char *maskArg = strtok_r(nullptr, " \t", &save);
      uint8_t mask = 0;
      if (!maskArg || !parseMaskArg(maskArg, mask)) {
        Serial.println("usage: route all <mask>");
        return;
      }
      config.setAllRoutes(mask);
      char buf[16];
      formatMask(mask, buf, sizeof(buf));
      Serial.printf("all outputs -> %s (use 'save' to persist)\n", buf);
      return;
    }

    char *end = nullptr;
    unsigned long out = strtoul(arg1, &end, 0);
    if (end == arg1 || *end || out < 1 || out > board::NUM_OUTPUTS) {
      Serial.println("invalid output (1..16)");
      return;
    }

    char *maskArg = strtok_r(nullptr, " \t", &save);
    if (!maskArg) {
      char buf[16];
      formatMask(config.route((uint8_t)(out - 1)), buf, sizeof(buf));
      Serial.printf("OUT%lu -> %s\n", out, buf);
      return;
    }

    uint8_t mask = 0;
    if (!parseMaskArg(maskArg, mask)) {
      Serial.println("invalid mask (0..15, hold, or 1+2+3+4)");
      return;
    }
    config.setRoute((uint8_t)(out - 1), mask);
    char buf[16];
    formatMask(mask, buf, sizeof(buf));
    Serial.printf("OUT%lu -> %s (use 'save' to persist)\n", out, buf);
    return;
  }

  if (!strcmp(cmd, "refresh")) {
    char *arg = strtok_r(nullptr, " \t", &save);
    if (!arg) {
      Serial.printf("refresh %u Hz\n", config.refreshHz());
      return;
    }
    char *end = nullptr;
    unsigned long hz = strtoul(arg, &end, 0);
    if (end == arg || *end || hz < 5 || hz > 43) {
      Serial.println("usage: refresh <5..43>");
      return;
    }
    config.setRefreshHz((uint8_t)hz);
    Serial.printf("refresh -> %lu Hz (use 'save' to persist)\n", hz);
    return;
  }

  if (!strcmp(cmd, "rdm")) {
    handleRdmCommand(save);
    return;
  }

  if (!strcmp(cmd, "save")) {
    if (config.save()) {
      Serial.println("configuration saved to /router.cfg");
    } else {
      Serial.println("save failed (LittleFS unavailable?)");
    }
    return;
  }

  if (!strcmp(cmd, "defaults")) {
    config.reset();
    rdm.enable(config.rdmEnabled());
    bool ok = config.save();
    Serial.printf("defaults restored%s\n", ok ? " and saved" : " (not saved)");
    return;
  }

  if (!strcmp(cmd, "reboot")) {
    Serial.println("rebooting...");
    Serial.flush();
    delay(50);
    rp2040.reboot();
    return;
  }

  Serial.printf("unknown command: %s (try 'help')\n", cmd);
}

static void pollSerial() {
  bool received = false;
  while (Serial.available() > 0) {
    int c = Serial.read();
    if (c < 0) break;
    received = true;
    if (c == '\r' || c == '\n') {
      if (s_lineLen) {
        s_line[s_lineLen] = 0;
        s_lineLen = 0;
        handleCommand(s_line);
      }
    } else if (c == 8 || c == 127) {
      if (s_lineLen) s_lineLen--;
    } else if (s_lineLen + 1 < sizeof(s_line)) {
      s_line[s_lineLen++] = (char)c;
    }
  }
  if (received) s_lastInputMs = millis();

  // Some serial monitors (including the VS Code Serial Monitor with
  // "line ending = none") send the text without CR/LF.  Execute the
  // buffered line after a short idle period as a fallback.
  if (s_lineLen && (uint32_t)(millis() - s_lastInputMs) >= 500) {
    s_line[s_lineLen] = 0;
    s_lineLen = 0;
    handleCommand(s_line);
  }
}

// ------------------------------------------------------------------
//  Setup / loop
// ------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && (uint32_t)(millis() - t0) < 1500) delay(10);

  Serial.println();
  Serial.println("===========================================");
  Serial.println(" DMX-Router 4>16  (RP2040 / Raspberry Pi Pico)");
  Serial.println(" 4 DMX/RDM in, 16 DMX out, HTP merge, RDM relay");
  Serial.println("===========================================");

  config.begin();
  router.begin();

  // UART1 is the shared input-card transmitter (RDM responses).
  // Set the pins BEFORE begin(): the default RX (GP5) is OUT6 here.
  Serial2.setTX(board::TX_IN);
  Serial2.setRX(255);  // UART_PIN_NOT_DEFINED, nothing to receive
  Serial2.begin(250000, SERIAL_8N2);

  // I2C1 for the PCF8574 and the optional SSD1306.  GP26/27 are the
  // only complete I2C pair that is still free after the 16 outputs,
  // the four inputs and the two RDM return pins.  On the Pico variant
  // `Wire` is I2C0 (GP4/GP5), so the I2C1 instance `Wire1` is used.
  Wire1.setSDA(board::UI_SDA);
  Wire1.setSCL(board::UI_SCL);
  Wire1.begin();
  Wire1.setClock(400000);

  if (!ioexp::begin(board::UI_PCF_ADDR)) {
    Serial.println("[err] PCF8574 not found - DIR_OUT/MUX are fixed");
    s_hardwareOk = false;
  }

  // Safe bus state before the PIO takes over: input cards receive,
  // output cards transmit an idle mark, multiplexers select channel 0.
  pinMode(board::DIR_IN, OUTPUT);
  digitalWrite(board::DIR_IN, LOW);
  ioexp::setDirOut(true);
  ioexp::selectMux(0);
  for (uint8_t pin = 0; pin < 16; pin++) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
  }

  // The RP2040 has two PIO blocks with four state machines each.
  // Distribute the eight state machines evenly:
  //   PIO0: 2 x TX groups, IN1, IN2
  //   PIO1: IN3, IN4, 2 x RDM multiplexer RX
  // No block is asked for more than its four SMs.
  for (uint8_t g = 0; g < 2; g++) {
    uint8_t base = g ? board::TX_GROUP1_PIN : board::TX_GROUP0_PIN;
    if (!txGroups[g].begin(pio0, base)) {
      Serial.printf("[err] TX group %u (GP%u) init failed\n", g, base);
      s_hardwareOk = false;
    }
  }

  for (uint8_t i = 0; i < board::NUM_INPUTS; i++) {
    PIO pio = (i < 2) ? pio0 : pio1;
    if (!inputs[i].begin(pio, board::RX_IN[i], i)) {
      Serial.printf("[err] input %u init failed\n", i + 1);
      s_hardwareOk = false;
    }
  }

  for (uint8_t m = 0; m < 2; m++) {
    if (!muxInputs[m].begin(pio1, board::MUX_Y[m], (uint8_t)(8 + m))) {
      Serial.printf("[err] RDM mux %u init failed\n", m);
      s_hardwareOk = false;
    }
  }

  rdm.begin(&txGroups[0], &txGroups[1], &muxInputs[0], &muxInputs[1]);
  ui.begin();

  if (!s_hardwareOk) {
    Serial.println("[warn] not all PIO resources could be claimed");
  }

  // First frame: outputs start at all-zero (safe blackout) and are then
  // merged as soon as the consoles deliver data.
  sendOutputFrames();
  s_lastRefreshMs = millis();
  s_lastRebuildMs = s_lastRefreshMs;

  Serial.printf("ready: %u Hz, %u inputs, %u outputs\n", config.refreshHz(),
                board::NUM_INPUTS, board::NUM_OUTPUTS);
  Serial.println("type 'help' for the serial console");
}

void loop() {
  uint32_t now = millis();

  // 1. Drain every receiver as often as possible (8-word FIFOs).
  for (uint8_t i = 0; i < board::NUM_INPUTS; i++) {
    inputs[i].poll();
    if (inputs[i].takeFrame()) {
      router.onFrame(inputs, board::NUM_INPUTS, i);
    }
  }

  // The multiplexed RDM returns echo our own DMX while transmitting.
  // They are polled here too so their FIFOs cannot overflow when the
  // RDM engine is idle.
  muxInputs[0].poll();
  muxInputs[1].poll();

  // 2. Age out missing inputs and pick up configuration changes.
  if ((uint32_t)(now - s_lastRebuildMs) >= 200) {
    router.rebuild(inputs, board::NUM_INPUTS);
    s_lastRebuildMs = now;
  }

  // 3. RDM relay.  While it owns the output bus, no DMX frame is
  //    started (DIR_OUT is switched to receive).
  bool rdmBusy = rdm.service(inputs, board::NUM_INPUTS);

  // 4. Periodic DMX refresh.
  if (!rdmBusy) {
    uint32_t interval = 1000u / config.refreshHz();
    // One frame takes ~22.8 ms, so never schedule faster than that.
    if (interval < 23) interval = 23;
    if ((uint32_t)(now - s_lastRefreshMs) >= interval) {
      if (sendOutputFrames()) s_lastRefreshMs = millis();
    }
  }

  // 5. Optional local display and buttons (I2C, non-blocking).
  ui.update(inputs, board::NUM_INPUTS);

  // 6. USB serial console.
  pollSerial();
}
