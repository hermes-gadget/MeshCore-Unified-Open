# MeshCore Unified Firmware

A single companion firmware build that supports **BLE**, **WiFi**, and **USB** transport simultaneously — the user selects the active transport from the device screen using the device's physical buttons, **without reflashing**.

## Overview

The standard MeshCore companion firmware (`examples/companion_radio/`) requires choosing a transport at **compile time** via build flags (`-D BLE_PIN_CODE=123456`, `-D WIFI_SSID=...`, etc.). Each transport requires a separate firmware build and flash.

The **Unified Firmware** (`examples/unified_radio/`) compiles all transports supported by the board into a single binary. At boot, the persisted transport mode is loaded. The user can change transports on the fly via the on-device menu.

## Architecture

```
examples/unified_radio/
├── main.cpp                     # Unified entry point — registers all transports
├── UnifiedTransportConfig.h     # TransportType enum, board capability macros
├── UnifiedTransportManager.h    # Proxy that delegates to the active transport
├── UnifiedTransportManager.cpp  # Transport switching, persistence, delegation
├── UnifiedTransportUI.h         # UIScreen for transport selection menu
├── MyMesh.h                     # Copied from companion_radio (unchanged)
├── MyMesh.cpp                   # Copied from companion_radio (unchanged)
├── AbstractUITask.h             # Copied from companion_radio (unchanged)
├── NodePrefs.h                  # Copied from companion_radio (unchanged)
├── DataStore.h                  # Copied from companion_radio (unchanged)
├── DataStore.cpp                # Copied from companion_radio (unchanged)
└── ui-new/
    ├── UITask.h                 # UI with transport page added
    ├── UITask.cpp               # UI with transport page + TransportBrowserScreen
    └── icons.h                  # Copied from companion_radio/ui-new (unchanged)
```

## Supported Boards

| Board | BLE | WiFi | USB | Screen | Buttons | Status |
|-------|-----|------|-----|--------|---------|--------|
| LilyGo T-Deck (ESP32-S3) | ✅ | ✅ | ✅ | ✅ | ✅ | **Verified build** |

**Adding a new board:**
1. Create a variant under `variants/<board>/` if not already there (per MeshCore conventions)
2. Create a board-specific `[env:<Board>_unified]` section in your variant's `platformio.ini`
3. Set `UNIFIED_TRANSPORT_BLE=1`, `UNIFIED_TRANSPORT_WIFI=1`, `UNIFIED_TRANSPORT_USB=1` as appropriate
4. Set credentials (`WIFI_SSID`, `WIFI_PWD`, `BLE_PIN_CODE`, `TCP_PORT`)
5. Add the `-I examples/unified_radio*` include paths and source filters

## Available Build Targets

| Target | Transports | Size (Flash) | Size (RAM) |
|--------|-----------|-------------|-----------|
| `LilyGo_TDeck_unified` | BLE + WiFi + USB | 24.7% (1.62 MB) | 58.9% (193 KB) |
| `LilyGo_TDeck_unified_ble_usb` | BLE + USB | 18.8% (1.23 MB) | 52.4% (172 KB) |
| `LilyGo_TDeck_unified_usb` | USB only | 9.6% (630 KB) | 44.4% (146 KB) |
| `LilyGo_TDeck_unified_8mb` | BLE + WiFi + USB | 52.5% (1.58 MB) | 58.9% (193 KB) |

## Building & Flashing

### Prerequisites

- [PlatformIO](https://platformio.org/) installed
- Clone the MeshCore-Unified-Open repo

### Build

```bash
# Build with all 3 transports (BLE + WiFi + USB)
pio run -e LilyGo_TDeck_unified

# Build with BLE + USB only
pio run -e LilyGo_TDeck_unified_ble_usb

# Build with USB only
pio run -e LilyGo_TDeck_unified_usb

# Build for 8MB flash T-Deck (no PSRAM)
pio run -e LilyGo_TDeck_unified_8mb
```

### Flash

```bash
pio run -e LilyGo_TDeck_unified -t upload
```

### Configure WiFi Credentials

Set your WiFi SSID and password **before flashing** in one of these places:

1. **Directly in the variant's platformio.ini** (for single-use builds):
   ```ini
   -D WIFI_SSID='"MyNetwork"'
   -D WIFI_PWD='"MyPassword"'
   ```

2. **In `platformio.local.ini`** (project root, not committed to git):
   ```ini
   [LilyGo_TDeck_unified_base]
   build_flags =
     -D WIFI_SSID='"MyNetwork"'
     -D WIFI_PWD='"MyPassword"'
   ```

3. **Override per env** in `platformio.local.ini`:
   ```ini
   [env:LilyGo_TDeck_unified]
   build_flags =
     -D WIFI_SSID='"MyNetwork"'
     -D WIFI_PWD='"MyPassword"'
   ```

## How to Switch BLE / WiFi / USB On-Device

The transport selection is available from the **Home Screen** of the device display:

1. Use the device button to cycle pages: **click** → cycles between pages (MSG → RECENT → RADIO → **Connection** → ADVERT → GPS → SENSORS → SHUTDOWN)
2. Navigate to the **Connection** page (shows current transport name)
3. **Press ENTER** (long press on single-button devices) to open the transport selection menu
4. Use **LEFT/RIGHT** (or **click** on single-button devices) to highlight a transport
5. **Press ENTER** to select and activate the highlighted transport
6. The device immediately switches to the new transport and shows `[ACTIVE]`

### Button Mapping

| Button | Action |
|--------|--------|
| **Click** (single press) | Next home screen page / move selection down |
| **Long Press** | Enter / confirm selection (on home page) or CLI rescue mode (first 8 seconds after boot) |
| **Double Click** | Previous home screen page / move selection up |

### Visual Indicators

- **Connection page**: Shows current transport name (e.g., "Bluetooth", "WiFi", "USB")
- **Transport Selection screen**: Highlights current transport in **green** with `[ACTIVE]` label
- **Selected (but not active)**: Highlighted with `*` prefix on dark background
- **Alert popups**: Shows confirmation text like "Bluetooth" when switching

## How Transport Persistence Works

1. **At boot**: The firmware reads a single byte from `/transport_mode` on the primary filesystem
2. **If the file exists and the transport is available**: That transport is started
3. **If the file doesn't exist, or the saved transport is unavailable**: The safe default is used
4. **Safe default logic**:
   - BLE if the board has BLE hardware (`BLE_PIN_CODE` set)
   - WiFi if the board has WiFi credentials configured
   - USB otherwise (always available)
5. **On each switch**: The new selection is saved to `/transport_mode` immediately
6. **No filesystem?**: The firmware works without persistence — USB is always the fallback

The persistence file is a **single byte** (`/transport_mode`) stored on the primary filesystem (SPIFFS on ESP32, LittleFS on RP2040, InternalFS on nRF52/STM32). This is completely separate from the NodePrefs binary blob and does not affect or interfere with the existing preferences system.

## When Transport Switching Happens

The following occurs during a transport switch:

1. **Current transport is gracefully stopped**:
   - BLE: Advertising stops, clients disconnect, service stops
   - WiFi: (external WiFi management preserves connection state)
   - USB: Interface is disabled
2. **Queued data is cleared** from both directions
3. **New transport is enabled**: Advertising starts (BLE), server starts (WiFi), serial prepares (USB)
4. **Selection is persisted** to `/transport_mode`
5. **UI updates** to reflect the new active transport

## Known Limitations

| Limitation | Description | Workaround |
|------------|-------------|------------|
| **WiFi credentials hardcoded** | WiFi SSID/password must be set at build time via `-D WIFI_SSID` / `-D WIFI_PWD` | Use `platformio.local.ini` to avoid committing credentials |
| **BLE PIN hardcoded** | BLE PIN is compiled in via `-D BLE_PIN_CODE` | Set in `platformio.ini` or `NodePrefs` default |
| **No in-menu WiFi config** | Cannot enter/edit WiFi credentials on-device via UI | Planned for future release |
| **Single-client WiFi** | WiFi transport accepts one TCP client at a time | Inherited from MeshCore's SerialWifiInterface |
| **nRF52 WiFi not supported** | No WiFi BSP library for nRF52 in this project | Use USB or BLE on nRF52 boards |
| **Simultaneous transports** | Only one transport active at a time | By design — prevents routing conflicts |
|| **Persistence requires filesystem** | Boards without SPIFFS/LittleFS/InternalFS cannot save transport mode | Default transport (USB) is used |
|| **ESP32-S3 no-PSRAM crash** | Pre-compiled ESP32-S3 SDK's `esp_spiram_init()` aborts fatally on boards without PSRAM | Use `LilyGo_TDeck_unified_8mb` target with `psram_stub.c` override; see build notes |

## Unsupported Boards / Transports

| Board | Reason | Recommended Transport |
|-------|--------|---------------------|
| nRF52 boards (T-Echo, etc.) | WiFi not available on nRF52 | BLE or USB |
| RP2040 boards | WiFi library not integrated for unified firmware | USB |
| STM32 boards | WiFi/BLE not integrated for unified firmware | USB |
| ESP32-C3 | Limited testing — should work with BLE + USB | USB initially |
| ESP32-S2 | No BLE hardware | WiFi or USB |

## Upstream Sync Notes

### New Files (not in upstream MeshCore)

All files under `examples/unified_radio/` are new and do not exist in the upstream `meshcore-dev/MeshCore` repo. They can be carried forward without merge conflicts during upstream updates.

| File | Purpose | Upstream Equivalent |
|------|---------|-------------------|
| `examples/unified_radio/UnifiedTransportConfig.h` | Transport enum, board capability macros | None (new) |
| `examples/unified_radio/UnifiedTransportManager.h` | Transport lifecycle manager | None (new) |
| `examples/unified_radio/UnifiedTransportManager.cpp` | Implementation | None (new) |
| `examples/unified_radio/UnifiedTransportUI.h` | Transport selection UIScreen | None (new) |
| `examples/unified_radio/main.cpp` | Unified firmware entry point | Based on `examples/companion_radio/main.cpp` |
| `examples/unified_radio/ui-new/UITask.h` | UI with transport page | Based on `examples/companion_radio/ui-new/UITask.h` |
| `examples/unified_radio/ui-new/UITask.cpp` | UI with transport page | Based on `examples/companion_radio/ui-new/UITask.cpp` |
| `examples/unified_radio/psram_stub.c` | Stub to override fatal PSRAM init on no-PSRAM boards | None (new) |
| `variants/lilygo_tdeck/partitions_8mb.csv` | 8MB flash partition table | None (new) |

### Modified Upstream Files

| File | Change | Why |
|------|--------|-----|
| `variants/lilygo_tdeck/platformio.ini` | Added `[LilyGo_TDeck_unified_base]`, `[LilyGo_TDeck_unified_8mb_base]`, and 4 `[env:LilyGo_TDeck_unified_*]` targets; 8MB variant uses `board=esp32-s3-devkitc-1` for no-PSRAM support; adds `-Wl,--allow-multiple-definition` for PSRAM stub | Build configuration for unified firmware + 8MB flash variant |

**No core MeshCore source files were modified.** The unified firmware uses only:
- Public base classes (`BaseSerialInterface`, `UIScreen`, `AbstractUITask`)
- Existing transport implementations (`SerialBLEInterface`, `SerialWifiInterface`, `ArduinoSerialInterface`)
- Existing board framework (ESP32Board, TDeckBoard, etc.)
- Existing filesystem abstraction (`DataStore`)

### How to Merge Future Upstream Updates

1. **Sync your fork** with upstream MeshCore:
   ```bash
   git fetch upstream
   git checkout dev
   git merge upstream/dev
   ```

2. **Check for conflicts in modified files:**
   - `variants/lilygo_tdeck/platformio.ini` — may have conflicts if upstream added/changed build targets
   - **Resolution**: Keep the unified firmware sections at the bottom; accept upstream's changes to the base `[LilyGo_TDeck]` section

3. **Sync copied files** from companion_radio:
   When `examples/companion_radio/` files are updated upstream, you may want to re-copy them:
   ```bash
   # Review what changed
   git diff upstream/dev -- examples/companion_radio/
   
   # Re-copy (then manually re-add transport page changes)
   cp examples/companion_radio/MyMesh.h examples/unified_radio/
   cp examples/companion_radio/MyMesh.cpp examples/unified_radio/
   cp examples/companion_radio/AbstractUITask.h examples/unified_radio/
   cp examples/companion_radio/NodePrefs.h examples/unified_radio/
   cp examples/companion_radio/DataStore.h examples/unified_radio/
   cp examples/companion_radio/DataStore.cpp examples/unified_radio/
   cp examples/companion_radio/ui-new/icons.h examples/unified_radio/ui-new/
   ```

4. **Review UI changes needed:**
   After re-copying `UITask.h/.cpp` from `companion_radio/ui-new/`, re-apply the transport page changes:
   - Add `TransportBrowserScreen` class
   - Add transport page to `HomeScreen::HomePage` enum
   - Add `openTransportScreen()` and `getTransportManager()` methods to `UITask`
   - Add transport rendering in `HomeScreen::render()`

5. **Review build flags** in case upstream changed macro names or added new board features

### Design Decisions for Mergeability

- **No changes to MeshCore.h, Mesh.cpp, Dispatcher.cpp, or other core files**
- **No changes to transport implementations** (the BLE/WiFi/USB classes are used as-is)
- **No changes to UI framework** (UIScreen pattern is preserved)
- **UnifiedTransportManager IS a BaseSerialInterface** — no interface widening needed
- **Separate persistence** — transport mode is stored in its own file, independent of NodePrefs binary format

## Future Work

- [ ] **On-device WiFi configuration** — scan and join networks from the UI
- [ ] **BLE PIN entry on-device** — change PIN without reflashing
- [ ] **Additional transports** — ESP-NOW, Thread, Zigbee as additional options
- [ ] **ESP32-S3 Octal PSRAM** — enable for better multitasking
- [ ] **Auto-detect board capabilities** — reduce build flag boilerplate
- [ ] **OTA support** — transport switching paired with OTA updates
- [ ] **nRF52 + ESP32 combo** — BLE on nRF52 + WiFi on co-processor
