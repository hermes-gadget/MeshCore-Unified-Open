import importlib.util
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "validate_unified_builds", ROOT / "tools/validate_unified_builds.py"
)
validator = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(validator)


class MatrixValidatorTest(unittest.TestCase):
    def test_classifies_pass_upstream_and_unified_failures(self):
        item = {
            "target": "Board_companion_radio_unified",
            "source_environment": "Board_companion_radio_ble",
        }
        with tempfile.TemporaryDirectory() as tmp_name:
            project = Path(tmp_name)
            config = project / "generated.ini"
            logs = project / "logs"

            passed_runner = Mock(return_value=0)
            passed = validator.validate_target(
                project, item, config, logs, False, passed_runner
            )
            self.assertEqual(passed["status"], validator.RESULT_PASS)
            self.assertEqual(passed_runner.call_count, 1)

            upstream_runner = Mock(side_effect=[1, 1])
            upstream = validator.validate_target(
                project, item, config, logs, False, upstream_runner
            )
            self.assertEqual(
                upstream["status"], validator.RESULT_UPSTREAM_FAILURE
            )
            self.assertEqual(upstream_runner.call_count, 2)

            unified_runner = Mock(side_effect=[1, 0])
            unified = validator.validate_target(
                project, item, config, logs, False, unified_runner
            )
            self.assertEqual(
                unified["status"], validator.RESULT_UNIFIED_FAILURE
            )
            self.assertEqual(unified_runner.call_count, 2)

    def test_select_targets_preserves_request_order(self):
        manifest = [
            {"target": "a", "source_environment": "a_usb"},
            {"target": "b", "source_environment": "b_ble"},
        ]
        selected = validator.select_targets(manifest, ["b", "a"])
        self.assertEqual([item["target"] for item in selected], ["b", "a"])


if __name__ == "__main__":
    unittest.main()
