import importlib.util
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock, patch


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

    def test_baseline_uses_original_helper_and_restores_backport_on_error(self):
        with tempfile.TemporaryDirectory() as tmp_name:
            project = Path(tmp_name)
            helper = project / "src/helpers/TxtDataHelpers.cpp"
            helper.parent.mkdir(parents=True)
            originals = project / ".pio/unified-upstream-originals"
            originals.mkdir(parents=True)
            helper.write_text("portable helper")
            (originals / "TxtDataHelpers.cpp").write_text("upstream helper")
            (originals / "TxtDataHelpers.cpp.patched").write_text("portable helper")

            seen = []
            def runner(*args):
                seen.append(helper.read_text())
                return 1

            result = validator.validate_target(
                project, {"target": "board_unified", "source_environment": "board_usb"},
                project / "generated.ini", project / "logs", False, runner,
            )
            self.assertEqual(result["status"], validator.RESULT_UPSTREAM_FAILURE)
            self.assertEqual(seen, ["portable helper", "upstream helper"])
            self.assertEqual(helper.read_text(), "portable helper")

            with self.assertRaises(RuntimeError):
                with validator.upstream_baseline(project):
                    self.assertEqual(helper.read_text(), "upstream helper")
                    raise RuntimeError("build could not start")
            self.assertEqual(helper.read_text(), "portable helper")

            helper.write_text("newer upstream helper")
            with validator.upstream_baseline(project):
                self.assertEqual(helper.read_text(), "newer upstream helper")
            self.assertEqual(helper.read_text(), "newer upstream helper")

    def test_resume_fingerprint_tracks_staged_sources_without_git_changes(self):
        for git_state in (b"", subprocess.CalledProcessError(128, ["git"])):
            with self.subTest(git_available=isinstance(git_state, bytes)):
                with tempfile.TemporaryDirectory() as tmp_name:
                    project = Path(tmp_name)
                    manifest, config = project / "targets.json", project / "generated.ini"
                    manifest.write_text("[]")
                    config.write_text("[env:board]")
                    source = project / "src/main.cpp"
                    source.parent.mkdir()
                    source.write_text("int main() { return 0; }")
                    git = (Mock(return_value=git_state) if isinstance(git_state, bytes)
                           else Mock(side_effect=git_state))
                    with patch.object(validator.subprocess, "check_output", git):
                        before = validator.validation_fingerprint(project, manifest, config)
                        source.write_text("int main() { compile_error }")
                        self.assertNotEqual(before, validator.validation_fingerprint(project, manifest, config))
                        source.write_text("int main() { return 0; }")
                        self.assertEqual(before, validator.validation_fingerprint(project, manifest, config))
                        header = project / "src/new.h"
                        header.write_text("#define NEW_FEATURE 1")
                        self.assertNotEqual(before, validator.validation_fingerprint(project, manifest, config))
                        header.unlink()
                        self.assertEqual(before, validator.validation_fingerprint(project, manifest, config))

    def test_resume_fingerprint_ignores_outputs_but_tracks_baseline_snapshot(self):
        with tempfile.TemporaryDirectory() as tmp_name:
            project = Path(tmp_name)
            manifest, config = project / "targets.json", project / "generated.ini"
            manifest.write_text("[]")
            config.write_text("[env:board]")
            with patch.object(validator.subprocess, "check_output", return_value=b""):
                before = validator.validation_fingerprint(project, manifest, config)
                for name in (".pio/build/firmware.elf", "tools/__pycache__/validator.pyc",
                             "src/.pio/cache"):
                    artifact = project / name
                    artifact.parent.mkdir(parents=True, exist_ok=True)
                    artifact.write_text("build output")
                self.assertEqual(before, validator.validation_fingerprint(project, manifest, config))
                upstream_config = project / "platformio.ini"
                upstream_config.write_text("[env:baseline]\n")
                self.assertNotEqual(before, validator.validation_fingerprint(project, manifest, config))
                upstream_config.unlink()
                original = project / ".pio/unified-upstream-originals/TxtDataHelpers.cpp"
                original.parent.mkdir(parents=True)
                original.write_text("pristine baseline helper")
                self.assertNotEqual(before, validator.validation_fingerprint(project, manifest, config))


if __name__ == "__main__":
    unittest.main()
