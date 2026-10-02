# Unified firmware upstream API port

## Scope and baseline

Compile verification only; no hardware or wiring. The overlay remains on the
repository's default `main` line, rather than importing the experimental
`sigurdos-tdeck` branch. Upstream `companion-v1.17.1` resolves to
`d92964352441e53b93e8667b802e04f6e072b39e`. The latest daily run before this
change, [36996304889](https://github.com/hermes-gadget/MeshCore-Unified-Open/actions/runs/36996304889),
had 74 failed build jobs out of 99 targets.

## Breakage inventory and decisions

| Upstream change | Overlay fix or compatibility evidence |
| --- | --- |
| `UITask` in ui-new, ui-orig and ui-tiny, plus `AbstractUITask`, now take `MultiSerialInterface*`, rather than `BaseSerialInterface*`. | Derive `UnifiedTransportManager` from the upstream concrete interface. No copied or modified upstream UI headers. |
| UI controls changed from serial enable/disable to Bluetooth-only controls. | Register each physical transport with upstream `InterfaceType`; inherited Bluetooth controls reach the real BLE interface. Unified reads, writes, busy checks and connection checks now respect disabled interfaces. |
| `BaseSerialInterface` gained `loop()` and the companion entry point polls its manager. | Forward the hook to enabled interfaces and call it from the unified entry point. |
| Upstream companion entry point starts and polls external watchdogs. | Preserve both calls behind `HAS_EXTERNAL_WATCHDOG`. |
| ThinkNode M7's selected BLE environment also enables CH390 Ethernet. | Initialize and register the inherited Ethernet interface, poll it, and list it in the generated transport manifest. Existing persisted USB/BLE/WiFi/All values remain unchanged. |
| Headers and enums | The relevant new dependency is `helpers/MultiSerialInterface.h`; `InterfaceType` supplies Bluetooth/USB/WiFi/Ethernet. `UIEventType` and the selected UI header paths remain compatible. |
| Mesh, board and storage APIs | `MyMesh` construction, `startInterface(BaseSerialInterface&)`, `begin(bool)`, `getNodePrefs()`, `getBLEPin()`, `DataStore` construction, `getPrimaryFS()`, radio initialization and board boot/sleep calls still match v1.17.1. Upstream preference serialization changes stay in the authoritative companion sources. |

Upstream has no entry-point hook for substituting this transport policy. Keep
the small unified entry point, using upstream UI and mesh implementations.
Delegating all transport behavior to upstream's manager would lose the overlay's
persisted selection, round-robin receive polling and USB connection detection.
The upstream manager supplies the UI integration; the existing unified policy
remains responsible for mesh I/O. The daily discovery job now runs the transport
regression test against the exact upstream headers it will build.

The PR unified CI lane now resolves the latest companion tag once, stages the
overlay on that tag and uses the same upstream source for discovery, host tests
and firmware builds. Its former use of the frozen root sources would miss the
release API regression. The native test environment now declares
`test_framework = googletest`, so PlatformIO reports all five tests instead of
zero parsed cases. The shared setup action also has its required description;
`actionlint` passes for both unified workflows and the unit-test lane.

## Reproduction

All staged source and build artifacts live inside this worktree under `.pio/`.
The staging copy is an archive of the fetched tag, with only the files in
`tools/apply_unified_overlay.py` copied on top; it is not another repository.

```sh
git fetch https://github.com/meshcore-dev/MeshCore.git refs/tags/companion-v1.17.1:refs/tags/companion-v1.17.1
mkdir -p .pio/upstream-companion-v1.17.1
git archive companion-v1.17.1 | tar -x -C .pio/upstream-companion-v1.17.1
python3 tools/apply_unified_overlay.py .pio/upstream-companion-v1.17.1
python3 .pio/upstream-companion-v1.17.1/tools/generate_unified_builds.py
cd .pio/upstream-companion-v1.17.1
pio run -c .pio/unified-platformio.ini -e Heltec_t114_without_display_companion_radio_unified -j 4
```

## Verification in progress

Host C++ regression test: PASS with `g++ -std=c++17 -Wall -Wextra -Werror`,
using `test/unified`, `examples/unified_radio` and the staged upstream `src`
include paths. Python generator and validator tests: 3/3 PASS. Root native
utility tests: 5/5 PASS, correctly reported by PlatformIO.

`pio run -c .pio/unified-platformio.ini -e Heltec_t114_without_display_companion_radio_unified -j 4`:
PASS, compile and link, 53.71 seconds. The same target with the frozen overlay
failed locally with the expected `UITask` constructor error.

Additional local builds and [branch workflow 37009648483](https://github.com/hermes-gadget/MeshCore-Unified-Open/actions/runs/37009648483)
are running. That workflow uses firmware commit `357e3c8f` and explicitly sets
`upstream_ref=companion-v1.17.1` and `publish_release=false`. Subsequent commits
change CI and native-test configuration, not firmware sources. No claim of
matrix-wide success is made here until actual build evidence is recorded.
