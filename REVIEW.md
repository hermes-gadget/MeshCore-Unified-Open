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
| STM32 helper portability | The PR's existing `wio-e5-mini_repeater` check exposed `ltoa` missing from the STM32 core. Use standard `snprintf` for the nonnegative 32-bit integer part; backport only this conversion when applying the overlay, preserving the rest of the upstream helper. |

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
The staging copy is an archive of the fetched tag, with the ten files in
`tools/apply_unified_overlay.py` copied on top and the portable integer
conversion patched in place; it is not another repository.

```sh
git fetch https://github.com/meshcore-dev/MeshCore.git refs/tags/companion-v1.17.1:refs/tags/companion-v1.17.1
mkdir -p .pio/upstream-companion-v1.17.1
git archive companion-v1.17.1 | tar -x -C .pio/upstream-companion-v1.17.1
python3 tools/apply_unified_overlay.py .pio/upstream-companion-v1.17.1
python3 .pio/upstream-companion-v1.17.1/tools/generate_unified_builds.py
cd .pio/upstream-companion-v1.17.1
pio run -c .pio/unified-platformio.ini -e Heltec_t114_without_display_companion_radio_unified -j 4
```

## Local compile and link evidence

PlatformIO Core 6.1.19. Commands below were run in
`.pio/upstream-companion-v1.17.1`, built from the exact upstream commit above.
All eleven staged source files (ten overlay files plus the patched helper)
were checked byte-for-byte against the branch. The local sample has **26
compile-and-link successes out of 30 attempts**. The four STM32 attempts are
not counted as working firmware.

| Exact command | Result | Seconds |
| --- | --- | ---: |
| `pio run -c .pio/unified-platformio.ini -e Heltec_t114_without_display_companion_radio_unified -j 4` | PASS, compile + link | 15.77 |
| `pio run -c .pio/unified-platformio.ini -e Xiao_S3_WIO_companion_radio_unified -j 4` | PASS, compile + link | 25.1 |
| `pio run -c .pio/unified-platformio.ini -e GAT562_30S_Mesh_Kit_companion_radio_unified -j 4` | PASS, compile + link | 12.15 |
| `pio run -c .pio/unified-platformio.ini -e Heltec_E290_companion_radio_unified -j 4` | PASS, compile + link | 27.72 |
| `pio run -c .pio/unified-platformio.ini -e LilyGo_TDeck_companion_radio_unified -j 4` | PASS, compile + link | 46.66 |
| `pio run -c .pio/unified-platformio.ini -e Heltec_v3_companion_radio_unified -j 4` | PASS, compile + link | 23.67 |
| `pio run -c .pio/unified-platformio.ini -e ThinkNode_M7_companion_radio_unified -j 4` | PASS, compile + link | 22.29 |
| `pio run -c .pio/unified-platformio.ini -e Heltec_t096_companion_radio_unified -j 4` | PASS, compile + link | 13.1 |
| `pio run -c .pio/unified-platformio.ini -e Heltec_mesh_solar_companion_radio_unified -j 4` | PASS, compile + link | 8.04 |
| `pio run -c .pio/unified-platformio.ini -e RAK_4631_companion_radio_unified -j 4` | PASS, compile + link | 12.21 |
| `pio run -c .pio/unified-platformio.ini -e LilyGo_TDeck_8MB_companion_radio_unified -j 4` | PASS, compile + link | 25.68 |
| `pio run -c .pio/unified-platformio.ini -e LilyGo_T-Echo_Card_companion_radio_unified -j 4` | PASS, compile + link | 83.68 |
| `pio run -c .pio/unified-platformio.ini -e RAK_3x72_companion_radio_unified -j 4` | FAIL, upstream RadioLib/core enum API | 28.46 |
| `pio run -c .pio/unified-platformio.ini -e Tiny_Relay_companion_radio_unified -j 4` | FAIL, upstream RadioLib/core enum API | 30.5 |
| `pio run -c .pio/unified-platformio.ini -e wio-e5_companion_radio_unified -j 4` | FAIL, upstream RadioLib/core enum API | 45.17 |
| `pio run -c .pio/unified-platformio.ini -e wio-e5-mini_companion_radio_unified -j 4` | FAIL, upstream RadioLib/core enum API | 34.9 |

The sample covers ui-new (including e-ink and NullDisplayDriver), ui-orig with
ThinkNode M7 Ethernet, ui-tiny on T-Echo Card, external watchdog servicing on
Heltec Mesh Solar, classic ESP32, ESP32-S3, nRF52 with extra storage, and the
8 MB/no-PSRAM T-Deck variant. All five required representative targets pass.

The root checkout also passes `pio run -e wio-e5-mini_repeater -j 4`:
compile + link, 221.79 seconds, 196976 bytes of flash. This fixes the previously
red non-unified STM32 PR check without changing board behavior.

## Remaining upstream failures

Four local STM32 unified attempts fail in the pinned RadioLib HAL: conversion
from `uint32_t` to the newer core's `PinMode`/`PinStatus` enums. Their **untouched**
upstream companion baselines independently fail because the core lacks `ltoa`.
The validator temporarily restores the original helper before a baseline and
restores the backport afterward, including when a build raises an exception.
An additional build of upstream's own `wio-e5-mini_companion_radio_usb` with only
the portable helper fix also fails with the same RadioLib enum errors, proving
that the unified entry point is not responsible for that dependency failure.

Exact classification command, run in the staged tree:

```sh
python3 tools/validate_unified_builds.py --target RAK_3x72_companion_radio_unified --target Tiny_Relay_companion_radio_unified --target wio-e5_companion_radio_unified --target wio-e5-mini_companion_radio_unified --results ../review-evidence/stm32-validation-results.json --logs-dir ../review-evidence/stm32-validation-logs
```

Result: four `upstream_failure`, zero `unified_failure`. The additional dependency
isolation command was `pio run -e wio-e5-mini_companion_radio_usb -j 4` with the
portable helper present; it failed with `PinMode`/`PinStatus` conversion errors.

The Generic ESP-NOW and SenseCap Indicator ESP-NOW unified targets and their
untouched baselines lack `P_LORA_DIO_1` in `src/helpers/ESP32Board.h`. Their
green jobs are classified skips, not successful firmware builds. The complete
named target list and final matrix counts below come from the completed logs.

## Regression and workflow checks

- `python3 -m unittest discover -s test/unified -p 'test_*.py' -v`: 6/6 PASS after the resume fix.
- `pio test -e native -vv`: 5/5 PASS with the correct GoogleTest runner.
- Actual upstream interface contract regression: PASS with the exact command below.
- Portable helper backport: applied to actual v1.17.1 source, preserves additional
  upstream content, is idempotent, and is restored after all four baseline builds.
- `actionlint .github/workflows/unified-upstream-release.yml .github/workflows/unified-companion-ci.yml .github/workflows/run-unit-tests.yml`: PASS.

```sh
g++ -std=c++17 -Wall -Wextra -Werror -I test/unified -I examples/unified_radio -I .pio/upstream-companion-v1.17.1/src test/unified/test_transport_manager.cpp examples/unified_radio/UnifiedTransportManager.cpp -o .pio/review-evidence/test-transport-manager
.pio/review-evidence/test-transport-manager
```

## Completed matrix verification

[Release verification run 37012566537](https://github.com/hermes-gadget/MeshCore-Unified-Open/actions/runs/37012566537) and [unified PR CI run 37012568820](https://github.com/hermes-gadget/MeshCore-Unified-Open/actions/runs/37012568820) both completed successfully on source commit `bbb53add652f7a20d1f785b4a932e6aa0fc029d3`. Both independently attempted all **99 targets** and agree on every target classification.

**93/99 compile-and-link successes; 6 upstream failures/skips; zero overlay failures.** All 74 previously failed build targets now compile and link in both runs; their names are recorded in the evidence file. The 6 skipped targets remain unverified as working firmware and are not counted as fixed.

| Unverified target | Untouched upstream environment | Build evidence |
| --- | --- | --- |
| `Generic_ESPNOW_companion_radio_unified` | `Generic_ESPNOW_comp_radio_usb` | [job log](https://github.com/hermes-gadget/MeshCore-Unified-Open/actions/runs/37012566537/job/110856526385) |
| `RAK_3x72_companion_radio_unified` | `RAK_3x72_companion_radio_usb` | [job log](https://github.com/hermes-gadget/MeshCore-Unified-Open/actions/runs/37012566537/job/110856537994) |
| `SenseCapIndicator-ESPNow_companion_radio_unified` | `SenseCapIndicator-ESPNow_comp_radio_usb` | [job log](https://github.com/hermes-gadget/MeshCore-Unified-Open/actions/runs/37012566537/job/110856539078) |
| `Tiny_Relay_companion_radio_unified` | `Tiny_Relay_companion_radio_usb` | [job log](https://github.com/hermes-gadget/MeshCore-Unified-Open/actions/runs/37012566537/job/110856541530) |
| `wio-e5-mini_companion_radio_unified` | `wio-e5-mini_companion_radio_usb` | [job log](https://github.com/hermes-gadget/MeshCore-Unified-Open/actions/runs/37012566537/job/110856542433) |
| `wio-e5_companion_radio_unified` | `wio-e5_companion_radio_usb` | [job log](https://github.com/hermes-gadget/MeshCore-Unified-Open/actions/runs/37012566537/job/110856542491) |

The [complete per-target evidence](docs/unified-build-evidence-20261002.json) names every verified and unverified target, its upstream source environment, exact PIO command, classification and public job log. This is a compile verification result, not a claim that all 99 firmware images work.

CI invokes `python3 tools/validate_unified_builds.py --target <target> --verbose`
in the staged upstream tree (the release lane also adds `--github-output`). It
runs `pio run -c .pio/unified-platformio.ini -e <target>`, then
`pio run -e <source_environment>` on failure with the helper backport removed.
Release compile flags set firmware version `v1.17.1` and date `02-Oct-2026`;
the PR lane uses `ci` and `01-Jan-1970`.

The release dispatch used `upstream_ref=companion-v1.17.1` and `publish_release=false`; ESP32 merged images and nRF52 UF2 packaging also passed for successful targets. No release was published. Native CI and all eleven non-unified PR build checks pass. Root native tests are 5/5 and Python overlay tests are 4/4.

[PR #4](https://github.com/hermes-gadget/MeshCore-Unified-Open/pull/4) targets `main` and follows [issue #3](https://github.com/hermes-gadget/MeshCore-Unified-Open/issues/3). It is not merged. The daily schedule is active; it continues using main until the owner merges the port. No hardware testing or wiring was performed.

## Continuation verification

The final audit rechecked [PR CI run 37017833586](https://github.com/hermes-gadget/MeshCore-Unified-Open/actions/runs/37017833586) on source commit `93f1bd5d7217656554e50b8e164822f12f427772`: **93 compile/link passes, six upstream failures, zero unified failures**. Every passing target has an explicit linker step and `[SUCCESS]` in the downloaded logs; the named 74 old failed jobs independently match the previously-failed list. The generated manifest and configuration reproduce byte-for-byte, and all 863 untouched staged upstream files match the pinned tag.

The deep review found that `--resume` could reuse an old result after an ignored archive
source edit: Git resolved the enclosing overlay checkout instead of the staged sources.
Commit `242e9e1e` adds source-content and pristine-helper-snapshot hashing while excluding
compiler output and Python caches. The real ignored-directory reproduction now invalidates
the fingerprint; two regression tests cover source edits, additions/deletions, standalone
archives, baseline configuration/snapshots and excluded build output.

Repeated host checks pass: native utilities **5/5**, Python regressions **6/6**,
`actionlint`, and the upstream transport contract with
`-fsanitize=address,undefined`. No firmware behavior changed in this continuation.

The additional board batch has **14 of 14** compile/link passes recorded so far.

| Board batch | Exact command (run in the staged upstream tree) | Result | Seconds |
| --- | --- | --- | ---: |
| Heltec nRF52 | `pio run -c .pio/unified-platformio.ini -e Heltec_t114_companion_radio_unified -j 4` | PASS, compile + link | 110.12 |
| Heltec nRF52 | `pio run -c .pio/unified-platformio.ini -e Heltec_t1_companion_radio_unified -j 4` | PASS, compile + link | 69.48 |
| Heltec nRF52 | `pio run -c .pio/unified-platformio.ini -e Heltec_tower_v2_companion_radio_unified -j 4` | PASS, compile + link | 59.08 |
| Heltec ESP32 | `pio run -c .pio/unified-platformio.ini -e Heltec_E213_companion_radio_unified -j 4` | PASS, compile + link | 100.53 |
| Heltec ESP32 | `pio run -c .pio/unified-platformio.ini -e heltec_v4_companion_radio_unified -j 4` | PASS, compile + link | 85.07 |
| Heltec ESP32 | `pio run -c .pio/unified-platformio.ini -e heltec_v4_tft_companion_radio_unified -j 4` | PASS, compile + link | 91.72 |
| Heltec ESP32 | `pio run -c .pio/unified-platformio.ini -e heltec_v4_r8_companion_radio_unified -j 4` | PASS, compile + link | 83.54 |
| Heltec ESP32 | `pio run -c .pio/unified-platformio.ini -e heltec_v4_r8_tft_companion_radio_unified -j 4` | PASS, compile + link | 147.63 |
| Heltec ESP32 | `pio run -c .pio/unified-platformio.ini -e heltec_tracker_v2_companion_radio_unified -j 4` | PASS, compile + link | 118.21 |
| ThinkNode | `pio run -c .pio/unified-platformio.ini -e ThinkNode_M1_companion_radio_unified -j 4` | PASS, compile + link | 118.82 |
| ThinkNode | `pio run -c .pio/unified-platformio.ini -e ThinkNode_M2_companion_radio_unified -j 4` | PASS, compile + link | 89.86 |
| ThinkNode | `pio run -c .pio/unified-platformio.ini -e ThinkNode_M3_companion_radio_unified -j 4` | PASS, compile + link | 48.38 |
| ThinkNode | `pio run -c .pio/unified-platformio.ini -e ThinkNode_M5_companion_radio_unified -j 4` | PASS, compile + link | 171.66 |
| ThinkNode | `pio run -c .pio/unified-platformio.ini -e ThinkNode_M9_companion_radio_unified -j 4` | PASS, compile + link | 97.12 |

Each new target was a failed job in daily run `36996304889`. Logs, command lines,
source revisions, log hashes and linked-ELF hashes are in `.pio/review-evidence/round2/`
and the committed evidence JSON. The verified/unverified matrix totals remain **93/6**;
additional local passes increase independent verification, not the overall target count.

## Safety checkpoint verification

[PR CI run 37031501709](https://github.com/hermes-gadget/MeshCore-Unified-Open/actions/runs/37031501709)
completed successfully on `7fb9408d521183d97a2eb1ffb534ab51d28f10a0`, including the
resume-validation fix and all fourteen additional local board receipts. An individual
audit of all 99 job logs confirms **93 compile/link passes, six upstream failures,
zero unified failures**, with every one of the 74 previously failing targets linked.
The evidence JSON preserves each checkpoint job URL and downloaded log SHA-256.

The real `--resume` integration check reused an unchanged T114 success, then rebuilt
successfully after adding a temporary ignored staged-source file. The probe was
removed after verification. This confirms that the fingerprint fix affects the
actual CLI path as well as the six passing Python regression tests.

## Additional checkpoint board builds

The final-window batch has **10** additional independent compile/link passes.
Local coverage is now **36 passes in 41 unique target attempts**,
with four documented upstream STM32 failures and one interrupted local build. The full CI totals remain 93/6.

| Target | Exact command in staged upstream tree | Seconds |
| --- | --- | ---: |
| `GAT562_Mesh_Tracker_Pro_companion_radio_unified` | `pio run -c .pio/unified-platformio.ini -e GAT562_Mesh_Tracker_Pro_companion_radio_unified -j 4` | 105.18 |
| `GAT562_Mesh_Watch13_companion_radio_unified` | `pio run -c .pio/unified-platformio.ini -e GAT562_Mesh_Watch13_companion_radio_unified -j 4` | 73.01 |
| `LilyGo_T-Echo-Lite_companion_radio_unified` | `pio run -c .pio/unified-platformio.ini -e LilyGo_T-Echo-Lite_companion_radio_unified -j 4` | 63.88 |
| `LilyGo_T-Echo_companion_radio_unified` | `pio run -c .pio/unified-platformio.ini -e LilyGo_T-Echo_companion_radio_unified -j 4` | 81.89 |
| `RAK_3401_companion_radio_unified` | `pio run -c .pio/unified-platformio.ini -e RAK_3401_companion_radio_unified -j 4` | 78.26 |
| `RAK_WisMesh_Tag_companion_radio_unified` | `pio run -c .pio/unified-platformio.ini -e RAK_WisMesh_Tag_companion_radio_unified -j 4` | 64.35 |
| `Xiao_nrf52_companion_radio_unified` | `pio run -c .pio/unified-platformio.ini -e Xiao_nrf52_companion_radio_unified -j 4` | 99.36 |
| `WioTrackerL1Eink_companion_radio_unified` | `pio run -c .pio/unified-platformio.ini -e WioTrackerL1Eink_companion_radio_unified -j 4` | 141.91 |
| `Mesh_pocket_companion_radio_unified` | `pio run -c .pio/unified-platformio.ini -e Mesh_pocket_companion_radio_unified -j 4` | 62.7 |
| `t1000e_companion_radio_unified` | `pio run -c .pio/unified-platformio.ini -e t1000e_companion_radio_unified -j 4` | 45.24 |

The tracked JSON includes source revisions, log and linked-ELF hashes, and essential
build-log receipts (platform, memory use, linker and success lines). No hardware was used.

## PlatformIO override resume verification

Commit `d5607b17` also invalidates resumed results when `PLATFORMIO_*`
environment overrides change. The CLI regression first reproduced a stale
success after changing `PLATFORMIO_BUILD_FLAGS`, then passed after the fix.
It confirms reuse for unchanged flags, rebuilding for changed or removed flags,
and reuse when an unrelated output variable changes. Override values enter
only the fingerprint hash. The complete Python suite now passes **7/7**;
firmware sources and generated build configuration remain unchanged.

The actual PlatformIO CLI also passed this sequence on
`Heltec_t114_companion_radio_unified`: initial build **29.92 s**, unchanged
override skipped **0.17 s**, changed override rebuilt and linked **35.15 s**.
Both real builds contain linker and success markers. All three commands,
source revision, fingerprints and log hashes are preserved in the evidence JSON.
[CI discovery](https://github.com/hermes-gadget/MeshCore-Unified-Open/actions/runs/37038533330/job/110942617552)
on `0795944a` independently passes all seven Python regressions and the upstream
transport contract; its native CI lane also passes.

## Superseded matrix cancellation

Commit `c6feb6ce` adds workflow concurrency keyed by workflow name and Git ref.
Newer PR or branch updates cancel superseded unified matrices, avoiding duplicate
99-target jobs after frequent checkpoint pushes. All three affected workflow
lint checks pass. This uses [GitHub's documented concurrency behavior](https://docs.github.com/en/actions/how-tos/write-workflows/choose-when-workflows-run/control-workflow-concurrency).

Live verification confirms that [run 37040477154](https://github.com/hermes-gadget/MeshCore-Unified-Open/actions/runs/37040477154)
on `c6feb6ce` was cancelled after the newer documentation checkpoint queued
[run 37040575781](https://github.com/hermes-gadget/MeshCore-Unified-Open/actions/runs/37040575781),
without a manual cancellation request for the superseded matrix.
The evidence also preserves 33 audited target logs from the validator follow-up
on `53387433`: 32 firmware links and one known upstream failure, before that
older run was manually retired. This partial audit does not count cancelled jobs
as verified firmware or change the complete 93/6 matrix result.

## Completed final follow-up matrix

[PR CI run 37045319718](https://github.com/hermes-gadget/MeshCore-Unified-Open/actions/runs/37045319718)
completed successfully on `c9b01670`, including the PlatformIO override resume
fix and workflow concurrency. All 99 target logs were individually audited:
**93 firmware links, six reproduced upstream failures, zero overlay failures**.
All 74 previously failing daily targets link, and all **112 PR checks pass**
on this audited revision. The evidence JSON records every final job URL and log
hash. The ten additional checkpoint boards all passed locally, bringing local
coverage to **36 successes in 41 unique target attempts**, including one interrupted attempt. No hardware was used.

The e-ink WioTracker L1 and Mesh Pocket follow-up builds use source commit
`c9b016702673b4bc5923af2b7a251738dae2d332` against the same pinned upstream release; each has
an explicit linker step and `[SUCCESS]`. The complete CI matrix remains 93/6.

## Owner wrap-up handoff

Stopped further local builds on Ben's explicit wrap-up signal. The completed
T1000-E build is saved with linker/success receipts and log/ELF hashes;
`MeshTracker_X1_companion_radio_unified` was interrupted and is **not locally
verified**. Its exact attempted command was
`pio run -c .pio/unified-platformio.ini -e MeshTracker_X1_companion_radio_unified -j 4`
in the staged upstream tree. It is independently compile/link verified in the
fully audited C9 CI matrix. Local totals are 36 successful links, four reproduced
upstream failures, and one interrupted attempt across 41 unique targets.

The last fully audited source revision is `c9b016702673b4bc5923af2b7a251738dae2d332`:
99 targets individually audited, 93 links, six upstream failures, zero overlay
failures; all 112 PR checks passed. Later commits change documentation and
evidence only. Fresh CI for those commits is still pending at handoff and has
not been claimed as completed. Python regressions are 7/7, native cases 5/5,
and the actual upstream-header sanitizer contract and workflow lint passed.

The six targets listed in the upstream-failure table remain unverified as
working firmware: Generic_ESPNOW, SenseCapIndicator-ESPNow, RAK_3x72, Tiny_Relay,
wio-e5 and wio-e5-mini (each with `_companion_radio_unified`). The ESPNow
baselines lack `P_LORA_DIO_1`; the STM32 baselines reproduce upstream formatting
and strict-enum incompatibilities. They are reported as upstream failures,
not firmware passes. Of the 93 CI-linked targets, 36 also link locally and
57 have CI-only compile verification.

PR #4 remains open for the owner's review and merge. Main is unchanged, so the
daily workflow does not gain the port until that merge. Release packaging was
verified in a `publish_release=false` dry run; no release was published.
No hardware testing was performed. All source fixes and compact build receipts
are committed; transient staged sources, build products and raw logs remain
ignored, with their relevant commands, source revisions and hashes in the
tracked evidence JSON. No further scope was started after the stop signal.
