import importlib.util
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location("ssf_native_compare", Path(__file__).with_name("ssf_native_compare.py"))
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        lines = ["ssf-native-v1 parsed segments 1"]
        for index in range(6):
            count = int(index >= 2)
            lines.append(f"dump {index} units {count}")
            (self.root / f"dump-{index}.units").write_bytes(bytes(4 * count))
            active = int(index not in (0, 4))
            lines.append(f"frame {index} found {active} subtitles {active}")
            lines.append(f"pixels {index} changed {0} bounds 0 0 20 20")
            changed = 0
            (self.root / f"frame-{index}.bgra").write_bytes(b"\x01\x00\x00\xff" * changed + b"\x00\x00\x00\xff" * (320 * 180 - changed))
        lines.extend(["text units 0041", "rendered 1 glyphs", "ssf-native-v1 complete"])
        (self.root / "stdout.txt").write_text("\n".join(lines) + "\n", newline="\n")
        (self.root / "stderr.txt").write_bytes(b"")

    def test_requires_four_complete_equal_captures(self):
        complete = MODULE.inspect(self.root, 0)
        self.assertEqual(complete["status"], "complete")
        self.assertTrue(MODULE.equivalent([complete] * 4))
        self.assertFalse(MODULE.equivalent([complete] * 2))
        changed = dict(complete, stdout_sha256="different")
        self.assertFalse(MODULE.equivalent([complete] * 3 + [changed]))
        changed = dict(complete, files=dict(complete["files"], **{"frame-2.bgra": "different"}))
        self.assertFalse(MODULE.equivalent([complete] * 3 + [changed]))

    def test_exit_failure_or_timeout_never_passes_even_with_complete_files(self):
        self.assertEqual(MODULE.inspect(self.root, 3)["status"], "incomplete")
        self.assertEqual(MODULE.inspect(self.root, 0, True)["status"], "timeout")

    def test_missing_or_partial_frame_never_passes(self):
        frame = self.root / "frame-2.bgra"
        frame.write_bytes(b"partial")
        self.assertEqual(MODULE.inspect(self.root, 0)["status"], "incomplete")
        frame.unlink()
        self.assertEqual(MODULE.inspect(self.root, 0)["status"], "incomplete")

    def test_blank_characterization_must_match_actual_pixels(self):
        (self.root / "frame-2.bgra").write_bytes(b"\x01\x00\x00\xff" * (320 * 180))
        self.assertEqual(MODULE.inspect(self.root, 0)["status"], "control_failed")

    def test_timing_control_rejects_a_subtitle_after_its_stop(self):
        path = self.root / "stdout.txt"
        path.write_text(path.read_text().replace("frame 4 found 0 subtitles 0", "frame 4 found 1 subtitles 1"), newline="\n")
        self.assertEqual(MODULE.inspect(self.root, 0)["status"], "control_failed")

    def test_missing_completion_and_empty_text_fail(self):
        path = self.root / "stdout.txt"
        original = path.read_text()
        path.write_text(original.replace("ssf-native-v1 complete\n", ""), newline="\n")
        self.assertEqual(MODULE.inspect(self.root, 0)["status"], "incomplete")
        path.write_text(original.replace("text units 0041", "text units"), newline="\n")
        self.assertEqual(MODULE.inspect(self.root, 0)["status"], "control_failed")

    def test_expected_error_requires_exact_declared_message_and_completed_handler(self):
        for path in list(self.root.glob("dump-*.units")) + list(self.root.glob("frame-*.bgra")):
            path.unlink()
        expected = "unterminated quoted string"
        message = "Error (Ln 1 Col 23): " + expected
        trace = "ssf exception utf16" + "".join(f" {ord(c):04x}" for c in message) + "\nssf-native-v1 exception complete\n"
        path = self.root / "stdout.txt"
        path.write_text(trace, newline="\n")
        result = MODULE.inspect(self.root, 2, expected_error=expected)
        self.assertEqual(result["status"], "complete")
        self.assertEqual(result["error_message"], message)
        self.assertEqual(result["blank_frame_observations"], 0)
        self.assertEqual(result["files"], {})
        self.assertEqual(MODULE.inspect(self.root, 2)["status"], "incomplete")
        for code in (0, 3, -1073741819):
            self.assertEqual(MODULE.inspect(self.root, code, expected_error=expected)["status"], "incomplete")
        self.assertEqual(MODULE.inspect(self.root, 2, True, expected)["status"], "timeout")
        self.assertEqual(MODULE.inspect(self.root, 2, expected_error="unterminated block")["status"], "control_failed")
        path.write_text(trace.replace("ssf-native-v1 exception complete\n", ""), newline="\n")
        self.assertEqual(MODULE.inspect(self.root, 2, expected_error=expected)["status"], "incomplete")
        path.write_text(trace, newline="\n")
        (self.root / "dump-0.units").write_bytes(b"")
        self.assertEqual(MODULE.inspect(self.root, 2, expected_error=expected)["status"], "incomplete")


if __name__ == "__main__":
    unittest.main()
