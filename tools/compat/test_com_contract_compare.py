# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import sys
import tempfile
import unittest

import com_contract_compare as harness


class ComContractHarnessTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def observations(self, variants=None):
        traces = {
            name: b"control.created value 1\nfilter.state value 0\nCOM_TRACE_COMPLETE\n"
            for name in ("reference-a", "reference-b", "candidate-a", "candidate-b")
        }
        traces.update(variants or {})
        observations, directories = {}, {}
        for name, trace in traces.items():
            directory = self.root / name
            directory.mkdir()
            (directory / "stdout.txt").write_bytes(trace)
            observations[name] = {"status": "complete"}
            directories[name] = directory
        return observations, directories

    def test_four_stable_complete_traces_match(self):
        self.assertEqual(harness.compare(*self.observations()), {"kind": "equal"})

    def test_reference_instability_is_not_candidate_parity(self):
        observations, directories = self.observations({"reference-b": b"different native state\n"})
        self.assertEqual(harness.compare(observations, directories), {"kind": "reference_unstable"})

    def test_candidate_instability_is_not_a_pass(self):
        self.assertEqual(harness.compare(*self.observations({"candidate-b": b"different\n"})), {"kind": "candidate_unstable"})

    def test_matching_process_failures_do_not_pass(self):
        observations, directories = self.observations()
        for result in observations.values():
            result["status"] = "process_failure"
        self.assertEqual(harness.compare(observations, directories)["kind"], "process_failure")

    def test_complete_trace_mismatch_identifies_first_line(self):
        trace = b"control.created value 1\nfilter.state value 1\nCOM_TRACE_COMPLETE\n"
        result = harness.compare(*self.observations({"candidate-a": trace, "candidate-b": trace}))
        self.assertEqual(result["kind"], "trace_mismatch")
        self.assertEqual(result["line"], 2)

    def test_capture_requires_successful_exit_and_creation_and_final_marker(self):
        scripts = [
            ("print('control.created value 1'); print('COM_TRACE_COMPLETE')", "complete"),
            ("print('COM_TRACE_COMPLETE')", "incomplete_trace"),
            ("print('control.created value 1')", "incomplete_trace"),
            ("print('control.created value 1'); print('COM_TRACE_COMPLETE'); raise SystemExit(3)", "process_failure"),
        ]
        for index, (script, expected) in enumerate(scripts):
            result = harness.capture([sys.executable, "-c", script], self.root / str(index), 5)
            self.assertEqual(result["status"], expected)

    def test_timeout_keeps_partial_trace_and_never_passes(self):
        script = "import time; print('control.created value 1', flush=True); time.sleep(10)"
        result = harness.capture([sys.executable, "-c", script], self.root / "timeout", 0.5)
        self.assertEqual(result["status"], "timeout")
        self.assertIn(b"control.created", (self.root / "timeout/stdout.txt").read_bytes())


if __name__ == "__main__":
    unittest.main()
