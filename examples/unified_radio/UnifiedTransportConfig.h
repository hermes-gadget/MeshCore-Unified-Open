#pragma once

#include <Arduino.h>

/**
 * UnifiedTransportConfig.h
 * 
 * Transport type enumeration and board capability detection for the
 * MeshCore Unified Firmware. This file defines which transports are
 * compiled into the firmware and provides helpers for runtime detection
 * of board capabilities.
 *
 * Design: All transport capability decisions are made at compile time
 * via build flags. Each build target (board) defines the transports
 * it supports by setting UNIFIED_TRANSPORT_* = 1 in platformio.ini.
 * Boards without BLE or WiFi hardware simply omit those flags.
 *
 * New transport types can be added by extending this enum and adding
 * a UNIFIED_TRANSPORT_* flag — no core MeshCore files need changes.
 */

/* ----------- Transport Type Enumeration ----------- */

enum TransportType : uint8_t {
    TRANSPORT_USB  = 0,   // USB CDC or hardware UART — always available
    TRANSPORT_BLE  = 1,   // Bluetooth Low Energy — requires BLE hardware
    TRANSPORT_WIFI = 2,   // WiFi TCP — requires WiFi hardware
    TRANSPORT_ALL  = 3,   // All registered transports are active concurrently

    // Reserve slots for future transports:
    // TRANSPORT_ESP_NOW   = 3,
    // TRANSPORT_ZIGBEE    = 4,
    // TRANSPORT_THREAD    = 5,

    TRANSPORT_NONE = 0xFF // Sentinel — not a valid selection
};

/* ----------- Transport Name String Helpers ----------- */

inline const char* transportTypeName(TransportType t) {
    switch (t) {
        case TRANSPORT_USB:  return "USB";
        case TRANSPORT_BLE:  return "Bluetooth";
        case TRANSPORT_WIFI: return "WiFi";
        case TRANSPORT_ALL:  return "All";
        default:             return "None";
    }
}

/* ----------- Board Capability Detection ----------- */

// These macros detect which transports the current board supports
// based on build flags set in platformio.ini or target.h.

// USB (Serial) is always available on any board with a serial port.
#define BOARD_HAS_USB_TRANSPORT true

// Generated targets set these flags from the companion environments exposed
// by the current upstream tree.
#if UNIFIED_TRANSPORT_BLE == 1
  #define BOARD_HAS_BLE_TRANSPORT true
#else
  #define BOARD_HAS_BLE_TRANSPORT false
#endif

#if UNIFIED_TRANSPORT_WIFI == 1 && defined(ESP32)
  #define BOARD_HAS_WIFI_TRANSPORT true
#else
  #define BOARD_HAS_WIFI_TRANSPORT false
#endif

/* ----------- Safe Default Transport ----------- */

// Returns the safest-supported transport for unified firmware.
// Preference order: BLE > USB (if screen available for PIN display) > USB.
inline TransportType getDefaultTransport() {
    return TRANSPORT_ALL;
}
