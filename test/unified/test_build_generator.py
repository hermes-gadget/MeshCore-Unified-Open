import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "generate_unified_builds", ROOT / "tools/generate_unified_builds.py"
)
generator = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(generator)


class BuildGeneratorTest(unittest.TestCase):
    def test_groups_names_strips_credentials_and_sizes_c6_partition(self):
        sections = [
            (
                "env:Generic_ESPNOW_comp_radio_usb",
                [
                    ("build_flags", [
                        "-D ESP32_PLATFORM",
                        "-D KEEP_ME=1 -D WIFI_SSID='secret'",
                    ]),
                    ("build_src_filter", ["+<helpers/esp32/*.cpp>", "+<../examples/companion_radio/*.cpp>"]),
                    ("platform", "https://example/pioarduino/platform.zip"),
                    ("board", "esp32-c3-devkitm-1"),
                ],
            ),
            (
                "env:Custom_Future_companion_radio_ble",
                [
                    ("build_flags", [
                        "-D ESP32_PLATFORM",
                        "-D OFFLINE_QUEUE_SIZE=128",
                    ]),
                    ("build_src_filter", ["+<../examples/companion_radio/*.cpp>"]),
                    ("platform", "platformio/espressif32"),
                    ("board", "future-custom-board"),
                ],
            ),
            (
                "env:Large_Future_companion_radio_ble",
                [
                    ("build_flags", ["-D ESP32_PLATFORM"]),
                    ("build_src_filter", ["+<../examples/companion_radio/*.cpp>"]),
                    ("platform", "platformio/espressif32"),
                    ("board", "future-16mb-board"),
                ],
            ),
            (
                "env:Xiao_C6_companion_radio_ble_",
                [
                    ("build_flags", [
                        "-D ESP32_PLATFORM",
                        "-D BLE_PIN_CODE=123456",
                        "-D WIFI_SSID='secret'",
                        "-D WIFI_PWD='secret'",
                    ]),
                    ("build_src_filter", ["+<../examples/companion_radio/*.cpp>"]),
                    ("board_build.partitions", "min_spiffs.csv"),
                    ("platform", "https://example/pioarduino/platform.zip"),
                    ("board", "esp32-c6-devkitm-1"),
                ],
            ),
            (
                "env:Heltec_E290_companion_ble",
                [
                    ("build_flags", ["-D ESP32_PLATFORM", "-D BLE_PIN_CODE=123456"]),
                    ("build_src_filter", ["+<../examples/companion_radio/*.cpp>"]),
                    ("platform", "platformio/espressif32"),
                    ("board", "esp32-s3-devkitc-1"),
                ],
            ),
            (
                "env:LilyGo_TDeck_companion_radio_ble",
                [
                    ("build_flags", ["-D ESP32_PLATFORM", "-D BOARD_HAS_PSRAM=1"]),
                    ("build_src_filter", ["+<../examples/companion_radio/*.cpp>"]),
                    ("platform", "platformio/espressif32"),
                    ("board", "t-deck"),
                ],
            ),
            (
                "env:RAK_4631_companion_radio_ble",
                [
                    ("build_flags", ["-D NRF52_PLATFORM", "-D BLE_PIN_CODE=123456"]),
                    ("build_src_filter", ["+<../examples/companion_radio/*.cpp>"]),
                    ("platform", "nordicnrf52"),
                    ("board", "rak4631"),
                ],
            ),
        ]

        with tempfile.TemporaryDirectory() as tmp_name:
            project = Path(tmp_name)
            (project / "boards").mkdir()
            (project / "boards/future-custom-board.json").write_text(
                json.dumps({"upload": {"flash_size": "4MB"}}),
                encoding="utf-8",
            )
            (project / "boards/future-16mb-board.json").write_text(
                json.dumps({"upload": {"flash_size": "16MB"}}),
                encoding="utf-8",
            )
            (project / "variants/lilygo_tdeck").mkdir(parents=True)
            (project / "variants/lilygo_tdeck/partitions_8mb.csv").write_text(
                "# test partition\n", encoding="utf-8"
            )
            (project / "examples/unified_radio").mkdir(parents=True)
            (project / "examples/unified_radio/psram_stub.c").write_text(
                "/* test stub */\n", encoding="utf-8"
            )
            output = project / "generated.ini"
            with patch.object(generator, "resolved_config", return_value=sections):
                manifest = generator.generate(project, output)

            by_target = {item["target"]: item for item in manifest}
            self.assertEqual(by_target["Xiao_C6_companion_radio_unified"]["architecture"], "esp32-c6")
            self.assertEqual(by_target["Generic_ESPNOW_companion_radio_unified"]["architecture"], "esp32")
            self.assertEqual(by_target["Custom_Future_companion_radio_unified"]["architecture"], "esp32")
            self.assertEqual(by_target["RAK_4631_companion_radio_unified"]["transports"], ["usb", "ble"])
            self.assertIn("Heltec_E290_companion_radio_unified", by_target)

            text = output.read_text(encoding="utf-8")
            self.assertIn("examples/unified_radio/partitions_4mb.csv", text)
            self.assertIn("board_upload.maximum_size = 3145728", text)
            self.assertIn("board_build.partitions = default_16MB.csv", text)
            self.assertIn("board_upload.maximum_size = 6553600", text)
            self.assertIn("-D OFFLINE_QUEUE_SIZE=96", text)
            self.assertNotIn("-D OFFLINE_QUEUE_SIZE=128", text)
            self.assertIn("[env:LilyGo_TDeck_8MB_companion_radio_unified]", text)
            self.assertIn("-UBOARD_HAS_PSRAM", text)
            self.assertNotIn("-U BOARD_HAS_PSRAM", text)
            self.assertNotIn("WIFI_SSID", text)
            self.assertNotIn("WIFI_PWD", text)
            self.assertIn("-D KEEP_ME=1", text)
            self.assertIn("-<../examples/companion_radio/main.cpp>", text)
            custom_start = text.index("[env:Custom_Future_companion_radio_unified]")
            generic_start = text.index("[env:Generic_ESPNOW_companion_radio_unified]")
            self.assertIn(
                "examples/unified_radio/partitions_4mb.csv",
                text[custom_start:generic_start],
            )
            # No-BLE targets must exclude wildcard helpers inherited from some
            # upstream ESP32 companion environments.
            e290_start = text.index("[env:Heltec_E290_companion_radio_unified]")
            self.assertIn(
                "-<helpers/esp32/SerialBLEInterface.cpp>",
                text[generic_start:e290_start],
            )


if __name__ == "__main__":
    unittest.main()
