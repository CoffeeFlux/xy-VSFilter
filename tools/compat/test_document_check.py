#!/usr/bin/env python3
"""Focused harness failure checks; no renderer or platform parity claims."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import document_check

class HarnessFailures(unittest.TestCase):
    def test_changed_contents_and_truncated_trace_are_rejected(self):
        reference='0 0\n1 2 3\n'
        document_check.compare(reference,reference)
        for changed in ['0 0\n1 2 4\n','0 0\n']:
            with self.subTest(changed=changed), self.assertRaises(RuntimeError):
                document_check.compare(reference,changed)

    def test_failed_run_replaces_stale_success_with_incomplete_report(self):
        with tempfile.TemporaryDirectory(prefix='xy-document-failure-') as directory:
            output=Path(directory)
            report=output/'report.json'
            report.write_text(json.dumps({'complete':True}))
            environment=os.environ.copy()
            environment['PATH']=directory  # No git/compiler/toolchain is executable.
            result=subprocess.run([sys.executable,str(Path(document_check.__file__)),
                                   '--output',directory],env=environment,
                                  capture_output=True,text=True,timeout=10)
            self.assertNotEqual(result.returncode,0)
            self.assertIs(json.loads(report.read_text())['complete'],False)

    def test_process_timeout_is_a_failure(self):
        with self.assertRaises(subprocess.TimeoutExpired):
            document_check.run([sys.executable,'-c','import time; time.sleep(10)'],timeout=0.05)

class NativeControlReplayChecks(unittest.TestCase):
    def test_control_stream_accepts_only_permutations_times_and_native_noops(self):
        original='S 0\nV 1 25\nV 1 25\nE 0 31 -1\n'
        replay='O 3 2 0 1\nT 3 -1 0 0 1 1 2\nP\nE 0 31 -1\n'
        document_check.validate_replay(original,replay)
        changed=[
            replay.replace('O 3 2 0 1','O 3 2 0 0'),
            replay.replace('O 3 2 0 1','O 2 2 0 1'),
            replay.replace('O 3 2 0 1','T 0'),
            replay.replace('T 3 -1 0 0 1 1 2','T 2 -1 0'),
            replay.replace('T 3 -1 0 0 1 1 2','T 1 0 9223372036854775808'),
            replay.replace('T 3 -1 0 0 1 1 2','T 4097'),
            replay.replace('E 0 31 -1','E 0 32 -1'),
            replay+'P\n',
        ]
        for mutated in changed:
            with self.subTest(mutated=mutated),self.assertRaises(RuntimeError):
                document_check.validate_replay(original,mutated)

if __name__=='__main__':unittest.main()
