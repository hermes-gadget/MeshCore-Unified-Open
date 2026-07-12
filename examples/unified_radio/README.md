# MeshCore Unified Companion Firmware

This is companion firmware with every transport supported by a device in one
image. USB/UART is always present, BLE is included where the upstream device
has a BLE companion target, and WiFi is included on ESP32 devices. The default
`All` mode keeps the available interfaces active together, so headless devices
do not need a screen or a reflash to change connection type.

The implementation is deliberately an overlay. Device definitions, radio
drivers, UI code, and companion behavior continue to come from upstream
MeshCore; only the entry point and transport multiplexer are replaced. This is
what allows a new upstream device or release to be picked up without copying
its firmware implementation into this directory.

## Build a device

Install PlatformIO, then generate the unified environments:

```bash
python3 tools/generate_unified_builds.py
```

List the generated targets:

```bash
python3 tools/generate_unified_builds.py --list
```

Build one target using the generated configuration:

```bash
pio run -c .pio/unified-platformio.ini \
  -e Heltec_v3_companion_radio_unified
```

The generated files under `.pio/` are disposable. Run the generator again
after syncing upstream or changing a variant's `platformio.ini`.

To validate the entire matrix locally, with failures checked against their
untouched upstream companion environments, run:

```bash
python3 tools/validate_unified_builds.py --resume
```

Per-target logs and resumable results are written under `.pio/`.

## Connection behavior

- `All` is the first-boot default and enables all registered transports.
- Release builds return to `All` after every reboot, including when upgrading
  a device that previously persisted a single transport.
- Frames received through USB/UART, BLE, or WiFi enter the same companion
  protocol handler.
- Responses and asynchronous events are sent to every connected interface.
- Polling rotates between interfaces so a busy connection cannot starve the
  others.
- The manager retains a single-transport API for future low-power controls,
  but release builds require no display or transport-selection UI. A custom UI
  can set `UNIFIED_RESTORE_TRANSPORT_MODE=1` to persist its selection.

On WiFi-capable release builds the device creates an access point named
`MeshCore-<node name>`. Connect to it and use TCP port `5000`; the default WPA2
password is `meshcore`.

## Easy configuration

Common settings are in
[`UnifiedFirmwareConfig.h`](UnifiedFirmwareConfig.h). You can edit that small
file directly. To keep credentials out of Git, pass private overrides only to
the build command:

```bash
export PLATFORMIO_BUILD_FLAGS="\
  -D UNIFIED_WIFI_SSID='\"Home WiFi\"' \
  -D UNIFIED_WIFI_PASSWORD='\"secret\"' \
  -D TCP_PORT=5000"
pio run -c .pio/unified-platformio.ini \
  -e Heltec_v3_companion_radio_unified
```

The generated configuration is self-contained, so PlatformIO does not merge a
normal `platformio.local.ini` automatically. CI uses the safe access-point
defaults and never embeds repository secrets.

## Device coverage

`tools/generate_unified_builds.py` resolves the current PlatformIO project and
groups its transport-specific companion environments by device. One unified
environment is generated for every group. On the current tree this covers
ESP32, nRF52, RP2040, and STM32 companion variants.

Transport capability rules are:

| Platform | Included transports |
|---|---|
| ESP32 with upstream BLE target | USB/UART + BLE + WiFi |
| ESP32 without upstream BLE target | USB/UART + WiFi |
| nRF52 with upstream BLE target | USB/UART + BLE |
| RP2040 | USB/UART |
| STM32 | USB/UART |

This describes hardware/library capability, not a promise that every upstream
board definition is defect-free. The unified CI is intended to identify only
failures introduced by this overlay.

## Release automation

Two workflows maintain coverage:

- `Unified Companion CI` regenerates and compiles every discovered companion
  target on GitHub-hosted runners for changes to this implementation.
- `Build Unified Firmware for MeshCore Release` checks daily for a new upstream
  `companion-v*` tag. It overlays the unified files onto that exact tag,
  generates every supported target, builds them on a bounded GitHub Actions
  matrix, and creates a draft `unified-v*` release with device-named images.
  If a unified target fails, the workflow compiles its untouched upstream
  source environment: a failure is ignored only when that baseline fails too.

The release workflow can also be run manually for any upstream tag or commit,
or triggered with a `meshcore-companion-release` repository dispatch event.

## Implementation files

| File | Purpose |
|---|---|
| `main.cpp` | Initializes upstream companion state and all available transports |
| `UnifiedTransportManager.*` | Concurrent multiplexer and optional single-mode selection |
| `UnifiedTransportConfig.h` | Transport identifiers and default mode |
| `UnifiedFirmwareConfig.h` | Small, user-editable WiFi/BLE/TCP configuration surface |
| `partitions_4mb.csv` | 3 MiB app plus persistent SPIFFS layout for unified 4 MB ESP32 builds |
| `tools/generate_unified_builds.py` | Cross-variant environment discovery and generation |
| `tools/apply_unified_overlay.py` | Reproduces the exact upstream release overlay locally and in CI |
| `tools/validate_unified_builds.py` | Resumable full-matrix build and upstream-failure classifier |

Both the generated builds and the retained T-Deck convenience targets compile
the authoritative files from `examples/companion_radio` and exclude only its
single-transport `main.cpp`.

Generated 4 MB ESP32 images use one factory application slot and therefore do
not support OTA updates. They retain a 896 KiB SPIFFS partition for companion
state; updates are installed over the board's normal USB/serial bootloader. On
classic 4 MB ESP32 boards that request a larger offline message queue, the
generator caps it at 96 entries so BLE and WiFi can operate together within
internal DRAM.
Generated 8 MB and 16 MB ESP32 images use the matching dual-OTA layout when a
board definition otherwise inherits Arduino's undersized default app slot.
