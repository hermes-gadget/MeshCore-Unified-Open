#pragma once

/*
 * User-editable unified companion settings.
 *
 * Release builds start an ESP32 access point so WiFi works without embedding
 * private credentials. Define UNIFIED_WIFI_SSID (and optionally
 * UNIFIED_WIFI_PASSWORD) through PLATFORMIO_BUILD_FLAGS to use station mode
 * without editing this tracked file. Build flags override every default here.
 */

#ifndef UNIFIED_WIFI_SSID
  #define UNIFIED_WIFI_SSID ""
#endif

#ifndef UNIFIED_WIFI_PASSWORD
  #define UNIFIED_WIFI_PASSWORD ""
#endif

#ifndef UNIFIED_WIFI_AP_PREFIX
  #define UNIFIED_WIFI_AP_PREFIX "MeshCore-"
#endif

// Eight or more characters enables WPA2. An empty value creates an open AP.
#ifndef UNIFIED_WIFI_AP_PASSWORD
  #define UNIFIED_WIFI_AP_PASSWORD "meshcore"
#endif

#ifndef TCP_PORT
  #define TCP_PORT 5000
#endif

// Set to 1 only for a custom build with a single-transport selector UI.
#ifndef UNIFIED_RESTORE_TRANSPORT_MODE
  #define UNIFIED_RESTORE_TRANSPORT_MODE 0
#endif

#ifndef BLE_PIN_CODE
  #define BLE_PIN_CODE 123456
#endif
