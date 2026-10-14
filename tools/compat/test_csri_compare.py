# SPDX-License-Identifier: GPL-2.0-or-later
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock

from dialog_capture import DialogCapture

import csri_compare as harness


class NativeHarnessTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.root = Path(cls.temporary.name)
        cls.libraries = {}
        variants = ["normal", "DIFFERENT", "CRASH", "HANG", "REJECT", "CORRUPT_GUARD", "INPUT_BYTE"]
        if os.name == "nt":
            variants += ["DIALOG", "DIALOG_DIFFERENT", "NATIVE_EXCEPTION"]
        for variant in variants:
            suffix = ".dll" if os.name == "nt" else ".dylib" if sys.platform == "darwin" else ".so"
            path = cls.root / (variant + suffix)
            source = harness.ROOT / "tools/compat/fake_csri.c"
            includes = harness.ROOT / "src/filters/transform/vsfilter"
            if os.name == "nt":
                command = ["cl", "/nologo", "/LD", "/O2", "/MD", f"/I{includes}",
                           f"/D{variant}", str(source), f"/Fe:{path}", f"/Fo:{cls.root / (variant + '.obj')}", "/link", "user32.lib"]
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

    @unittest.skipUnless(os.name == "nt", "native Windows modal dialogs")
    def test_dialog_text_and_response_are_compared(self):
        self.case["dialog_actions"] = [{"title": "Warning", "text_prefix": "Harness ", "button": "cancel"}]
        harness.write_json(self.case_path, self.case)
        left, ld = self.run_variant("DIALOG", "left")
        right, rd = self.run_variant("DIALOG", "right")
        self.assertEqual(left["status"], "ok", left)
        self.assertEqual(left["dialogs"][0]["response"], "cancel")
        self.assertEqual((ld / left["frames"][0]["file"]).read_bytes()[1], 2)  # IDCANCEL returned natively
        self.assertEqual(harness.compare_captures(left,right,ld,rd), {"kind": "equal"})
        other, od = self.run_variant("DIALOG_DIFFERENT", "other")
        self.assertEqual(other["status"], "ok", other)
        self.assertIn("dialogs", harness.compare_captures(left,other,ld,od)["changed_fields"])

    @unittest.skipUnless(os.name == "nt", "native Windows modal dialogs")
    def test_scripted_ok_returns_native_idok(self):
        self.case["dialog_actions"] = [{"title": "Warning", "text_prefix": "Harness ", "button": "ok"}]
        harness.write_json(self.case_path, self.case)
        result, directory = self.run_variant("DIALOG", "ok-response")
        self.assertEqual(result["status"], "ok", result)
        self.assertEqual(result["dialogs"][0]["response"], "ok")
        self.assertEqual((directory / result["frames"][0]["file"]).read_bytes()[1], 1)  # IDOK

    @unittest.skipUnless(os.name == "nt", "native Windows modal dialogs")
    def test_unscripted_dialog_times_out_and_missing_dialog_fails(self):
        result, _ = self.run_variant("DIALOG", "unscripted", timeout=2)
        self.assertEqual(result["status"], "timeout", result)
        self.assertIsNone(result["dialogs"][0]["response"])
        self.case["dialog_actions"] = [{"title": "Warning", "text_prefix": "Harness ", "button": "ok"}]
        harness.write_json(self.case_path, self.case)
        result, _ = self.run_variant("normal", "missing")
        self.assertEqual(result["status"], "harness_error", result)

    @unittest.skipUnless(os.name == "nt", "Windows structured exception capture")
    def test_native_failure_keeps_its_stage_before_close(self):
        result, directory = self.run_variant("NATIVE_EXCEPTION", "native-exception")
        self.assertEqual(result["status"], "harness_error", result)
        self.assertEqual(result["last_progress"], {"stage": "close"})
        self.assertEqual(result["failure_before_close"]["progress"], {"stage": "render", "frame": 0, "timestamp": "1"})
        self.assertIn("csri_render", result["failure_before_close"]["traceback"])
        self.assertEqual(harness.compare_captures(result,result,directory,directory)["kind"], "unresolved")

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

    def test_control_requires_the_fixture_to_affect_rendered_pixels(self):
        second = self.case_dir / "second.ass"
        second.write_bytes(b"other raw input")
        case = dict(self.case, id="control", script=str(second), sha256=harness.digest(second.read_bytes()))
        manifest = self.case_dir / "control-cases.json"
        harness.write_json(manifest, {"schema": 1, "cases": [self.case, case],
                                     "expect_distinct_frames": [[self.case["id"], case["id"]]]})
        for variant, code in (("normal", 1), ("INPUT_BYTE", 0)):
            output = self.case_dir / ("control-" + variant)
            command = [sys.executable, str(harness.ROOT / "tools/compat/csri_compare.py"),
                "--reference", str(self.libraries[variant]), "--candidate", str(self.libraries[variant]),
                "--cases", str(manifest), "--output", str(output), "--environment-label", "FAKE CONTROL TEST"]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, code, result.stderr)
            report = harness.read_json(output / "report.json")
            self.assertEqual(report["controls_passed"], code == 0)


class ScriptedDialogDeliveryTests(unittest.TestCase):
    def setUp(self):
        self.capture = DialogCapture([], Path("unused-dialog-capture"))
        self.capture.api = Mock()
        self.capture.api.GetDlgItem.return_value = 222
        self.capture.api.GetParent.return_value = 111
        self.capture.api.GetDlgCtrlID.return_value = 2
        self.capture.api.IsWindowVisible.return_value = True
        self.capture.api.IsWindowEnabled.return_value = True
        self.capture.api.PostMessageW.return_value = True
        self.capture.class_name = Mock(return_value="Button")
        self.action = {"button": "cancel"}

    def test_ready_button_dispatches_one_command_to_dialog_with_control_identity(self):
        button = self.capture.scripted_button(111, self.action)
        self.assertEqual(button, 222)
        self.capture.post_response(111, button, self.action)
        self.capture.api.PostMessageW.assert_called_once_with(111, 0x0111, 2, 222)

    def test_unready_or_wrong_control_never_receives_a_command(self):
        for name, value in [("GetDlgItem", None), ("GetParent", 999),
                            ("GetDlgCtrlID", 1), ("IsWindowVisible", False),
                            ("IsWindowEnabled", False)]:
            function = getattr(self.capture.api, name)
            previous = function.return_value
            function.return_value = value
            with self.subTest(name=name):
                self.assertIsNone(self.capture.scripted_button(111, self.action))
                self.capture.api.PostMessageW.assert_not_called()
            function.return_value = previous
        self.capture.class_name.return_value = "Static"
        self.assertIsNone(self.capture.scripted_button(111, self.action))
        self.capture.api.PostMessageW.assert_not_called()

    def test_failed_queue_operation_does_not_report_delivery(self):
        self.capture.api.PostMessageW.return_value = False
        with self.assertRaisesRegex(RuntimeError, "could not queue"):
            self.capture.post_response(111, 222, self.action)
        self.assertEqual(self.capture.consumed, 0)
        self.assertEqual(self.capture.events, [])

    def configure_watch(self, destination, polls=1):
        self.capture.destination = destination
        self.capture.actions = [{"title": "Warning", "text_prefix": "Harness ", "button": "cancel"}]
        self.capture.windows = lambda parent=None: [111] if parent is None else [222, 333]
        self.capture.class_name = lambda window: {111: "#32770", 222: "Button", 333: "Static"}[window]
        self.capture.text = lambda window: {111: "Warning", 222: "Cancel", 333: "Harness warning"}[window]
        self.capture.api.GetDlgCtrlID.side_effect = lambda window: {222: 2, 333: 65535}[window]
        self.capture.api.GetWindowLongW.return_value = 0
        self.capture.stop = Mock()
        self.capture.stop.is_set.side_effect = [False] * polls + [True]

    def test_readiness_poll_keeps_one_warning_and_dispatches_only_once(self):
        with tempfile.TemporaryDirectory() as directory:
            self.configure_watch(Path(directory), polls=3)
            self.capture.api.IsWindowEnabled.side_effect = [False, True]
            self.capture.watch()
            self.assertIsNone(self.capture.error)
            self.assertEqual(self.capture.consumed, 1)
            self.assertEqual(len(self.capture.events), 1)
            self.assertEqual(self.capture.events[0]["response"], "cancel")
            self.capture.api.PostMessageW.assert_called_once_with(111, 0x0111, 2, 222)

    def test_failed_dispatch_preserves_warning_without_claiming_response(self):
        with tempfile.TemporaryDirectory() as directory:
            self.configure_watch(Path(directory))
            self.capture.api.PostMessageW.return_value = False
            self.capture.watch()
            self.assertIn("could not queue", self.capture.error)
            self.assertEqual(self.capture.consumed, 0)
            events = json.loads((Path(directory) / "dialogs.json").read_text())
            self.assertEqual(len(events), 1)
            self.assertIsNone(events[0]["response"])

    def test_disabled_button_keeps_warning_and_action_unconsumed(self):
        with tempfile.TemporaryDirectory() as directory:
            self.configure_watch(Path(directory))
            self.capture.api.IsWindowEnabled.return_value = False
            self.capture.watch()
            self.assertEqual(self.capture.consumed, 0)
            self.capture.api.PostMessageW.assert_not_called()
            events = json.loads((Path(directory) / "dialogs.json").read_text())
            self.assertEqual(len(events), 1)
            self.assertIsNone(events[0]["response"])


if __name__ == "__main__":
    unittest.main()
