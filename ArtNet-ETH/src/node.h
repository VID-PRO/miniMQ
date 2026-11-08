#pragma once

#include <Arduino.h>
#include <IPAddress.h>

#include "board.h"

// Live node state shared between the Art-Net engine and the local UI.
struct NodeRuntime {
  IPAddress ip;          // current IP (DHCP lease or static fallback)
  IPAddress mask;
  IPAddress gateway;
  bool linkUp;           // W5100S reports a physical link
  bool dhcpActive;       // an address was obtained over DHCP
  bool netReady;         // Ethernet + UDP + web server are running
  uint32_t artFps;       // Art-Net packets in the last second
  uint32_t lastArtMs[board::NUM_PORTS];  // last ArtDmx for an output port
  uint32_t lastInMs[board::NUM_PORTS];   // last DMX frame for an input port
  uint32_t uptimeMs;
};

extern NodeRuntime node;

// Applied by the UI when a port's direction changes: parks DE/RE and
// resets the DMX-input framing state.
void nodeApplyDirection(uint8_t port);

// Save configuration and restart the node.
void nodeReboot();

// True if the node is currently in a state where ArtDmx is expected.
bool nodeInputActive(uint8_t port);
