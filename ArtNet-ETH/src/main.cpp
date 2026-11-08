#include <Arduino.h>
#include <Wire.h>

#include <cstdio>
#include <cstring>

// Real Ethernet on the W5100S-EVB-Pico.  The on-board W5100S is driven
// through SPI0 by the lwIP W5100 driver (not the blocking
// ArduinoWiznet5100lwIP wrapper, so DHCP can time out into a fallback).
#include <W5100lwIP.h>
// lwIP UDP sockets (Art-Net) and HTTP server on top of the same netif.
#include <WiFiUdp.h>
#include <WebServer.h>
// DMX512 transmitter with spec-compliant PIO/DMA timing.
#include "DmxOut.h"
// RDM gateway: RS485 direction control + ArtRdm bridge.
#include "artnet_rdm.h"
// Persisted Art-Net mapping and network configuration.
#include "artnet_config.h"
// Embedded single-page web UI (channels + settings).
#include "index_html.h"
// Board pin map, shared runtime state, OLED/keys UI.
#include "board.h"
#include "node.h"
#include "ui.h"

// ------------------------------------------------------------------
// DMX / RDM ports.  DmxOut uses pio0 (one state machine per port), the
// RDM/DMX receiver uses pio1 (one state machine per port).
// ------------------------------------------------------------------
static const uint8_t NUM_UNIVERSES = board::NUM_PORTS;
static DmxOut dmx[board::NUM_PORTS];
// [start code 0x00] + 512 channels
static uint8_t dmx_buffer[board::NUM_PORTS][DMX_UNIVERSE_SIZE + 1];

static RdmPort rdm_port[board::NUM_PORTS];
static ArtnetRdm *artnet_rdm = nullptr;

// ------------------------------------------------------------------
// Ethernet.  GP17 = CS, GP21 = W5100S interrupt (hard-wired on the
// board).  SPI0 is the default Arduino SPI bus.
// ------------------------------------------------------------------
static Wiznet5100lwIP Ethernet(board::ETH_CS, SPI, board::ETH_IRQ);

static WiFiUDP artnet_udp;
static WebServer http(80);

static const uint16_t OP_POLL      = 0x2000;
static const uint16_t OP_POLLREPLY = 0x2100;
static const uint16_t OP_DMX       = 0x5000;
static const uint16_t OP_ADDRESS   = 0x6000;
static const uint16_t OP_RDM       = 0x00CC;

// Art-Net node info for ArtPollReply.
static const uint16_t ARTNET_PORT_STYLE_DMX = 0x88;  // output + RDM
static const uint32_t ARTNET_OEM            = 0x1234;
static const uint16_t ARTNET_ESTA           = 0x0000;

// ------------------------------------------------------------------
// Runtime state
// ------------------------------------------------------------------
NodeRuntime node = {};

static bool net_ready = false;
static bool services_started = false;
static bool dhcp_lease_active = false;

static uint8_t our_mac[6] = {0};
static uint32_t last_announce = 0;

static volatile uint32_t art_fps = 0;
static uint32_t fps_cnt = 0;
static uint32_t fps_base_ms = 0;

// Per-port last ArtDmx / DMX-in frame timestamps.
static uint32_t last_art[board::NUM_PORTS] = {0};
static uint32_t last_in[board::NUM_PORTS]  = {0};

// DMX input framing state: 0 = wait for BREAK, 1 = start code, 2 = data.
static uint8_t  in_seq[board::NUM_PORTS]   = {0};
static uint8_t  in_state[board::NUM_PORTS] = {0};
static uint16_t in_idx[board::NUM_PORTS]   = {0};

static uint32_t led_off_at = 0;

// ------------------------------------------------------------------
// Helpers
// ------------------------------------------------------------------
static bool inputActive(uint8_t p) {
  return (millis() - last_in[p]) < board::INPUT_TIMEOUT_MS;
}

static void resetInputState(uint8_t p) {
  last_in[p] = 0;
  in_state[p] = 0;
  in_idx[p] = 0;
}

// Directed broadcast of the currently active interface (DHCP lease or
// static fallback).  Broadcasts must follow the real netmask, not the
// stored fallback.
static IPAddress currentBroadcast() {
  IPAddress ip = Ethernet.localIP();
  IPAddress mask = Ethernet.subnetMask();
  if (ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0) {
    ip = config.ipAddr();
    mask = config.maskAddr();
  }
  if (mask[0] == 0 && mask[1] == 0 && mask[2] == 0 && mask[3] == 0) {
    mask = config.maskAddr();
  }
  return IPAddress((uint8_t)(ip[0] | ~mask[0]),
                   (uint8_t)(ip[1] | ~mask[1]),
                   (uint8_t)(ip[2] | ~mask[2]),
                   (uint8_t)(ip[3] | ~mask[3]));
}

static IPAddress currentIP() {
  IPAddress ip = Ethernet.localIP();
  if (ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0) {
    ip = config.ipAddr();
  }
  return ip;
}

static IPAddress currentMask() {
  IPAddress mask = Ethernet.subnetMask();
  if (mask[0] == 0 && mask[1] == 0 && mask[2] == 0 && mask[3] == 0) {
    mask = config.maskAddr();
  }
  return mask;
}

// Stable, locally administered MAC derived from the RP2040 unique ID.
// Multiple nodes therefore never share a MAC address.
static void buildMac(uint8_t mac[6]) {
  const char *id = rp2040.getChipID();
  uint32_t h = 2166136261u;
  for (const char *p = id; p && *p; p++) {
    h ^= (uint8_t)*p;
    h *= 16777619u;
  }
  mac[0] = 0x02;                 // locally administered, unicast
  mac[1] = 0x4D;                 // 'M'
  mac[2] = 0x51;                 // 'Q'
  mac[3] = (uint8_t)(h >> 16);
  mac[4] = (uint8_t)(h >> 8);
  mac[5] = (uint8_t)h;
}

// ------------------------------------------------------------------
// ArtPollReply
// ------------------------------------------------------------------
static void buildPollReply(uint8_t *p) {
  memset(p, 0, 239);

  memcpy(p, "Art-Net", 7);
  p[7] = 0x00;

  p[8]  = OP_POLLREPLY & 0xFF;
  p[9]  = (OP_POLLREPLY >> 8) & 0xFF;

  IPAddress ip = currentIP();
  p[10] = ip[0];
  p[11] = ip[1];
  p[12] = ip[2];
  p[13] = ip[3];

  p[14] = board::ARTNET_UDP_PORT & 0xFF;
  p[15] = (board::ARTNET_UDP_PORT >> 8) & 0xFF;

  // VersInfo 1.0
  p[16] = 0x00;
  p[17] = 0x01;

  // The node currently advertises one net/subnet for all four ports
  // through its port table below.  These two bytes mirror the first
  // port for controllers that only look at the node-level fields.
  p[18] = config.net[0] & 0x7F;
  p[19] = config.subnet[0] & 0x0F;

  p[20] = ARTNET_OEM & 0xFF;
  p[21] = (ARTNET_OEM >> 8) & 0xFF;

  p[22] = 0x00;   // UbeaVersion
  p[23] = 0x02;   // Status1: RDM capable

  p[24] = ARTNET_ESTA & 0xFF;
  p[25] = (ARTNET_ESTA >> 8) & 0xFF;

  const char *shortName = config.short_name;
  if (shortName[0] == '\0') shortName = "ArtNet-ETH";
  strncpy((char *)&p[26], shortName, 17);
  p[26 + 17] = 0x00;

  const char *longName = config.long_name;
  if (longName[0] == '\0') longName = "ArtNet-ETH 4xDMX node";
  strncpy((char *)&p[44], longName, 63);
  p[44 + 63] = 0x00;

  // NodeReport (108..171) left zeroed.

  p[172] = 0x00;
  p[173] = NUM_UNIVERSES;

  for (uint8_t i = 0; i < NUM_UNIVERSES; i++) {
    bool in = config.isInput(i);
    uint8_t sw = (uint8_t)((config.subnet[i] << 4) | config.universe[i]);
    p[174 + i] = in ? 0x40 : ARTNET_PORT_STYLE_DMX;
    p[178 + i] = in ? (inputActive(i) ? 0x80 : 0x00) : 0x00;
    p[182 + i] = in ? 0x00 : 0x80;
    p[186 + i] = in ? sw : 0x80;
    p[190 + i] = in ? 0x00 : sw;
  }

  p[194] = 0x00;   // SwVideo
  p[195] = 0x00;   // SwMacro
  p[196] = 0x00;   // SwRemote
  p[200] = 0x00;   // Style: StNode

  memcpy(&p[201], our_mac, 6);
  p[207] = 0x00;   // BindIndex
  p[208] = 0x00;   // Status2
}

static void sendPollReply(IPAddress dest) {
  if (dest[0] == 0 && dest[1] == 0 && dest[2] == 0 && dest[3] == 0) return;
  uint8_t reply[239];
  buildPollReply(reply);

  artnet_udp.beginPacket(dest, board::ARTNET_UDP_PORT);
  artnet_udp.write(reply, sizeof(reply));
  artnet_udp.endPacket();
}

static void handlePoll(IPAddress src) {
  sendPollReply(src);
  sendPollReply(currentBroadcast());
}

// ------------------------------------------------------------------
// ArtAddress (remote programming)
// ------------------------------------------------------------------
static void handleAddress(const uint8_t *pkt, int n, IPAddress src) {
  if (pkt[10] != 0x00 || pkt[11] != 0x0E) return;
  if (n < 30) return;

  bool changed = false;

  if ((pkt[12] & 0x80) && (pkt[12] & 0x7F) != 0x7F) {
    uint8_t net = pkt[12] & 0x7F;
    for (int i = 0; i < NUM_UNIVERSES; i++) {
      if (config.net[i] != net) changed = true;
      config.net[i] = net;
    }
  }

  if (n >= 105 && (pkt[104] & 0x80) && (pkt[104] & 0x0F) != 0x0F) {
    uint8_t sub = pkt[104] & 0x0F;
    for (int i = 0; i < NUM_UNIVERSES; i++) {
      if (config.subnet[i] != sub) changed = true;
      config.subnet[i] = sub;
    }
  }

  if (n >= 104) {
    for (int i = 0; i < NUM_UNIVERSES; i++) {
      uint8_t sw = config.isInput(i) ? pkt[96 + i] : pkt[100 + i];
      if (sw & 0x80) {
        uint8_t uni = sw & 0x0F;
        if (config.universe[i] != uni) {
          config.universe[i] = uni;
          changed = true;
        }
      }
    }
  }

  if (n >= 32 && pkt[14] != 0) {
    size_t len = strnlen((const char *)&pkt[14], 17);
    if (memcmp(pkt + 14, config.short_name, len) != 0 ||
        config.short_name[len] != 0) {
      memcpy(config.short_name, &pkt[14], len);
      config.short_name[len] = '\0';
      changed = true;
    }
  }
  if (n >= 96 && pkt[32] != 0) {
    size_t len = strnlen((const char *)&pkt[32], 63);
    if (memcmp(pkt + 32, config.long_name, len) != 0 ||
        config.long_name[len] != 0) {
      memcpy(config.long_name, &pkt[32], len);
      config.long_name[len] = '\0';
      changed = true;
    }
  }

  // Command byte: AcDirectionTx0..3 = output, AcDirectionRx0..3 = input.
  if (n >= 107) {
    uint8_t cmd = pkt[106];
    if (cmd >= 0x20 && cmd < 0x20 + NUM_UNIVERSES) {
      uint8_t port = cmd - 0x20;
      if (config.isInput(port)) {
        config.direction[port] = PORT_OUTPUT;
        nodeApplyDirection(port);
        changed = true;
      }
    } else if (cmd >= 0x30 && cmd < 0x30 + NUM_UNIVERSES) {
      uint8_t port = cmd - 0x30;
      if (!config.isInput(port)) {
        config.direction[port] = PORT_INPUT;
        nodeApplyDirection(port);
        changed = true;
      }
    }
  }

  if (changed) config.save();
  sendPollReply(src);
}

// ------------------------------------------------------------------
// DMX input -> ArtDmx
// ------------------------------------------------------------------
static void sendArtDmx(uint8_t port) {
  // The UDP socket only exists once the network bring-up succeeded.
  if (!net_ready) return;

  uint8_t pkt[18 + DMX_UNIVERSE_SIZE];
  memcpy(pkt, "Art-Net", 8);
  pkt[8]  = OP_DMX & 0xFF;
  pkt[9]  = (OP_DMX >> 8) & 0xFF;
  pkt[10] = 0x00;
  pkt[11] = 0x0E;
  pkt[12] = in_seq[port]++;
  pkt[13] = 0x00;
  pkt[14] = (uint8_t)((config.subnet[port] << 4) | config.universe[port]);
  pkt[15] = config.net[port] & 0x7F;
  pkt[16] = (DMX_UNIVERSE_SIZE >> 8) & 0xFF;
  pkt[17] = DMX_UNIVERSE_SIZE & 0xFF;
  memcpy(pkt + 18, &dmx_buffer[port][1], DMX_UNIVERSE_SIZE);

  artnet_udp.beginPacket(currentBroadcast(), board::ARTNET_UDP_PORT);
  artnet_udp.write(pkt, sizeof(pkt));
  artnet_udp.endPacket();
}

static void serviceDmxInput() {
  uint32_t now = millis();
  for (int i = 0; i < NUM_UNIVERSES; i++) {
    if (!config.isInput(i)) continue;
    while (rdm_port[i].available()) {
      int b = rdm_port[i].readByte();
      if (b < 0) break;
      switch (in_state[i]) {
        case 0:   // BREAK: a long low period appears as a single 0x00
          if (b == 0x00) in_state[i] = 1;
          break;
        case 1:   // DMX start code
          if (b == 0x00) {
            memset(&dmx_buffer[i][1], 0, DMX_UNIVERSE_SIZE);
            in_idx[i] = 1;
            in_state[i] = 2;
          } else {
            in_state[i] = 0;
          }
          break;
        default:  // 512 data slots
          dmx_buffer[i][in_idx[i]++] = (uint8_t)b;
          if (in_idx[i] > DMX_UNIVERSE_SIZE) {
            dmx_buffer[i][0] = 0x00;
            last_in[i] = now;
            sendArtDmx(i);
            in_state[i] = 0;
          }
          break;
      }
    }
  }
}

// ------------------------------------------------------------------
// Art-Net receive path
// ------------------------------------------------------------------
static void readArtnet() {
  int packet_size = artnet_udp.parsePacket();
  if (packet_size < 14) return;

  IPAddress src = artnet_udp.remoteIP();

  uint8_t pkt[18 + DMX_UNIVERSE_SIZE + 8];
  int n = artnet_udp.read(pkt, sizeof(pkt));
  if (n < 14) return;

  if (memcmp(pkt, "Art-Net", 7) != 0 || pkt[7] != 0) return;

  digitalWrite(board::STATUS_LED, HIGH);
  led_off_at = millis() + 5;
  fps_cnt++;

  uint16_t opcode = (uint16_t)(pkt[9] << 8) | pkt[8];

  switch (opcode) {
    case OP_POLL:
      if (pkt[10] == 0x00 && pkt[11] == 0x0E) handlePoll(src);
      break;

    case OP_DMX: {
      if (n < 18) return;
      if (pkt[10] != 0x00 || pkt[11] != 0x0E) return;

      uint16_t addr_net = pkt[15] & 0x7F;
      uint8_t sub = (pkt[14] >> 4) & 0x0F;
      uint8_t uni = pkt[14] & 0x0F;
      uint16_t address = (uint16_t)((addr_net << 8) | (sub << 4) | uni);

      uint16_t length = (uint16_t)(pkt[17] << 8) | pkt[16];
      if (length == 0) return;
      if (length > DMX_UNIVERSE_SIZE) length = DMX_UNIVERSE_SIZE;

      int port = config.outputPortForAddress(address);
      if (port < 0) return;
      last_art[port] = millis();

      if (dmx[port].busy()) return;

      uint8_t *data = &pkt[18];
      int avail = n - 18;

      // Art-Net 4 senders include the DMX start code as Data[0]
      // (Length = 513).  MagicQ and several other consoles send only the
      // 512 channel bytes (Length = 512).  A data field longer than one
      // universe is unambiguous, so skip the start code in that case.
      if (avail >= DMX_UNIVERSE_SIZE + 1) {
        data++;
        avail--;
      }
      if (avail > DMX_UNIVERSE_SIZE) avail = DMX_UNIVERSE_SIZE;
      if (avail < 0) avail = 0;

      dmx_buffer[port][0] = 0x00;
      memcpy(&dmx_buffer[port][1], data, avail);
      // Controllers often send only the changed slots.  Pad the rest of
      // the full 512-slot frame with zero so no stale data is emitted.
      if (avail < DMX_UNIVERSE_SIZE) {
        memset(&dmx_buffer[port][1 + avail], 0,
               DMX_UNIVERSE_SIZE - avail);
      }

      dmx[port].write(dmx_buffer[port], DMX_UNIVERSE_SIZE + 1);
      break;
    }

    case OP_ADDRESS:
      handleAddress(pkt, n, src);
      break;

    case OP_RDM:
      if (artnet_rdm) artnet_rdm->handlePacket(pkt, n, src);
      break;

    default:
      break;
  }
}

// ------------------------------------------------------------------
// Web UI + JSON API
// ------------------------------------------------------------------
static void rebootNode() {
  Serial.println("Rebooting...");
  Serial.flush();
  delay(100);
  rp2040.reboot();
  while (1) {}
}

void nodeReboot() { rebootNode(); }

static bool parseIPv4(const String &s, uint8_t *out) {
  unsigned int o[4];
  if (sscanf(s.c_str(), "%u.%u.%u.%u", &o[0], &o[1], &o[2], &o[3]) != 4)
    return false;
  for (int i = 0; i < 4; i++) {
    if (o[i] > 255) return false;
    out[i] = (uint8_t)o[i];
  }
  return true;
}

static void webRoot() {
  http.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  http.send(200, "text/html", INDEX_HTML);
}

static String jsonEscape(const char *s) {
  String out;
  for (; *s; s++) {
    if (*s == '"' || *s == '\\') out += '\\';
    out += *s;
  }
  return out;
}

static String ipString(IPAddress a) {
  return String(a[0]) + "." + String(a[1]) + "." + String(a[2]) + "." +
         String(a[3]);
}

static void webStatus() {
  int port = http.arg("port").toInt();
  if (port < 0 || port >= NUM_UNIVERSES) port = 0;

  static uint32_t last_hash[NUM_UNIVERSES] = {0};
  uint32_t hash = 2166136261u;
  for (int c = 0; c <= DMX_UNIVERSE_SIZE; c++) {
    hash ^= dmx_buffer[port][c];
    hash *= 16777619u;
  }
  bool values_changed = (hash != last_hash[port]);
  last_hash[port] = hash;

  String j = "{\"ip\":\"" + ipString(currentIP()) + "\",";
  j.reserve(values_changed ? 4096 : 512);
  j += "\"mask\":\"" + ipString(currentMask()) + "\",";
  j += "\"static_ip\":\"" + ipString(config.ipAddr()) + "\",";
  j += "\"static_mask\":\"" + ipString(config.maskAddr()) + "\",";
  j += "\"dhcp\":" + String(config.dhcp_enabled ? "true" : "false") + ",";
  j += "\"dhcp_active\":" + String(dhcp_lease_active ? "true" : "false") + ",";
  j += "\"name\":\"" + jsonEscape(config.short_name) + "\",";
  j += "\"longname\":\"" + jsonEscape(config.long_name) + "\",";
  j += "\"ports\":" + String(NUM_UNIVERSES) + ",";
  j += "\"uptime_ms\":" + String(millis()) + ",";
  j += "\"artnet\":{\"fps\":" + String(art_fps) + ",";
  j += "\"connected\":" + String(art_fps > 0 ? "true" : "false") + "},";
  j += "\"net\":[";
  for (int i = 0; i < NUM_UNIVERSES; i++) {
    j += String(config.net[i]);
    if (i < NUM_UNIVERSES - 1) j += ",";
  }
  j += "],\"subnet\":[";
  for (int i = 0; i < NUM_UNIVERSES; i++) {
    j += String(config.subnet[i]);
    if (i < NUM_UNIVERSES - 1) j += ",";
  }
  j += "],\"universe\":[";
  for (int i = 0; i < NUM_UNIVERSES; i++) {
    j += String(config.universe[i]);
    if (i < NUM_UNIVERSES - 1) j += ",";
  }
  j += "],\"direction\":[";
  for (int i = 0; i < NUM_UNIVERSES; i++) {
    j += String((int)config.direction[i]);
    if (i < NUM_UNIVERSES - 1) j += ",";
  }
  j += "],\"input_active\":[";
  for (int i = 0; i < NUM_UNIVERSES; i++) {
    j += (config.isInput(i) && inputActive(i)) ? "true" : "false";
    if (i < NUM_UNIVERSES - 1) j += ",";
  }
  j += "],\"changed\":" + String(values_changed ? "true" : "false");
  if (values_changed) {
    j += ",\"values\":[0";
    for (int c = 1; c <= DMX_UNIVERSE_SIZE; c++) {
      j += ',';
      j += dmx_buffer[port][c];
    }
    j += "]";
  }
  j += "}";
  http.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  http.send(200, "application/json", j);
}

static void webGetConfig() {
  String j = "{\"ip\":\"" + ipString(config.ipAddr()) + "\",";
  j.reserve(512);
  j += "\"mask\":\"" + ipString(config.maskAddr()) + "\",";
  j += "\"dhcp\":" + String(config.dhcp_enabled ? "true" : "false") + ",\"net\":[";
  for (int i = 0; i < NUM_UNIVERSES; i++) {
    j += String(config.net[i]);
    if (i < NUM_UNIVERSES - 1) j += ",";
  }
  j += "],\"subnet\":[";
  for (int i = 0; i < NUM_UNIVERSES; i++) {
    j += String(config.subnet[i]);
    if (i < NUM_UNIVERSES - 1) j += ",";
  }
  j += "],\"universe\":[";
  for (int i = 0; i < NUM_UNIVERSES; i++) {
    j += String(config.universe[i]);
    if (i < NUM_UNIVERSES - 1) j += ",";
  }
  j += "],\"direction\":[";
  for (int i = 0; i < NUM_UNIVERSES; i++) {
    j += String((int)config.direction[i]);
    if (i < NUM_UNIVERSES - 1) j += ",";
  }
  j += "],\"name\":\"" + jsonEscape(config.short_name) + "\",";
  j += "\"longname\":\"" + jsonEscape(config.long_name) + "\"}";
  http.send(200, "application/json", j);
}

static void sendJsonError(const char *error) {
  http.send(400, "application/json",
            String("{\"ok\":false,\"error\":\"") + error + "\"}");
}

static void setConfigName(char *dst, size_t dst_sz, const String &value) {
  size_t n = (size_t)value.length();
  if (n >= dst_sz) n = dst_sz - 1;
  memcpy(dst, value.c_str(), n);
  dst[n] = '\0';
}

static void webSaveConfig() {
  uint8_t ip[4], mask[4];
  if (!parseIPv4(http.arg("ip"), ip)) return sendJsonError("bad ip");
  if (!parseIPv4(http.arg("mask"), mask)) return sendJsonError("bad netmask");
  if (mask[0] == 0 && mask[1] == 0 && mask[2] == 0 && mask[3] == 0)
    return sendJsonError("netmask is zero");

  for (int i = 0; i < NUM_UNIVERSES; i++) {
    long n = http.arg("n" + String(i)).toInt();
    long s = http.arg("s" + String(i)).toInt();
    long u = http.arg("u" + String(i)).toInt();
    long d = http.arg("d" + String(i)).toInt();
    if (n < 0 || n > 127) return sendJsonError("net out of range");
    if (s < 0 || s > 15) return sendJsonError("subnet out of range");
    if (u < 0 || u > 15) return sendJsonError("universe out of range");
    if (d != PORT_OUTPUT && d != PORT_INPUT)
      return sendJsonError("bad direction");
    config.net[i]       = (uint8_t)n;
    config.subnet[i]    = (uint8_t)s;
    config.universe[i]  = (uint8_t)u;
    config.direction[i] = (uint8_t)d;
  }
  memcpy(config.ip, ip, 4);
  memcpy(config.mask, mask, 4);
  config.dhcp_enabled = http.hasArg("dhcp") &&
                        (http.arg("dhcp") == "true" || http.arg("dhcp") == "on");

  if (http.hasArg("name"))
    setConfigName(config.short_name, sizeof(config.short_name), http.arg("name"));
  if (http.hasArg("longname"))
    setConfigName(config.long_name, sizeof(config.long_name), http.arg("longname"));

  bool ok = config.save();
  http.send(200, "application/json",
            String("{\"ok\":") + (ok ? "true" : "false") + ",\"reboot\":true}");
  if (ok) rebootNode();
}

static void webReboot() {
  http.send(200, "application/json", "{\"ok\":true}");
  rebootNode();
}

static void webFactory() {
  config.reset();
  http.send(200, "application/json", "{\"ok\":true}");
  rebootNode();
}

static void startWebServer() {
  http.on("/", webRoot);
  http.on("/api/status", HTTP_GET, webStatus);
  http.on("/api/config", HTTP_GET, webGetConfig);
  http.on("/api/config", HTTP_POST, webSaveConfig);
  http.on("/api/reboot", HTTP_POST, webReboot);
  http.on("/api/factory", HTTP_POST, webFactory);
  http.begin();
  Serial.printf("Config page: http://%s/\n", ipString(currentIP()).c_str());
}

// ------------------------------------------------------------------
// Network bring-up: DHCP client, static fallback after timeout.
// ------------------------------------------------------------------
static bool bringUpNetwork() {
  if (net_ready) return true;

  bool lease = false;

  if (config.dhcp_enabled) {
    // Explicitly select DHCP mode.  This also resets the netif after a
    // failed static attempt earlier in this boot.
    Ethernet.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0),
                    IPAddress(0, 0, 0, 0));
    Serial.printf("Ethernet: DHCP request (timeout %lu s)...\n",
                  (unsigned long)(board::DHCP_TIMEOUT_MS / 1000));
    if (Ethernet.begin(our_mac)) {
      uint32_t t0 = millis();
      while (!Ethernet.connected() &&
             millis() - t0 < board::DHCP_TIMEOUT_MS) {
        delay(10);
      }
      lease = Ethernet.connected();
    } else {
      Serial.println("Ethernet: begin() failed (DHCP)");
    }
    if (!lease) {
      Serial.println("Ethernet: no DHCP lease, using static fallback");
      Ethernet.end();   // release the netif so config()/begin() can restart
      delay(20);
    }
  }

  if (!lease) {
    IPAddress ip = config.ipAddr();
    IPAddress mask = config.maskAddr();
    if (!Ethernet.config(ip, IPAddress(0, 0, 0, 0), mask,
                         IPAddress(0, 0, 0, 0))) {
      Serial.println("Ethernet: static config rejected");
      Ethernet.end();
      return false;
    }
    if (!Ethernet.begin(our_mac)) {
      Serial.println("Ethernet: begin() failed (static)");
      Ethernet.end();
      return false;
    }
  }

  dhcp_lease_active = lease;
  Ethernet.macAddress(our_mac);

  Serial.printf("Ethernet up: %s / %s (%s)\n",
                ipString(currentIP()).c_str(),
                ipString(currentMask()).c_str(),
                lease ? "DHCP" : "static fallback");

  if (!services_started) {
    services_started = true;
    artnet_udp.begin(board::ARTNET_UDP_PORT);
    startWebServer();
  }

  net_ready = true;
  return true;
}

// ------------------------------------------------------------------
// Direction changes from the local UI / ArtAddress
// ------------------------------------------------------------------
void nodeApplyDirection(uint8_t port) {
  if (port >= NUM_UNIVERSES) return;

  if (config.isInput(port)) {
    rdm_port[port].setReceiveMode();   // DE low, listen on the bus
    rdm_port[port].flush_rx();
    resetInputState(port);
  } else {
    rdm_port[port].setTransmitMode();  // DE high, stream DMX out
    // Do not replay the last captured input frame on the output.
    memset(dmx_buffer[port], 0, sizeof(dmx_buffer[port]));
    last_art[port] = 0;
  }
}

bool nodeInputActive(uint8_t port) {
  if (port >= NUM_UNIVERSES) return false;
  return inputActive(port);
}

static void updateRuntime() {
  uint32_t now = millis();
  node.ip = currentIP();
  node.mask = currentMask();
  node.gateway = Ethernet.gatewayIP();
  node.linkUp = net_ready;
  node.netReady = net_ready;
  node.dhcpActive = dhcp_lease_active;
  node.artFps = art_fps;
  node.uptimeMs = now;
  for (uint8_t i = 0; i < NUM_UNIVERSES; i++) {
    node.lastArtMs[i] = last_art[i];
    node.lastInMs[i] = last_in[i];
  }
}

static void blinkLed(int times, int on_ms, int off_ms) {
  for (int i = 0; i < times; i++) {
    digitalWrite(board::STATUS_LED, HIGH);
    delay(on_ms);
    digitalWrite(board::STATUS_LED, LOW);
    delay(off_ms);
  }
}

// ------------------------------------------------------------------
// Arduino lifecycle
// ------------------------------------------------------------------
static NodeUi ui;

void setup() {
  pinMode(board::STATUS_LED, OUTPUT);
  digitalWrite(board::STATUS_LED, LOW);

  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("ArtNet-ETH starting...");

  config.begin();

  buildMac(our_mac);

  // OLED on I2C1 (GP26/GP27).  Wire1 is I2C1 on the Arduino-Pico core.
  Wire1.setSDA(board::UI_SDA);
  Wire1.setSCL(board::UI_SCL);
  Wire1.begin();
  Wire1.setClock(400000);
  ui.begin();

  // DMX transmitters on pio0.
  for (int i = 0; i < NUM_UNIVERSES; i++) {
    if (dmx[i].begin(board::DMX_TX_PINS[i]) != DmxOut::SUCCESS) {
      Serial.printf("[%u] DMX PIO init failed\n", i);
    }
    memset(dmx_buffer[i], 0, sizeof(dmx_buffer[i]));
  }

  // RDM receivers / DMX inputs on pio1.
  for (int i = 0; i < NUM_UNIVERSES; i++) {
    if (!rdm_port[i].begin(&dmx[i], board::DMX_RX_PINS[i],
                           board::DMX_DIR_PINS[i], pio1, i)) {
      Serial.printf("[%u] RDM RX init failed\n", i);
    }
    if (config.isInput(i)) {
      rdm_port[i].setReceiveMode();
      resetInputState(i);
      Serial.printf("[%u] configured as DMX INPUT (net %u / subnet %u / universe %u)\n",
                    i, config.net[i], config.subnet[i], config.universe[i]);
    }
  }

  static ArtnetRdm bridge(artnet_udp, rdm_port, &config);
  artnet_rdm = &bridge;

  if (!bringUpNetwork()) {
    Serial.println("Network not ready; retrying from loop()");
  } else {
    Serial.println("READY");
    blinkLed(3, 120, 120);
  }

  updateRuntime();
}

void loop() {
  // Retry a failed network bring-up (e.g. cable unplugged at boot).  The
  // retry also covers the DHCP timeout before the static fallback.
  if (!net_ready) {
    static uint32_t last_net_retry = 0;
    if (millis() - last_net_retry >= 2000) {
      last_net_retry = millis();
      if (bringUpNetwork()) {
        Serial.println("READY (network recovered)");
        blinkLed(3, 120, 120);
      }
    }
  }

  // Service the DMX input receivers first: their PIO FIFOs are only four
  // bytes deep, so any HTTP parsing or Art-Net handling in between can
  // overflow them at 250 kbaud.
  serviceDmxInput();

  if (services_started) {
    readArtnet();
    http.handleClient();
  }

  // Continuously repeat the current frame on every output port (~33 Hz).
  // A DMX node must not go silent when the controller only sends changes.
  static uint32_t last_stream = 0;
  uint32_t now_ms = millis();
  if (now_ms - last_stream >= 30) {
    last_stream = now_ms;
    for (int i = 0; i < NUM_UNIVERSES; i++) {
      if (config.isInput(i)) continue;
      if (dmx[i].busy()) continue;
      dmx[i].write(dmx_buffer[i], DMX_UNIVERSE_SIZE + 1);
    }
  }

  // Periodic ArtPollReply so controllers find the node even if their own
  // ArtPoll does not reach us.
  if (net_ready && now_ms - last_announce > 3000) {
    last_announce = now_ms;
    sendPollReply(currentBroadcast());
  }

  // Status LED: short flash on every valid Art-Net packet.
  if (led_off_at && now_ms >= led_off_at) {
    digitalWrite(board::STATUS_LED, LOW);
    led_off_at = 0;
  }

  // 1 s rolling packet rate.
  if (now_ms - fps_base_ms >= 1000) {
    art_fps = fps_cnt;
    fps_cnt = 0;
    fps_base_ms = now_ms;
    updateRuntime();
  }

  ui.update();
}
