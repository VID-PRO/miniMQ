// Marker header for the project-local TinyUSB NCM driver override.
//
// The pico-sdk/arduino-pico core ships this library as a thin wrapper around
// pico-sdk/lib/tinyusb/src/class/net/ncm_device.c.  We shadow it so the NCM
// NTB parameters can advertise wNdbInDivisor/wNdbOutDivisor = 4, which the
// Windows 11 usbncm.sys driver requires since KB5124008 (Sept 2026).
// See lib/tusb-ncm/src/ncm_device.c and TinyUSB PR #3914.
#pragma once
