#include <Arduino.h>

// USB CDC-NCM "Ethernet over USB" + lwIP (Arduino-Pico core)
#include <NCMEthernetlwIP.h>
// lwIP UDP socket that routes over the NCM netif
#include <WiFiUdp.h>
// DMX512 via the RP2040 PIO (our own transmitter with spec-compliant timing)
#include <DmxOut.h>
// DHCP server for the USB link (bundled in the Arduino-Pico WiFi lib)
#include <dhcpserver/dhcpserver.h>
// Hard reboot via the watchdog
#include <hardware/watchdog.h>
// RDM gateway: bidirectional transceiver + ArtRdm bridge
#include "artnet_rdm.h"
// Persisted Art-Net config (per-port universe mapping + network settings)
#include "artnet_config.h"
// Embedded single-page web UI (Channels grid + Settings)
#include "index_html.h"
// Tiny embedded web server for the config page (served over USB Ethernet)
#include <WebServer.h>
// USB device control (disconnect the bus before a warm reboot)
#include <USB.h>

// ------------------------------------------------------------------
// DMX outputs: 4 universes on GPIO 2 / 3 / 4 / 5
// (DmxOut runs on pio0, one SM per universe.)
// ------------------------------------------------------------------
static const uint8_t NUM_UNIVERSES = 4;
static const uint8_t DMX_PINS[4] = {2, 3, 4, 5};
static DmxOut dmx[4];
// [start code 0x00] + 512 channels
static uint8_t dmx_buffer[4][DMX_UNIVERSE_SIZE + 1];

// ------------------------------------------------------------------
// RDM wiring (per port):
//   DI     = DMX_PINS[i]                  (RS485 driver input, TX - DmxOutput)
//   RO     = RDM_RX_PINS[i]               (RS485 receiver output -> Pico GPIO)
//   DE/RE  = RDM_DE_PINS[i]               (direction; high=TX, low=RX)
// RDM RX runs on pio1 (pio0 is fully used by the 4 DMX outputs).
// ------------------------------------------------------------------
static const uint8_t RDM_RX_PINS[4]  = {10, 11, 12, 13};
static const uint8_t RDM_DE_PINS[4]  = {6, 7, 8, 9};
static RdmPort rdm_port[4];
static ArtnetRdm *artnet_rdm = nullptr;

// ------------------------------------------------------------------
// USB NCM Ethernet. The static address, netmask and DHCP server come
// from the persisted config (ArtnetConfig; defaults 10.0.0.10/8, DHCP
// on, pool 10.0.0.1..10.0.0.9). The Pico itself never gets an address
// from DHCP - it only serves it to the host.
// ------------------------------------------------------------------
NCMEthernetlwIP eth;

// DHCP server hands the host an address in <ip first 3 octets>.1..9
// (DHCPS_BASE_IP=1 .. DHCPS_MAX_IP=9). Controlled by config.dhcp_enabled.
static dhcp_server_t _dhcpServer;

// ------------------------------------------------------------------
// Art-Net over the USB netif (raw UDP parse - no external lib)
// ------------------------------------------------------------------
static const uint16_t ARTNET_UDP_PORT = 6454;
static WiFiUDP artnet_udp;
static WebServer http(80);

// ------------------------------------------------------------------
// Art-Net autodiscovery: respond to ArtPoll with an ArtPollReply so
// MagicQ & co. discover all 4 universes automatically.
// ------------------------------------------------------------------
static const uint16_t OP_POLL    = 0x2000;  // ArtPoll
static const uint16_t OP_POLLREPLY = 0x2100; // ArtPollReply
static const uint16_t OP_DMX     = 0x5000;  // ArtDmx
static const uint16_t OP_ADDRESS = 0x6000;  // ArtAddress (remote programming)
static const uint16_t OP_RDM     = 0x00CC;  // ArtRdm

// On-board LED (GPIO25 on the plain Pico), flashed on each received packet.
static const uint8_t STATUS_LED = PIN_LED;

// ------------------------------------------------------------------
// Diagnostics:
//  - DMX_DEBUG_LOG: print one ArtDmx frame/second (universe, length, first
//    channel values) over USB serial, so we can see if MagicQ is sending.
//  - DMX_TEST_PATTERN: on boot, output ch1=255 on every universe so the
//    Pico->transceiver->fixture path can be verified WITHOUT MagicQ.
//  - DMX_SELF_TEST: bypass everything and toggle the DI pins directly for
//    8s (DE held high). This isolates "Pico GPIO + MAX3485 + LEDs" from
//    the PIO/DMA DMX path: LEDs should flash at ~4 Hz if the wiring works.
// ------------------------------------------------------------------
#define DMX_DEBUG_LOG     1
#define DMX_TEST_PATTERN  0
#define DMX_SELF_TEST     0

static const uint16_t ARTNET_PORT_STYLE_DMX = 0x88; // 0x80 DMX512 output + 0x08 RDM capable
static const uint32_t ARTNET_OEM            = 0x1234;
static const uint16_t ARTNET_ESTA           = 0x0000; // ESTA manufacturer code

// Fixed node MAC for the NCM link. Must match ncm_default_mac in the vendored
// NCMEthernet.cpp so the lwIP netif MAC matches the USB descriptor string that
// from the very first enumeration (host interface MAC = NCM_MAC flipped).
static const uint8_t NCM_MAC[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};

static uint8_t our_mac[6] = {0};
static uint32_t last_announce = 0;

// True once the NCM link, DHCP server and web server are up. If bring-up
// fails at boot we keep retrying from loop() so the config page recovers
// without a re-plug.
static bool net_ready = false;

// Live status for the web UI: per-port last ArtDmx time, fps counter.
static uint32_t last_art[4] = {0};
static volatile uint32_t art_fps = 0;
static uint32_t fps_cnt = 0;
static uint32_t fps_base_ms = 0;

// DMX input state (ports configured as inputs): time of the last complete
// frame, ArtDmx sequence counter and the receive framing state machine.
static uint32_t last_in[4]  = {0};
static uint8_t  in_seq[4]   = {0};
static uint8_t  in_state[4] = {0};   // 0=sync 1=start code 2=data
static uint16_t in_idx[4]   = {0};

// True if the input port has produced a frame within the last second.
static bool inputActive(uint8_t p) { return (millis() - last_in[p]) < 1000; }

// Build a full ArtPollReply packet (239 bytes) advertising NUM_UNIVERSES
// DMX output ports on universes 0..NUM_UNIVERSES-1 (net 0).
// Offsets follow the Art-Net spec: after the 64-byte NodeReport (108..171)
// come NumPorts (172-173), then the port tables.
static void buildPollReply(uint8_t *p) {
  memset(p, 0, 239);

  // ID "Art-Net"
  memcpy(p, "Art-Net", 7);
  p[7] = 0x00;

  // OpCode = OpPollReply (little-endian)
  p[8] = OP_POLLREPLY & 0xFF;
  p[9] = (OP_POLLREPLY >> 8) & 0xFF;

  // IP of the node (ours)
  p[10] = eth.localIP()[0];
  p[11] = eth.localIP()[1];
  p[12] = eth.localIP()[2];
  p[13] = eth.localIP()[3];

  // Port 6454 (little-endian)
  p[14] = ARTNET_UDP_PORT & 0xFF;
  p[15] = (ARTNET_UDP_PORT >> 8) & 0xFF;

  // VersInfo (little-endian), e.g. 1.0
  p[16] = 0x00;
  p[17] = 0x01;

  // NetSwitch (net) & SubSwitch (subnet) both 0
  p[18] = 0x00;
  p[19] = 0x00;

  // OEM (little-endian)
  p[20] = ARTNET_OEM & 0xFF;
  p[21] = (ARTNET_OEM >> 8) & 0xFF;

  // UbeaVersion = 0, Status1: bit1 = RDM capable
  p[22] = 0x00;
  p[23] = 0x02;

  // ESTA manufacturer code (all 0 = not registered)
  p[24] = ARTNET_ESTA & 0xFF;
  p[25] = (ARTNET_ESTA >> 8) & 0xFF;

  // ShortName (18)
  const char *shortName = config.short_name;
  if (shortName[0] == '\0') shortName = "ArtNet-USB";
  strncpy((char *)&p[26], shortName, 17);
  p[26 + 17] = 0x00;

  // LongName (64)
  const char *longName = config.long_name;
  if (longName[0] == '\0') longName = "ArtNet-USB 4xDMX node";
  strncpy((char *)&p[44], longName, 63);
  p[44 + 63] = 0x00;

  // NodeReport (64): 108..171 left blank (zeroed)

  // NumPortsHi/Lo = 0x00, 0x04
  p[172] = 0x00;
  p[173] = NUM_UNIVERSES;

  // PortTypes[4]: output = DMX512 + output (0x80) + RDM (0x08);
  //               input  = DMX512 + input  (0x40).
  // GoodInput/GoodOutput, SwIn and SwOut follow the configured direction.
  for (uint8_t i = 0; i < NUM_UNIVERSES; i++) {
    bool in = config.isInput(i);
    uint8_t sw = (uint8_t)((config.subnet[i] << 4) | config.universe[i]);
    p[174 + i] = in ? 0x40 : ARTNET_PORT_STYLE_DMX;              // PortTypes
    p[178 + i] = in ? (inputActive(i) ? 0x80 : 0x00) : 0x00;     // GoodInput
    p[182 + i] = in ? 0x00 : 0x80;                               // GoodOutput
    p[186 + i] = in ? sw : 0x80;                                 // SwIn
    p[190 + i] = in ? 0x00 : sw;                                 // SwOut
  }

  // SwVideo, SwMacro, SwRemote, Spare, Style
  p[194] = 0x00;              // SwVideo
  p[195] = 0x00;              // SwMacro
  p[196] = 0x00;              // SwRemote
  // p[197..199] Spare = 0
  p[200] = 0x00;              // Style: 0 = StNode

  // MAC (ours)
  memcpy(&p[201], our_mac, 6);

  // BindIndex = 0, Status2 = 0, filler[26] = 0
  p[207] = 0x00;              // BindIndex
  p[208] = 0x00;              // Status2
  // p[209..234] filler
}

// Send an ArtPollReply, either to a unicast requester or broadcast.
static void sendPollReply(IPAddress dest) {
  uint8_t reply[239];
  buildPollReply(reply);

  artnet_udp.beginPacket(dest, ARTNET_UDP_PORT);
  artnet_udp.write(reply, sizeof(reply));
  artnet_udp.endPacket();
}

// Handle an incoming ArtPoll and reply so the controller discovers us.
static void handlePoll(const uint8_t *pkt, int size, IPAddress src) {
  (void)pkt; (void)size;
  // Reply unicast to the requester (most reliable) and to the subnet
  // broadcast so any controller sniffing the link finds us too.
  sendPollReply(src);
  sendPollReply(config.broadcastAddr());
}

// ------------------------------------------------------------------
// ArtAddress (OpCode 0x6000) - remote programming.
//
// A controller (e.g. MagicQ) can reprogram a node's net/subnet/universe
// mapping without touching the web UI. Field layout (Art-Net 4 spec):
//   12 NetSwitch, 13 BindIndex, 14..31 ShortName, 32..95 LongName,
//   96..99 SwIn[4], 100..103 SwOut[4], 104 SubSwitch, 105 AcnPriority,
//   106 Command.
// A switch value is applied only when its new-value flag (bit 7) is set;
// 0x00 resets to the physical setting, 0x7F means "no change". Names are
// ignored if the string is null. Per spec the node confirms with a fresh
// unicast ArtPollReply.
// ------------------------------------------------------------------
static void handleAddress(const uint8_t *pkt, int n, IPAddress src) {
  // ProtVer must be 14. Minimum ArtAddress payload has a full 107-byte
  // packet (up to Command); names/SwIn/SwOut need at least 104/106.
  if (pkt[10] != 0x00 || pkt[11] != 0x0E) return;
  if (n < 30) return;   // nothing beyond the header

  bool changed = false;

  // NetSwitch: new value only when bit 7 set (0x7F = no change).
  if ((pkt[12] & 0x80) && (pkt[12] & 0x7F) != 0x7F) {
    uint8_t net = pkt[12] & 0x7F;
    for (int i = 0; i < NUM_UNIVERSES; i++) if (config.net[i] != net) changed = true;
    for (int i = 0; i < NUM_UNIVERSES; i++) config.net[i] = net;
  }
  // SubSwitch: same rule for bits 7-4 of the Port-Address.
  if ((pkt[104] & 0x80) && (pkt[104] & 0x0F) != 0x0F) {
    uint8_t sub = pkt[104] & 0x0F;
    for (int i = 0; i < NUM_UNIVERSES; i++) if (config.subnet[i] != sub) changed = true;
    for (int i = 0; i < NUM_UNIVERSES; i++) config.subnet[i] = sub;
  }
  // SwIn/SwOut: per-port universe (bits 3-0), apply only when bit 7 set.
  if (n >= 104) {
    for (int i = 0; i < NUM_UNIVERSES; i++) {
      uint8_t sw = config.isInput(i) ? pkt[96 + i] : pkt[100 + i];
      if (sw & 0x80) {
        uint8_t uni = sw & 0x0F;
        if (config.universe[i] != uni) { config.universe[i] = uni; changed = true; }
      }
    }
  }

  // Names: apply only if the string is not null (first byte != 0).
  if (n >= 32 && pkt[14] != 0) {
    size_t len = strnlen((const char *)&pkt[14], 17);
    if (memcmp(pkt + 14, config.short_name, len) != 0 || config.short_name[len] != 0) {
      memcpy(config.short_name, &pkt[14], len);
      config.short_name[len] = '\0';
      changed = true;
    }
  }
  if (n >= 96 && pkt[32] != 0) {
    size_t len = strnlen((const char *)&pkt[32], 63);
    if (memcmp(pkt + 32, config.long_name, len) != 0 || config.long_name[len] != 0) {
      memcpy(config.long_name, &pkt[32], len);
      config.long_name[len] = '\0';
      changed = true;
    }
  }

  // Command (byte 106): AcDirectionTx0..3 = port to output,
  // AcDirectionRx0..3 = port to input (Art-Net spec: retained on power-off).
  if (n >= 107) {
    uint8_t cmd = pkt[106];
    if (cmd >= 0x20 && cmd < 0x24) {
      int port = cmd - 0x20;
      if (config.isInput(port)) {
        config.direction[port] = PORT_OUTPUT;
        rdm_port[port].setTransmitMode();   // DE high: stream DMX again
        changed = true;
      }
    } else if (cmd >= 0x30 && cmd < 0x34) {
      int port = cmd - 0x30;
      if (!config.isInput(port)) {
        config.direction[port] = PORT_INPUT;
        rdm_port[port].setReceiveMode();    // DE low: listen on the bus
        last_in[port] = 0;                  // clear stale "input active" state
        in_state[port] = 0;                 // reset the DMX-in framing
        in_idx[port] = 0;
        changed = true;
      }
    }
  }

  if (changed) {
    bool ok = config.save();
    (void)ok;
  }

  // Confirm to the requester with a fresh ArtPollReply reflecting the
  // just-applied address (spec: node replies to ArtAddress by unicasting).
  sendPollReply(src);
}

// ------------------------------------------------------------------
// DMX input: for each port configured as an input, capture the incoming
// DMX stream (BREAK + start code 0x00 + 512 slots) from the RDM RX PIO
// and publish it as ArtDmx to the network.
// ------------------------------------------------------------------

// Send one ArtDmx packet carrying the current input frame for 'port'.
static void sendArtDmx(uint8_t port) {
  uint8_t pkt[18 + DMX_UNIVERSE_SIZE];
  memcpy(pkt, "Art-Net", 8);          // ID + null terminator
  pkt[8]  = OP_DMX & 0xFF;            // OpCode lo
  pkt[9]  = (OP_DMX >> 8) & 0xFF;     // OpCode hi
  pkt[10] = 0x00;                     // ProtVerHi
  pkt[11] = 0x0E;                     // ProtVerLo
  pkt[12] = in_seq[port]++;           // Sequence (wraps 0..255)
  pkt[13] = 0x00;                     // Physical
  pkt[14] = (uint8_t)((config.subnet[port] << 4) | config.universe[port]);
  pkt[15] = config.net[port] & 0x7F;
  pkt[16] = (DMX_UNIVERSE_SIZE >> 8) & 0xFF;  // LengthHi = 0x02
  pkt[17] = DMX_UNIVERSE_SIZE & 0xFF;         // LengthLo = 0x00
  memcpy(pkt + 18, &dmx_buffer[port][1], DMX_UNIVERSE_SIZE);

  artnet_udp.beginPacket(config.broadcastAddr(), ARTNET_UDP_PORT);
  artnet_udp.write(pkt, sizeof(pkt));
  artnet_udp.endPacket();
}

// Drain the RX FIFO of every input port and assemble complete DMX frames.
// The PIO is edge-triggered, so a long BREAK yields exactly one 0x00 byte
// which we use as the frame delimiter.
static void serviceDmxInput() {
  uint32_t now = millis();
  for (int i = 0; i < NUM_UNIVERSES; i++) {
    if (!config.isInput(i)) continue;
    while (rdm_port[i].available()) {
      int b = rdm_port[i].readByte();
      if (b < 0) break;
      switch (in_state[i]) {
        case 0:   // wait for BREAK (0x00 marks the long low period)
          if (b == 0x00) in_state[i] = 1;
          break;
        case 1:   // expect the DMX start code (0x00)
          if (b == 0x00) { in_idx[i] = 1; in_state[i] = 2; }
          else in_state[i] = 0;
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

void readArtnet() {
  int packet_size = artnet_udp.parsePacket();
  if (packet_size < 14) return;

  IPAddress src = artnet_udp.remoteIP();

  uint8_t pkt[530];
  int n = artnet_udp.read(pkt, sizeof(pkt));
  if (n < 14) return;

  // Art-Net ID
  if (memcmp(pkt, "Art-Net", 7) != 0 || pkt[7] != 0) return;

  // Valid ArtNet packet received: blink the on-board LED.
  digitalWrite(STATUS_LED, 1);
  fps_cnt++;

  // OpCode (little-endian)
  uint16_t opcode = (uint16_t)(pkt[9] << 8) | pkt[8];

  switch (opcode) {
    case OP_POLL: {
      // MagicQ/others broadcast ArtPoll to discover nodes. Confirm the
      // protocol version then advertise our 4 output universes.
      if (pkt[10] == 0x00 && pkt[11] == 0x0E) {
        handlePoll(pkt, n, src);
      }
      break;
    }

    case OP_DMX: {
      if (n < 18) return;

      // Protocol version must be 14 (ProtVerHi=0, ProtVerLo=0x0E)
      if (pkt[10] != 0x00 || pkt[11] != 0x0E) return;

      // Art-Net 15-bit address: byte15 = Net (7 bits),
      //                       byte14 = SubNet(4) | Universe(4).
      uint16_t addr_net = pkt[15] & 0x7F;
      uint8_t  sub      = (pkt[14] >> 4) & 0x0F;
      uint8_t  uni      = pkt[14] & 0x0F;
      uint16_t address  = (uint16_t)((addr_net << 8) | (sub << 4) | uni);

      uint16_t length = (uint16_t)(pkt[17] << 8) | pkt[16];
      if (length > DMX_UNIVERSE_SIZE) length = DMX_UNIVERSE_SIZE;

      int port = config.outputPortForAddress(address);
      if (port < 0) return;
      last_art[port] = millis();

      // If the previous frame is still draining out via DMA, skip this one
      // entirely. Rewriting dmx_buffer or restarting the SM while DMA reads
      // it would corrupt the frame in progress. MagicQ retransmits ArtDmx at
      // ~30-40 Hz, so the next packet lands within a frame period anyway.
      if (dmx[port].busy()) return;

      uint8_t *data = &pkt[18];
      int avail = n - 18;                       // bytes actually received
      if (avail > DMX_UNIVERSE_SIZE) avail = DMX_UNIVERSE_SIZE;

      // MagicQ-like controllers declare a tiny length in the header but
      // always transmit the full universe. Trust the LARGER of declared
      // length and the real datagram size; never copy past the buffer.
      int frame_len = (length > avail) ? length : avail;

      dmx_buffer[port][0] = 0x00;                // DMX start code
      memcpy(&dmx_buffer[port][1], data, avail);
      // Pad to a full 512-slot frame: controllers often transmit only the
      // slots that changed, but many fixtures require a complete frame.
      if (frame_len < DMX_UNIVERSE_SIZE) {
        memset(&dmx_buffer[port][frame_len + 1], 0,
               DMX_UNIVERSE_SIZE - frame_len);
      }
      dmx[port].write(dmx_buffer[port], DMX_UNIVERSE_SIZE + 1);

      break;
    }

    case OP_ADDRESS: {
      // MagicQ reprograms the node (net/subnet/universe/names) via
      // ArtAddress. Apply and confirm with a fresh unicast ArtPollReply.
      handleAddress(pkt, n, src);
      break;
    }

    case OP_RDM: {
      // Route RDM commands from MagicQ to the fixtures on the DMX ports.
      if (artnet_rdm) {
        artnet_rdm->handlePacket(pkt, n, src);
      }
      break;
    }

    default:
      break;
  }
}

// ------------------------------------------------------------------
// Embedded web UI + JSON API (Channels grid + Settings), served at
// http://<node-ip>/  over the USB Ethernet link.
// ------------------------------------------------------------------

// Hard reboot via the watchdog (SDK: watchdog_enable with 1ms timeout).
//
// Before triggering the reset we explicitly drop the USB device. A watchdog
// reset alone can leave the USB pull-up asserted, so the host never notices
// the disconnect and does not re-enumerate the NCM interface -> the web page
// is unreachable after a warm reboot (it only works after a cold boot).
// USB.disconnect() blocks ~500 ms while connected, giving the host time to
// see the unplug, so the next boot enumerates cleanly.
static void rebootNode() {
  Serial.println("Rebooting...");
  Serial.flush();
  USB.disconnect();
  delay(100);
  watchdog_enable(1, 1);
  while (1) {}
}

// Parses "a.b.c.d" into octets. Returns true on success.
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

// Escape a name for safe embedding in a JSON string token
// (quotes and backslashes only - sufficient for our use).
static String jsonEscape(const char *s) {
  String out;
  for (; *s; s++) {
    if (*s == '"' || *s == '\\') out += '\\';
    out += *s;
  }
  return out;
}

// Live status for the header pills + the 512-channel grid.
static void webStatus() {
  int port = http.arg("port").toInt();
  if (port < 0 || port >= NUM_UNIVERSES) port = 0;

  // Cheap 32-bit FNV hash of the requested port's frame. When unchanged, the
  // ~1.5 KB "values" array is skipped so the 300 ms poll does far less work
  // on the Pico and over the USB link; the browser keeps its last grid.
  static uint32_t last_hash[NUM_UNIVERSES] = {0};
  uint32_t hash = 2166136261u;
  for (int c = 0; c <= DMX_UNIVERSE_SIZE; c++) {
    hash ^= dmx_buffer[port][c];
    hash *= 16777619u;
  }
  bool values_changed = (hash != last_hash[port]);
  last_hash[port] = hash;

  String j = "{\"ip\":\"" + config.ipAddr().toString() + "\",";
  j.reserve(values_changed ? 4096 : 512);   // only budget for values if changed
  j += "\"mask\":\"" + config.maskAddr().toString() + "\",";
  j += "\"dhcp\":" + String(config.dhcp_enabled ? "true" : "false") + ",";
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
    j += ",\"values\":[0";   // values[i] == DMX channel i (page uses 1..512)
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
  String j = "{\"ip\":\"" + String(config.ip[0]) + "." + String(config.ip[1])
           + "." + String(config.ip[2]) + "." + String(config.ip[3]) + "\",";
  j.reserve(512);
  j += "\"mask\":\"" + String(config.mask[0]) + "." + String(config.mask[1])
     + "." + String(config.mask[2]) + "." + String(config.mask[3]) + "\",";
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

// Copy a (possibly longer) web form value into a fixed-size config buffer,
// truncated and NUL-terminated safely.
static void setConfigName(char *dst, size_t dst_sz, const String &value) {
  size_t n = (size_t)value.length();
  if (n >= dst_sz) n = dst_sz - 1;
  memcpy(dst, value.c_str(), n);
  dst[n] = '\0';
}

// Saves the form (urlencoded) and reboots so the new network settings
// apply. Mirrors the classic "Save & reboot" loop of the node UI.
static void webSaveConfig() {
  uint8_t ip[4], mask[4];
  if (!parseIPv4(http.arg("ip"), ip))  return sendJsonError("bad ip");
  if (!parseIPv4(http.arg("mask"), mask)) return sendJsonError("bad netmask");
  if (mask[0] == 0 && mask[1] == 0 && mask[2] == 0 && mask[3] == 0)
    return sendJsonError("netmask is zero");

  for (int i = 0; i < NUM_UNIVERSES; i++) {
    long n = http.arg("n" + String(i)).toInt();
    long s = http.arg("s" + String(i)).toInt();
    long u = http.arg("u" + String(i)).toInt();
    long d = http.arg("d" + String(i)).toInt();
    if (n < 0 || n > 127) return sendJsonError("net out of range");
    if (s < 0 || s > 15)  return sendJsonError("subnet out of range");
    if (u < 0 || u > 15)  return sendJsonError("universe out of range");
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

  // Optional: override the ArtPollReply node names.
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
  config.reset();   // back to 10.0.0.10 / 255.0.0.0 / DHCP on / univ 0..3
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
  Serial.printf("Config page: http://%s/\n", config.ipAddr().toString().c_str());
}

static void blinkLed(int times, int on_ms, int off_ms) {
  for (int i = 0; i < times; i++) {
    digitalWrite(STATUS_LED, 1);
    delay(on_ms);
    digitalWrite(STATUS_LED, 0);
    delay(off_ms);
  }
}

static void blinkReadyLed() {
  blinkLed(3, 120, 120);   // 3 flashes = boot + init complete
}

// Bring the USB NCM link, DHCP server and web server up. Safe to call
// repeatedly. eth.begin() can only run once (the library rejects a second
// instance), so the stack/servers are started on the first call and later
// calls just wait for the host to enumerate the interface.
static bool bringUpNetwork() {
  if (net_ready) return true;

  static bool eth_started = false;   // eth.begin() can only run once
  IPAddress node_ip = config.ipAddr();

  if (!eth_started) {
    bool ok = false;
    for (int attempt = 1; attempt <= 5 && !ok; attempt++) {
      eth.config(node_ip, node_ip, config.maskAddr());
      // Pass a fixed MAC so the lwIP netif MAC matches the NCM descriptor
      // registered at boot (see NCMEthernet.cpp). begin() no longer
      // disconnects/re-enumerates USB; the CDC+NCM composite is present from
      // the very first enumeration, which is what macOS needs to hand out an
      // Ethernet interface.
      ok = eth.begin(NCM_MAC);
      if (!ok) {
        Serial.printf("NCM Ethernet init attempt %d failed; retrying\n", attempt);
        delay(500);
      }
    }
    if (!ok) {
      Serial.println("NCM Ethernet init FAILED");
      blinkLed(5, 60, 60);
      return false;
    }
    eth_started = true;

    // Cache our MAC for the ArtPollReply node fields.
    eth.macAddress(our_mac);
    Serial.printf("NCM Ethernet up @ %s\n", eth.localIP().toString().c_str());

    // Optionally serve DHCP so the host auto-configures. Server IP/netmask
    // come from the NCM netif itself; the pool is always <server>.1..9
    // (DHCPS_BASE_IP..DHCPS_MAX_IP -> last octet).
    if (config.dhcp_enabled) {
      struct netif *nif = eth.getNetIf();
      dhcp_server_init(&_dhcpServer, &nif->ip_addr, &nif->netmask, nif);
      Serial.printf("DHCP server on %u.%u.%u.x (.1-.9)\n",
                    node_ip[0], node_ip[1], node_ip[2]);
    } else {
      Serial.println("DHCP server disabled (configure the host manually)");
    }

    startWebServer();
  }

  // Only declare ready once the host has actually enumerated the NCM
  // interface (USB mounted). After a warm reboot the host may still be
  // re-enumerating; without this wait the node would report "ready" while
  // the host has no link to it yet.
  unsigned long t0 = millis();
  while (eth.linkStatus() != LinkON && millis() - t0 < 3000) {
    delay(10);
  }
  if (eth.linkStatus() != LinkON) {
    Serial.println("Waiting for host to enumerate NCM interface...");
    return false;
  }

  // Give the host a moment to finish DHCP/ARP before we declare ready.
  delay(200);
  net_ready = true;
  return true;
}

// ------------------------------------------------------------------
// Arduino lifecycle
// ------------------------------------------------------------------
// The framework calls this between C++ global construction and USB.begin(),
// after the USBClass USB global is fully constructed but before the USB
// descriptor is built. The NCM interface must be registered here, NOT from a
// global constructor: the "eth" global may be constructed before the "USB"
// global (link order), and USB's constructor would then wipe the registered
// interfaces/endpoints, leaving macOS with a CDC-only descriptor.
void initVariant() {
    eth.usbRegisterInterfaces();
}

void setup() {
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, 0);
  Serial.begin(115200);

  config.begin();   // load per-port universe mapping from flash

  // Raw GPIO self-test (see DMX_SELF_TEST above).
  if (DMX_SELF_TEST) {
    // Phase A: toggle DI directly at 4 Hz, DE high -> cable + LED check.
    for (int i = 0; i < NUM_UNIVERSES; i++) {
      pinMode(DMX_PINS[i], OUTPUT);
      pinMode(RDM_DE_PINS[i], OUTPUT);
      digitalWrite(RDM_DE_PINS[i], HIGH);   // keep driver enabled
      digitalWrite(DMX_PINS[i], LOW);
    }
    uint32_t t0 = millis();
    while (millis() - t0 < 8000) {
      bool v = (millis() / 125) & 1;         // 4 Hz square
      for (int i = 0; i < NUM_UNIVERSES; i++)
        digitalWrite(DMX_PINS[i], v);
      digitalWrite(STATUS_LED, (millis() / 250) & 1);
      delay(10);
    }

    // Phase B: hammer DmxOutput::write() back-to-back with an alternating
    // byte pattern so BREAK+data are continuous (~high duty on data-).
    for (int i = 0; i < NUM_UNIVERSES; i++) {
      memset(dmx_buffer[i], 0, sizeof(dmx_buffer[i]));
      for (int c = 1; c < DMX_UNIVERSE_SIZE + 1; c++)
        dmx_buffer[i][c] = (c & 1) ? 0xAA : 0x55;
    }
    t0 = millis();
    while (millis() - t0 < 8000) {
      for (int i = 0; i < NUM_UNIVERSES; i++)
        dmx[i].write(dmx_buffer[i], DMX_UNIVERSE_SIZE + 1);
    }
    digitalWrite(STATUS_LED, 1);
    delay(1000);
  }

  // DMX outputs on GPIO 2/3/4/5 (all on pio0)
  for (int i = 0; i < NUM_UNIVERSES; i++) {
    if (dmx[i].begin(DMX_PINS[i]) != DmxOut::SUCCESS) {
      Serial.printf("[%u] DMX PIO init failed\n", i);
    }
    memset(dmx_buffer[i], 0, sizeof(dmx_buffer[i]));
  }

  // Test pattern: channels 1-4 = 255, everything else 0, on every universe.
  // If the fixture lights now, the Pico -> transceiver -> fixture path is
  // good and the problem is MagicQ's output mapping. Set DMX_TEST_PATTERN
  // to 0 to disable.
  if (DMX_TEST_PATTERN) {
    for (int i = 0; i < NUM_UNIVERSES; i++) {
      if (config.isInput(i)) continue;
      dmx_buffer[i][1] = 255;
      dmx_buffer[i][2] = 255;
      dmx_buffer[i][3] = 255;
      dmx_buffer[i][4] = 255;
      dmx[i].write(dmx_buffer[i], DMX_UNIVERSE_SIZE + 1);
    }
  }

  // RDM receivers on pio1 (one SM per port on pins 6..9), DE/RE on 10..13.
  // Ports configured as DMX inputs are parked in receive mode (DE low); they
  // never stream DMX out, their frames are published as ArtDmx in loop().
  for (int i = 0; i < NUM_UNIVERSES; i++) {
    if (!rdm_port[i].begin(&dmx[i], RDM_RX_PINS[i], RDM_DE_PINS[i],
                           pio1, i)) {
      Serial.printf("[%u] RDM RX init failed\n", i);
    }
    if (config.isInput(i)) {
      rdm_port[i].setReceiveMode();
      Serial.printf("[%u] configured as DMX INPUT (net %u / subnet %u / universe %u)\n",
                    i, config.net[i], config.subnet[i], config.universe[i]);
    }
  }
  static ArtnetRdm bridge(artnet_udp, rdm_port, &config);
  artnet_rdm = &bridge;
  (void)artnet_rdm;

  // Configured static IP on the USB link (default 10.0.0.10). The gateway
  // is the node itself (Pico is the only host on this point-to-point link).
  if (!bringUpNetwork()) {
    Serial.println("Network not ready; retrying from loop()");
  }

  artnet_udp.begin(ARTNET_UDP_PORT);

  if (net_ready) {
    Serial.println("READY");
    blinkReadyLed();
  }
}

void loop() {
  // If the network/web server failed to come up at boot, keep retrying.
  if (!net_ready) {
    static uint32_t last_net_retry = 0;
    if (millis() - last_net_retry >= 2000) {
      last_net_retry = millis();
      if (bringUpNetwork()) {
        Serial.println("READY (network recovered)");
        blinkReadyLed();
      }
    }
  }

  http.handleClient();

  readArtnet();

  // Assemble any incoming DMX on the ports configured as inputs and publish
  // the completed frames as ArtDmx.
  serviceDmxInput();

  // Continuously stream the current frame for every OUTPUT universe (~33 Hz).
  // A DMX node always outputs a repeating frame - it must not go silent
  // when the controller only sends ArtDmx on value changes. The boot
  // test pattern (if DMX_TEST_PATTERN) simply seeds those frames.
  static uint32_t last_stream = 0;
  uint32_t now_ms = millis();
  if (now_ms - last_stream >= 30) {
    last_stream = now_ms;
    for (int i = 0; i < NUM_UNIVERSES; i++) {
      if (config.isInput(i)) continue;
      // Never restart the PIO/DMA while a frame is still on the wire; the
      // next 30 ms tick picks it up once the current frame finished.
      if (dmx[i].busy()) continue;
      dmx[i].write(dmx_buffer[i], DMX_UNIVERSE_SIZE + 1);
    }
  }

  // Periodically announce ourselves so MagicQ & co. discover the node even
  // if their ArtPoll doesn't round-trip over the USB link. Standard
  // fallback for flaky broadcast/ArtPoll delivery.
  uint32_t now = millis();
  if (now - last_announce > 3000) {
    last_announce = now;
    sendPollReply(config.broadcastAddr());   // e.g. 10.255.255.255 for /8
  }

  // 1 s rolling Art-Net packet rate for the web status pills.
  if (now - fps_base_ms >= 1000) {
    art_fps = fps_cnt;
    fps_cnt = 0;
    fps_base_ms = now;
  }
}
