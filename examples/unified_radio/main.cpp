#include <Arduino.h>
#include <Mesh.h>
#include "MyMesh.h"
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
#if UNIFIED_TRANSPORT_BLE == 1 && defined(BLE_PIN_CODE)
  #if defined(ESP32)
    #include <helpers/esp32/SerialBLEInterface.h>
  #elif defined(NRF52_PLATFORM)
    #include <helpers/nrf52/SerialBLEInterface.h>
  #endif
  SerialBLEInterface ble_serial_interface;
#endif

// WiFi — if board supports it AND credentials are configured
#if UNIFIED_TRANSPORT_WIFI == 1 && defined(WIFI_SSID) && defined(WIFI_PWD)
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

// ---------- Helpers ----------

static uint32_t _atoi(const char* sp) {
  uint32_t n = 0;
  while (*sp && *sp >= '0' && *sp <= '9') {
    n *= 10;
    n += (*sp++ - '0');
  }
  return n;
}

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

#if defined(ESP32) && defined(WIFI_SSID) && UNIFIED_TRANSPORT_WIFI == 1
  bool wifi_needs_reconnect = false;
  unsigned long last_wifi_reconnect_attempt = 0;
#endif

// ---------- setup() ----------

void setup() {
  Serial.begin(115200);

  board.begin();

#ifdef DISPLAY_CLASS
  DisplayDriver* disp = NULL;
  if (display.begin()) {
    disp = &display;
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
  the_mesh.begin(disp != NULL);

#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  store.begin();
  the_mesh.begin(disp != NULL);

#elif defined(ESP32)
  SPIFFS.begin(true);
  store.begin();
  the_mesh.begin(disp != NULL);
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
  #if UNIFIED_TRANSPORT_BLE == 1 && defined(BLE_PIN_CODE)
    ble_serial_interface.begin(BLE_NAME_PREFIX, the_mesh.getNodePrefs()->node_name, the_mesh.getBLEPin());
    transport_manager.addTransport(TRANSPORT_BLE, &ble_serial_interface);
  #endif

  // WiFi transport — if hardware supports
  #if UNIFIED_TRANSPORT_WIFI == 1 && defined(WIFI_SSID) && defined(WIFI_PWD)
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
    WiFi.begin(WIFI_SSID, WIFI_PWD);
    wifi_serial_interface.begin(TCP_PORT);
    transport_manager.addTransport(TRANSPORT_WIFI, &wifi_serial_interface);
  #endif

  // ---------- Set up transport persistence and load saved mode ----------

  transport_manager.setPersistenceCallbacks(transportSaveCallback, transportLoadCallback);

  // Set safe default based on available transports
  TransportType default_t = TRANSPORT_USB;
  #if UNIFIED_TRANSPORT_BLE == 1 && defined(BLE_PIN_CODE) && BLE_PIN_CODE
    default_t = TRANSPORT_BLE;
  #elif UNIFIED_TRANSPORT_WIFI == 1 && defined(WIFI_SSID) && defined(WIFI_PWD)
    default_t = TRANSPORT_WIFI;
  #endif
  transport_manager.setDefaultTransport(default_t);

  // Load saved transport mode (falls back to default if none saved)
  transport_manager.loadPersistedTransport();

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

  // WiFi reconnect (only if WiFi is the active transport)
#if defined(ESP32) && defined(WIFI_SSID) && UNIFIED_TRANSPORT_WIFI == 1
  if (transport_manager.getActiveTransport() == TRANSPORT_WIFI) {
    if (wifi_needs_reconnect && (millis() - last_wifi_reconnect_attempt > 10000)) {
      WIFI_DEBUG_PRINTLN("Attempting manual WiFi reconnect...");
      WiFi.disconnect();
      WiFi.reconnect();
      last_wifi_reconnect_attempt = millis();
    }
  }
#endif
}
