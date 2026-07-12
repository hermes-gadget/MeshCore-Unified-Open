#include <Arduino.h>
#include <Mesh.h>
#include "../companion_radio/MyMesh.h"
#include "UnifiedFirmwareConfig.h"
#include "UnifiedTransportConfig.h"
#include "UnifiedTransportManager.h"

// ---------- Filesystem ----------

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #include <InternalFileSystem.h>
  #if defined(QSPIFLASH)
    #include <CustomLFS_QSPIFlash.h>
    DataStore store(InternalFS, QSPIFlash, rtc_clock);
  #else
  #if defined(EXTRAFS)
    #include <CustomLFS.h>
    CustomLFS ExtraFS(0xD4000, 0x19000, 128);
    DataStore store(InternalFS, ExtraFS, rtc_clock);
  #else
    DataStore store(InternalFS, rtc_clock);
  #endif
  #endif
#elif defined(RP2040_PLATFORM)
  #include <LittleFS.h>
  DataStore store(LittleFS, rtc_clock);
#elif defined(ESP32)
  #include <SPIFFS.h>
  DataStore store(SPIFFS, rtc_clock);
#endif

// ---------- All Transports ----------

// USB — always available
#include <helpers/ArduinoSerialInterface.h>
ArduinoSerialInterface usb_serial_interface;
#if defined(SERIAL_RX)
  HardwareSerial companion_serial(1);
#endif

// BLE — if board supports it
#if UNIFIED_TRANSPORT_BLE == 1
  #if defined(ESP32)
    #include <helpers/esp32/SerialBLEInterface.h>
  #elif defined(NRF52_PLATFORM)
    #include <helpers/nrf52/SerialBLEInterface.h>
  #endif
  SerialBLEInterface ble_serial_interface;
#endif

// WiFi — station mode with configured credentials, otherwise access-point mode
#if UNIFIED_TRANSPORT_WIFI == 1 && defined(ESP32)
  #include <helpers/esp32/SerialWifiInterface.h>
  SerialWifiInterface wifi_serial_interface;
#endif

// ---------- Transport Manager ----------

UnifiedTransportManager transport_manager;

// ---------- UI ----------

#ifdef DISPLAY_CLASS
  #include "UITask.h"
  UITask ui_task(&board, &transport_manager);
#endif

// ---------- Mesh & Globals ----------

StdRNG fast_rng;
SimpleMeshTables tables;
MyMesh the_mesh(radio_driver, fast_rng, rtc_clock, tables, store
   #ifdef DISPLAY_CLASS
      , &ui_task
   #endif
);

void halt() { while (1) ; }

// ---------- Transport Persistence Callbacks ----------

// These read/write a single byte to a file called /transport_mode
// on the primary filesystem. The DataStore manages the filesystem.

static bool transportSaveCallback(uint8_t value) {
  // We write via a raw filesystem operation. The DataStore instance
  // is called 'store' and is a global. We use its primary FS.
  FILESYSTEM* fs = store.getPrimaryFS();
  if (!fs) return false;
#if defined(ESP32)
  File f = fs->open("/transport_mode", "w", true);
#elif defined(RP2040_PLATFORM)
  File f = fs->open("/transport_mode", "w");
#else
  // NRF52 / STM32 — must remove first
  fs->remove("/transport_mode");
  File f = fs->open("/transport_mode", FILE_O_WRITE);
#endif
  if (!f) return false;
  size_t written = f.write(&value, 1);
  f.close();
  return (written == 1);
}

static bool transportLoadCallback(uint8_t* value) {
  FILESYSTEM* fs = store.getPrimaryFS();
  if (!fs) return false;
#if defined(ESP32)
  File f = fs->open("/transport_mode", "r", false);
#elif defined(RP2040_PLATFORM)
  File f = fs->open("/transport_mode", "r");
#else
  File f = fs->open("/transport_mode", FILE_O_READ);
#endif
  if (!f) return false;
  if (f.size() < 1) { f.close(); return false; }
  size_t read = f.read(value, 1);
  f.close();
  return (read == 1);
}

// ---------- WiFi Reconnect ----------

#if defined(ESP32) && UNIFIED_TRANSPORT_WIFI == 1
  bool wifi_needs_reconnect = false;
  unsigned long last_wifi_reconnect_attempt = 0;
#endif

// ---------- setup() ----------

void setup() {
  Serial.begin(115200);

  // Headless boards pass this through the same initialization path.
  bool display_ready = false;

  board.begin();

#ifdef DISPLAY_CLASS
  DisplayDriver* disp = NULL;
  if (display.begin()) {
    disp = &display;
    display_ready = true;
    disp->startFrame();
  #ifdef ST7789
    disp->setTextSize(2);
  #endif
    disp->drawTextCentered(disp->width() / 2, 28, "Loading...");
    disp->endFrame();
  }
#endif

  if (!radio_init()) { halt(); }

  fast_rng.begin(radio_driver.getRngSeed());

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  InternalFS.begin();
  #if defined(QSPIFLASH)
    if (!QSPIFlash.begin()) {
      MESH_DEBUG_PRINTLN("CustomLFS_QSPIFlash: failed to initialize");
    } else {
      MESH_DEBUG_PRINTLN("CustomLFS_QSPIFlash: initialized successfully");
    }
  #else
  #if defined(EXTRAFS)
      ExtraFS.begin();
  #endif
  #endif
  store.begin();
  the_mesh.begin(display_ready);

#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  store.begin();
  the_mesh.begin(display_ready);

#elif defined(ESP32)
  SPIFFS.begin(true);
  store.begin();
  the_mesh.begin(display_ready);
#endif

  // ---------- Register all available transports ----------

  // USB transport — always available
  #if defined(SERIAL_RX)
    companion_serial.setPins(SERIAL_RX, SERIAL_TX);
    companion_serial.begin(115200);
    usb_serial_interface.begin(companion_serial);
  #else
    usb_serial_interface.begin(Serial);
  #endif
  transport_manager.addTransport(TRANSPORT_USB, &usb_serial_interface);

  // BLE transport — if hardware supports
  #if UNIFIED_TRANSPORT_BLE == 1
    ble_serial_interface.begin(BLE_NAME_PREFIX, the_mesh.getNodePrefs()->node_name, the_mesh.getBLEPin());
    transport_manager.addTransport(TRANSPORT_BLE, &ble_serial_interface);
  #endif

  // WiFi transport — if hardware supports
  #if UNIFIED_TRANSPORT_WIFI == 1 && defined(ESP32)
    board.setInhibitSleep(true);
    WiFi.setAutoReconnect(true);
    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info){
        if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
            WIFI_DEBUG_PRINTLN("WiFi disconnected. Flagging for reconnect...");
            wifi_needs_reconnect = true;
        } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
            WIFI_DEBUG_PRINTLN("WiFi connected successfully!");
            wifi_needs_reconnect = false;
        }
    });
    if (UNIFIED_WIFI_SSID[0] != '\0') {
      WiFi.mode(WIFI_STA);
      WiFi.begin(UNIFIED_WIFI_SSID, UNIFIED_WIFI_PASSWORD);
    } else {
      char ap_name[33];
      snprintf(ap_name, sizeof(ap_name), "%s%s", UNIFIED_WIFI_AP_PREFIX,
               the_mesh.getNodePrefs()->node_name);
      WiFi.mode(WIFI_AP);
      if (UNIFIED_WIFI_AP_PASSWORD[0] == '\0') WiFi.softAP(ap_name);
      else WiFi.softAP(ap_name, UNIFIED_WIFI_AP_PASSWORD);
    }
    wifi_serial_interface.begin(TCP_PORT);
    transport_manager.addTransport(TRANSPORT_WIFI, &wifi_serial_interface);
  #endif

  // ---------- Set up transport persistence and load saved mode ----------

  // Set safe default based on available transports
  transport_manager.setDefaultTransport(getDefaultTransport());

  // Normal release builds always recover to concurrent mode. A custom
  // low-power selector UI can opt into restoring a persisted single mode.
  #if UNIFIED_RESTORE_TRANSPORT_MODE == 1
    transport_manager.setPersistenceCallbacks(transportSaveCallback, transportLoadCallback);
    transport_manager.loadPersistedTransport();
  #else
    transport_manager.selectTransport(getDefaultTransport());
  #endif

  // ---------- Start the mesh with the active transport ----------

  the_mesh.startInterface(transport_manager);

  // ---------- Sensors ----------

  sensors.begin();

#if ENV_INCLUDE_GPS == 1
  the_mesh.applyGpsPrefs();
#endif

#ifdef DISPLAY_CLASS
  ui_task.begin(disp, &sensors, the_mesh.getNodePrefs());
#endif

  board.onBootComplete();
}

// ---------- loop() ----------

void loop() {
  the_mesh.loop();
  sensors.loop();
#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif
  rtc_clock.tick();

  if (!the_mesh.hasPendingWork()) {
#if defined(NRF52_PLATFORM)
    board.sleep(0);
#endif
  }

  // WiFi reconnect while WiFi is selected directly or through "All" mode.
#if defined(ESP32) && UNIFIED_TRANSPORT_WIFI == 1
  if (transport_manager.getActiveTransport() == TRANSPORT_WIFI ||
      transport_manager.getActiveTransport() == TRANSPORT_ALL) {
    if (wifi_needs_reconnect && (millis() - last_wifi_reconnect_attempt > 10000)) {
      WIFI_DEBUG_PRINTLN("Attempting manual WiFi reconnect...");
      WiFi.disconnect();
      WiFi.reconnect();
      last_wifi_reconnect_attempt = millis();
    }
  }
#endif
}
