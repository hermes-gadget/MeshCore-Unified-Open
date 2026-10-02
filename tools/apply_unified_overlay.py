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

    # STM32's Arduino core does not provide ltoa. Backport only this conversion
    # rather than freezing the rest of the upstream formatting helper.
    helper = destination / "src/helpers/TxtDataHelpers.cpp"
    if helper.is_file():
        original = helper.read_text(encoding="utf-8")
        patched = original.replace(
            "ltoa(int_part, p, 10);",
            '// int_part is nonnegative int32_t: at most ten digits plus the terminator.\n'
            '    snprintf(p, 11, "%ld", static_cast<long>(int_part));',
        )
        if patched != original:
            originals = destination / ".pio/unified-upstream-originals"
            originals.mkdir(parents=True, exist_ok=True)
            (originals / "TxtDataHelpers.cpp").write_text(original, encoding="utf-8")
            (originals / "TxtDataHelpers.cpp.patched").write_text(patched, encoding="utf-8")
            helper.write_text(patched, encoding="utf-8")
            print("Applied portable integer conversion to upstream TxtDataHelpers")

    print(f"Applied {len(OVERLAY_FILES)} unified files to {destination}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("destination", type=Path, help="MeshCore checkout to modify")
    args = parser.parse_args()
    apply_overlay(args.destination)


if __name__ == "__main__":
    main()
