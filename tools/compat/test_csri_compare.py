# SPDX-License-Identifier: GPL-2.0-or-later
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import csri_compare as harness


class NativeHarnessTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.root = Path(cls.temporary.name)
        cls.libraries = {}
        for variant in ("normal", "DIFFERENT", "CRASH", "HANG", "REJECT", "CORRUPT_GUARD"):
            suffix = ".dll" if os.name == "nt" else ".dylib" if sys.platform == "darwin" else ".so"
            path = cls.root / (variant + suffix)
            source = harness.ROOT / "tools/compat/fake_csri.c"
            includes = harness.ROOT / "src/filters/transform/vsfilter"
            if os.name == "nt":
                command = ["cl", "/nologo", "/LD", "/O2", "/MD", f"/I{includes}",
                           f"/D{variant}", str(source), f"/Fe:{path}", f"/Fo:{cls.root / (variant + '.obj')}"]
            else:
                command = ["cc", "-O2", "-dynamiclib" if sys.platform == "darwin" else "-shared",
                           "-fPIC", "-I", str(includes), f"-D{variant}", str(source), "-o", str(path)]
            subprocess.run(command, check=True, capture_output=True)
            cls.libraries[variant] = path

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def setUp(self):
        self.case_dir = Path(tempfile.mkdtemp(dir=self.root))
        script = self.case_dir / "raw.ass"
        script.write_bytes(b"\xff\x00malformed bytes\r\n")
        self.case = {"id": "fake-case", "script": str(script), "sha256": harness.digest(script.read_bytes()),
                     "width": 3, "height": 2, "stride_padding": 4, "open": "memory",
                     "times": ["1", "3", "1", "1"], "fill_seed": 43}
        self.case_path = self.case_dir / "case.json"
        harness.write_json(self.case_path, self.case)

    def run_variant(self, variant, directory, timeout=10):
        destination = self.case_dir / directory
        return harness.run_worker(self.libraries[variant], self.case_path, destination, timeout), destination

    def test_same_library_preserves_state_and_exact_bytes(self):
        left, ld = self.run_variant("normal", "left")
        right, rd = self.run_variant("normal", "right")
        self.assertEqual(harness.compare_captures(left, right, ld, rd), {"kind": "equal"})
        self.assertEqual([frame["changed"] for frame in left["frames"]], [True] * 4)
        self.assertEqual([(ld / frame["file"]).read_bytes()[0] for frame in left["frames"]], [11, 32, 13, 14])
        self.assertTrue(all(frame["guards_intact"] for frame in left["frames"]))

    def test_byte_mismatch_has_location(self):
        left, ld = self.run_variant("normal", "left")
        right, rd = self.run_variant("DIFFERENT", "right")
        diff = harness.compare_captures(left, right, ld, rd)
        self.assertEqual(diff["kind"], "mismatch")
        self.assertEqual(diff["byte_differences"][0]["first_byte"], 1)

    def test_equal_crashes_are_unresolved(self):
        left, ld = self.run_variant("CRASH", "left")
        right, rd = self.run_variant("CRASH", "right")
        self.assertEqual(left["status"], "crash")
        self.assertEqual(harness.compare_captures(left, right, ld, rd)["kind"], "unresolved")

    def test_timeout_is_unresolved(self):
        result, _ = self.run_variant("HANG", "hung", timeout=0.5)
        self.assertEqual(result["status"], "timeout")

    def test_rejection_is_recorded_as_rejection(self):
        left, ld = self.run_variant("REJECT", "left")
        right, rd = self.run_variant("REJECT", "right")
        self.assertEqual(left["status"], "open_rejected")
        self.assertEqual(left["frames"], [])
        self.assertEqual(harness.compare_captures(left, right, ld, rd)["kind"], "equal")

    def test_guard_corruption_cannot_pass(self):
        result, directory = self.run_variant("CORRUPT_GUARD", "corrupt")
        self.assertEqual(result["status"], "guard_corruption")
        self.assertEqual(harness.compare_captures(result, result, directory, directory)["kind"], "unresolved")

    def test_negative_stride_points_to_logical_top_row(self):
        self.case["bottom_up"] = True
        self.case["open"] = "file"
        harness.write_json(self.case_path, self.case)
        result, directory = self.run_variant("normal", "bottom-up")
        self.assertEqual(result["status"], "ok")
        data = (directory / result["frames"][0]["file"]).read_bytes()
        self.assertEqual(data[16], 11)
        self.assertEqual(data[0], self.case["fill_seed"])

    def test_fixture_changes_fail_before_loading(self):
        self.case["sha256"] = "wrong"
        harness.write_json(self.case_path, self.case)
        result, _ = self.run_variant("normal", "bad-fixture")
        self.assertEqual(result["status"], "harness_error")

    def test_driver_exit_code_and_provenance(self):
        manifest = self.case_dir / "cases.json"
        harness.write_json(manifest, {"schema": 1, "cases": [self.case]})
        for variant, code in (("normal", 0), ("DIFFERENT", 1), ("CRASH", 1)):
            output = self.case_dir / ("driver-" + variant)
            command = [sys.executable, str(harness.ROOT / "tools/compat/csri_compare.py"),
                "--reference", str(self.libraries["normal"]), "--candidate", str(self.libraries[variant]),
                "--cases", str(manifest), "--output", str(output), "--environment-label", "FAKE TEST ONLY"]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, code, result.stderr)
            report = harness.read_json(output / "report.json")
            self.assertTrue(report["complete"])
            self.assertEqual(report["passed"], code == 0)
            self.assertEqual(report["reference_dll"]["sha256"], harness.digest(self.libraries["normal"].read_bytes()))
            if code == 0:
                self.assertEqual(list(output.glob("case-*/*/frame-*.bin")), [])
            else:
                self.assertTrue(list(output.glob("case-*/*/frame-*.bin")))

    def test_reducer_keeps_a_real_mismatch_and_preserves_the_original(self):
        original = Path(self.case["script"]).read_bytes()
        for variant, code in (("DIFFERENT", 0), ("normal", 2), ("CRASH", 2)):
            output = self.case_dir / ("reduced-" + variant)
            command = [sys.executable, str(harness.ROOT / "tools/compat/reduce_case.py"),
                "--reference", str(self.libraries["normal"]), "--candidate", str(self.libraries[variant]),
                "--case", str(self.case_path), "--output", str(output), "--max-attempts", "4"]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, code, result.stderr)
            report = harness.read_json(output / "report.json")
            self.assertEqual(Path(self.case["script"]).read_bytes(), original)
            if variant == "DIFFERENT":
                self.assertTrue(report["complete"])
                self.assertLess(report["best_bytes"], len(original))
                self.assertEqual(report["best_sha256"], harness.digest((output / "minimized.ass").read_bytes()))
                self.assertTrue(report["environment_unchanged"])
                self.assertTrue(list((output / report["best_trial"]).glob("*/frame-*.bin")))
            else:
                self.assertFalse(report["complete"])


if __name__ == "__main__":
    unittest.main()
