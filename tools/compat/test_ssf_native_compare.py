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
            lines.append(f"frame {index} found 1 subtitles 1")
            lines.append(f"pixels {index} changed {0} bounds 0 0 20 20")
            changed = 0
            (self.root / f"frame-{index}.bgra").write_bytes(b"\x01\x00\x00\xff" * changed + b"\x00\x00\x00\xff" * (320 * 180 - changed))
        lines.extend(["text units 0041", "rendered 1 glyphs", "ssf-native-v1 complete"])
        (self.root / "stdout.txt").write_text("\n".join(lines) + "\n")
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

    def test_missing_completion_and_empty_text_fail(self):
        path = self.root / "stdout.txt"
        original = path.read_text()
        path.write_text(original.replace("ssf-native-v1 complete\n", ""))
        self.assertEqual(MODULE.inspect(self.root, 0)["status"], "incomplete")
        path.write_text(original.replace("text units 0041", "text units"))
        self.assertEqual(MODULE.inspect(self.root, 0)["status"], "control_failed")


if __name__ == "__main__":
    unittest.main()
