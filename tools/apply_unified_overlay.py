#!/usr/bin/env python3
"""Copy the unified companion overlay into a MeshCore source checkout."""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path


OVERLAY_FILES = (
    "examples/unified_radio/UnifiedFirmwareConfig.h",
    "examples/unified_radio/UnifiedTransportConfig.h",
    "examples/unified_radio/UnifiedTransportManager.h",
    "examples/unified_radio/UnifiedTransportManager.cpp",
    "examples/unified_radio/main.cpp",
    "examples/unified_radio/partitions_4mb.csv",
    "examples/unified_radio/psram_stub.c",
    "variants/lilygo_tdeck/partitions_8mb.csv",
    "tools/generate_unified_builds.py",
    "tools/validate_unified_builds.py",
)


def apply_overlay(destination: Path) -> None:
    overlay_root = Path(__file__).resolve().parents[1]
    destination = destination.resolve()
    if not (destination / "platformio.ini").is_file():
        raise SystemExit(f"Not a MeshCore checkout: {destination}")
    if destination == overlay_root:
        raise SystemExit("Refusing to overlay the implementation onto itself")

    for relative_name in OVERLAY_FILES:
        source = overlay_root / relative_name
        target = destination / relative_name
        if not source.is_file():
            raise SystemExit(f"Missing overlay source: {source}")
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)

    print(f"Applied {len(OVERLAY_FILES)} unified files to {destination}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("destination", type=Path, help="MeshCore checkout to modify")
    args = parser.parse_args()
    apply_overlay(args.destination)


if __name__ == "__main__":
    main()
