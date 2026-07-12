#!/usr/bin/env python3
"""Generate unified companion PlatformIO environments from upstream targets.

The upstream project has one or more companion environments per hardware
variant.  This script resolves those environments through PlatformIO, groups
the transport-specific builds by device, and emits one self-contained unified
environment per device.  Keeping this generated layer out of variant files
means new upstream devices are picked up automatically.
"""

from __future__ import annotations

import argparse
import copy
import json
import re
import subprocess
from collections import OrderedDict
from pathlib import Path


TRANSPORT_RE = re.compile(
    r"^(?P<device>.+)_(?:companion_radio|companion|comp_radio)_"
    r"(?P<transport>ble|wifi|usb|serial)_?$",
    re.IGNORECASE,
)
PREFERENCE = {"ble": 0, "wifi": 1, "usb": 2, "serial": 3}
LEGACY_WIFI_DEFINE_RE = re.compile(
    r"(?:^|\s)-D\s*WIFI_(?:SSID|PWD)(?:=(?:\"[^\"]*\"|'[^']*'|\S+))?"
)
KNOWN_FOUR_MB_ESP32_BOARDS = {
    "esp32-c3-devkitm-1",
    "esp32-c6-devkitm-1",
    "esp32doit-devkit-v1",
    "seeed_xiao_esp32c3",
    "ttgo-lora32-v1",
    "ttgo-t-beam",
}


def resolved_config(project_dir: Path) -> list:
    result = subprocess.run(
        ["pio", "project", "config", "--json-output"],
        cwd=project_dir,
        check=True,
        capture_output=True,
        text=True,
    )
    return json.loads(result.stdout)


def append_unique(values: list[str], value: str) -> None:
    if value not in values:
        values.append(value)


def quote_value(value: object) -> str:
    if isinstance(value, bool):
        return "yes" if value else "no"
    return str(value)


def write_option(handle, key: str, value: object) -> None:
    if isinstance(value, list):
        handle.write(f"{key} =\n")
        for item in value:
            handle.write(f"  {quote_value(item)}\n")
    else:
        handle.write(f"{key} = {quote_value(value)}\n")


def _size_in_bytes(value: object) -> int | None:
    if isinstance(value, int):
        return value
    match = re.fullmatch(r"\s*(\d+)\s*([KMG]?B)?\s*", str(value), re.IGNORECASE)
    if not match:
        return None
    multiplier = {None: 1, "B": 1, "KB": 1024, "MB": 1024 * 1024}[
        match.group(2).upper() if match.group(2) else None
    ]
    return int(match.group(1)) * multiplier


def local_board_manifest(project_dir: Path, board: str) -> dict:
    path = project_dir / "boards" / f"{board}.json"
    if not path.is_file():
        return {}
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
        return value if isinstance(value, dict) else {}
    except (OSError, json.JSONDecodeError):
        return {}


def esp32_flash_size(project_dir: Path, options: OrderedDict) -> int | None:
    """Determine flash capacity without installing every target platform.

    PlatformIO's resolved project config omits board-manifest upload fields.
    Prefer explicit environment settings and repository-local manifests, which
    also makes newly added upstream custom boards automatic. Standard board
    definitions need a small fallback because their manifests live inside a
    platform package that may not be installed during matrix discovery.
    """
    explicit_size = _size_in_bytes(options.get("board_build.flash_size", ""))
    if explicit_size is not None:
        return explicit_size

    board = str(options.get("board", ""))
    board_data = local_board_manifest(project_dir, board)
    upload = board_data.get("upload", {})
    if isinstance(upload, dict):
        manifest_size = _size_in_bytes(
            upload.get("flash_size", upload.get("maximum_size", ""))
        )
        if manifest_size is not None:
            return manifest_size

    if board in KNOWN_FOUR_MB_ESP32_BOARDS:
        return 4 * 1024 * 1024
    return None


def configure_esp32_partitions(
    project_dir: Path, options: OrderedDict
) -> None:
    flash_size = esp32_flash_size(project_dir, options)
    current = options.get("board_build.partitions")

    # Four-megabyte unified images need a single large application slot. Keep
    # persistent SPIFFS data even though these constrained boards lose OTA.
    if flash_size == 4 * 1024 * 1024 or current in {
        "min_spiffs.csv",
        "max_app_4MB.csv",
    }:
        options["board_build.partitions"] = (
            "examples/unified_radio/partitions_4mb.csv"
        )
        options["board_upload.maximum_size"] = 0x300000
        return

    # Some custom 8/16 MB board manifests declare the physical flash size but
    # no partition layout, causing Arduino's 1.25 MiB default app slot to be
    # used. Select the matching dual-OTA layout without overriding a deliberate
    # custom partition table.
    if current not in {None, "default.csv"}:
        return
    if flash_size == 8 * 1024 * 1024:
        options["board_build.partitions"] = "default_8MB.csv"
        options["board_upload.maximum_size"] = 0x330000
    elif flash_size is not None and flash_size >= 16 * 1024 * 1024:
        options["board_build.partitions"] = "default_16MB.csv"
        options["board_upload.maximum_size"] = 0x640000


def esp32_architecture(project_dir: Path, options: OrderedDict) -> str:
    board = str(options.get("board", ""))
    board_data = local_board_manifest(project_dir, board)
    build = board_data.get("build", {})
    mcu = str(build.get("mcu", "")) if isinstance(build, dict) else ""
    identifier = f"{board} {mcu}".lower()
    return "esp32-c6" if re.search(r"esp32[-_]?c6", identifier) else "esp32"


def strip_legacy_wifi_credentials(flags: list[str]) -> list[str]:
    cleaned = []
    for flag in flags:
        value = LEGACY_WIFI_DEFINE_RE.sub("", flag).strip()
        if value:
            cleaned.append(value)
    return cleaned


def cap_numeric_define(flags: list[str], name: str, maximum: int) -> list[str]:
    """Cap an existing numeric build define without increasing its default."""
    pattern = re.compile(
        rf"(?<!\S)-D\s*{re.escape(name)}(?:\s*=\s*|\s+)(?P<value>\d+)(?=\s|$)"
    )
    values = [
        int(match.group("value"))
        for flag in flags
        for match in pattern.finditer(flag)
    ]
    if not values or max(values) <= maximum:
        return flags

    cleaned = []
    for flag in flags:
        value = pattern.sub("", flag).strip()
        if value:
            cleaned.append(value)
    append_unique(cleaned, f"-D {name}={maximum}")
    return cleaned


def generate(project_dir: Path, output: Path) -> list[dict[str, object]]:
    sections = resolved_config(project_dir)
    environments: dict[str, OrderedDict] = {}
    groups: dict[str, list[tuple[str, str]]] = {}

    for section_name, raw_options in sections:
        if not section_name.startswith("env:"):
            continue
        env_name = section_name[4:]
        environments[env_name] = OrderedDict(raw_options)
        match = TRANSPORT_RE.match(env_name)
        if match:
            groups.setdefault(match.group("device"), []).append(
                (env_name, match.group("transport").lower())
            )

    manifest: list[dict[str, object]] = []
    generated: list[tuple[str, OrderedDict]] = []
    for device in sorted(groups, key=str.casefold):
        choices = sorted(groups[device], key=lambda item: PREFERENCE[item[1]])
        source_env, _ = choices[0]
        transports = {transport for _, transport in choices}
        options = OrderedDict(environments[source_env])
        options.pop("extends", None)

        flags = strip_legacy_wifi_credentials(list(options.get("build_flags", [])))
        append_unique(flags, "-I examples/companion_radio")
        append_unique(flags, "-I examples/unified_radio")
        append_unique(flags, "-D UNIFIED_FIRMWARE=1")
        append_unique(flags, "-D UNIFIED_TRANSPORT_USB=1")
        has_ble = "ble" in transports
        has_wifi = any("ESP32_PLATFORM" in flag for flag in flags)
        architecture = (
            esp32_architecture(project_dir, options)
            if has_wifi
            else "nrf52"
            if any("NRF52_PLATFORM" in flag for flag in flags)
            else "rp2040"
            if any("RP2040_PLATFORM" in flag for flag in flags)
            else "stm32"
            if any("STM32_PLATFORM" in flag for flag in flags)
            else "unknown"
        )
        # Running BLE and WiFi together adds static interface buffers. Several
        # classic 4 MB ESP32 companions configure unusually large 128-entry
        # offline queues and otherwise exceed internal DRAM by about 1-2 KiB.
        # Preserve every transport while retaining a generous 96-entry queue.
        if (
            has_ble
            and has_wifi
            and architecture == "esp32"
            and esp32_flash_size(project_dir, options) == 4 * 1024 * 1024
        ):
            flags = cap_numeric_define(flags, "OFFLINE_QUEUE_SIZE", 96)
        append_unique(flags, f"-D UNIFIED_TRANSPORT_BLE={1 if has_ble else 0}")
        append_unique(flags, f"-D UNIFIED_TRANSPORT_WIFI={1 if has_wifi else 0}")
        if has_ble and not any("BLE_PIN_CODE" in flag for flag in flags):
            append_unique(flags, "-D BLE_PIN_CODE=123456")
        options["build_flags"] = flags

        sources = list(options.get("build_src_filter", []))
        # The upstream companion code remains authoritative.  Only its entry
        # point is replaced by the unified entry point and transport manager.
        append_unique(sources, "-<../examples/companion_radio/main.cpp>")
        append_unique(sources, "+<../examples/unified_radio/main.cpp>")
        append_unique(sources, "+<../examples/unified_radio/UnifiedTransportManager.cpp>")
        append_unique(sources, "+<helpers/ArduinoSerialInterface.cpp>")
        if not has_ble:
            append_unique(sources, "-<helpers/esp32/SerialBLEInterface.cpp>")
            append_unique(sources, "-<helpers/nrf52/SerialBLEInterface.cpp>")
        if has_wifi:
            append_unique(sources, "+<helpers/esp32/SerialWifiInterface.cpp>")
        options["build_src_filter"] = sources

        if has_wifi:
            configure_esp32_partitions(project_dir, options)

        target = f"{device}_companion_radio_unified"
        generated.append((target, options))
        manifest.append(
            {
                "target": target,
                "source_environment": source_env,
                "platform": "esp32" if has_wifi else architecture,
                "architecture": architecture,
                "transports": [
                    name for name, enabled in
                    (("usb", True), ("ble", has_ble), ("wifi", has_wifi)) if enabled
                ],
            }
        )

        # This repository supports an 8 MB / no-PSRAM T-Deck hardware variant
        # that is not present as a separate upstream companion environment.
        # Keep it generated from the same authoritative T-Deck definition.
        partition_file = project_dir / "variants/lilygo_tdeck/partitions_8mb.csv"
        psram_stub = project_dir / "examples/unified_radio/psram_stub.c"
        if device == "LilyGo_TDeck" and partition_file.exists() and psram_stub.exists():
            special_options = copy.deepcopy(options)
            special_options["board"] = "esp32-s3-devkitc-1"
            special_options["board_build.flash_size"] = "8MB"
            special_options["board_build.partitions"] = "variants/lilygo_tdeck/partitions_8mb.csv"
            special_options["board_build.flash_mode"] = "dio"
            special_options["board_build.psram"] = False
            special_flags = special_options["build_flags"]
            append_unique(special_flags, "-UBOARD_HAS_PSRAM")
            append_unique(special_flags, "-D BOARD_HAS_PSRAM=0")
            append_unique(special_flags, "-Wl,--wrap=esp_spiram_init")
            append_unique(special_flags, "-Wl,--wrap=esp_spiram_init_cache")
            append_unique(special_flags, "-Wl,--wrap=esp_spiram_test")
            append_unique(special_flags, "-Wl,--wrap=esp_spiram_add_to_heapalloc")
            append_unique(
                special_options["build_src_filter"],
                "+<../examples/unified_radio/psram_stub.c>",
            )
            special_target = "LilyGo_TDeck_8MB_companion_radio_unified"
            generated.append((special_target, special_options))
            manifest.append(
                {
                    "target": special_target,
                    "source_environment": source_env,
                    "platform": "esp32",
                    "architecture": "esp32",
                    "transports": ["usb", "ble", "wifi"],
                }
            )

    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", encoding="utf-8") as handle:
        handle.write("; Generated by tools/generate_unified_builds.py; do not edit.\n")
        handle.write("[platformio]\n")
        handle.write("src_dir = src\n\n")
        for target, options in generated:
            handle.write(f"[env:{target}]\n")
            for key, value in options.items():
                write_option(handle, key, value)
            handle.write("\n")

    return manifest


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default=".pio/unified-platformio.ini")
    parser.add_argument("--manifest", default=".pio/unified-targets.json")
    parser.add_argument("--list", action="store_true", help="print generated target names")
    args = parser.parse_args()

    project_dir = Path(__file__).resolve().parents[1]
    output = (project_dir / args.output).resolve()
    manifest_path = (project_dir / args.manifest).resolve()
    manifest = generate(project_dir, output)
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    if args.list:
        print("\n".join(item["target"] for item in manifest))
    else:
        print(f"Generated {len(manifest)} unified targets in {output}")


if __name__ == "__main__":
    main()
