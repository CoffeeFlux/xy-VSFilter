import json
from pathlib import Path
import struct
import sys
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

import document_prepare_check as native
import document_prepare_rust_check as check
from test_document_prepare_check import trace


class RustPreparationComparisonTests(unittest.TestCase):
    def test_failure_invalidates_previous_comparison_success(self):
        with TemporaryDirectory() as directory:
            path = Path(directory)
            (path/'report.json').write_text('{"complete":true,"equal":true}')
            with patch.object(sys,'argv',['check','--native-capture','missing','--output',directory]), \
                    patch.object(check,'native_capture',side_effect=ValueError('incomplete capture')):
                self.assertEqual(check.main(),1)
            report = json.loads((path/'report.json').read_text())
            self.assertFalse(report['complete'])
            self.assertFalse(report['equal'])
            self.assertEqual(report['unresolved'],'incomplete capture')

    def test_exact_raw_output_and_repeat_required(self):
        reference = trace()
        values = b'\n'.join(line for line in reference.splitlines() if line.startswith(b'value '))+b'\n'
        self.assertEqual(check.compare_values(reference,[values,values],64),600)
        changed = values.replace(b'value add 1 0 -',b'value add 1 1 0000',1)
        with self.assertRaisesRegex(ValueError,'differs'):
            check.compare_values(reference,[changed,changed],64)
        with self.assertRaisesRegex(ValueError,'repeatable'):
            check.compare_values(reference,[values,changed],64)
        with self.assertRaises(ValueError):
            check.compare_values(reference,[values.split(b'\n',1)[1]]*2,64)
        with self.assertRaises(ValueError):
            check.compare_values(reference,[values+b'extra\n']*2,64)

    def test_transport_line_endings_do_not_change_codeunits(self):
        reference = trace().replace(b'\n',b'\r\n')
        values = b'\n'.join(line for line in reference.splitlines() if line.startswith(b'value '))+b'\n'
        self.assertEqual(check.compare_values(reference,[values,values],64),600)

    def test_incomplete_native_report_is_not_evidence(self):
        with TemporaryDirectory() as directory:
            path = Path(directory)
            (path/'report.json').write_text(json.dumps({'schema':1,'complete':False}))
            with self.assertRaisesRegex(ValueError,'incomplete'):
                check.native_capture(path)

    def test_retained_native_binary_hash_and_architecture_are_checked(self):
        with TemporaryDirectory() as directory:
            path = Path(directory)
            binary = bytearray(128)
            binary[:2] = b'MZ'; struct.pack_into('<I',binary,60,64)
            binary[64:68] = b'PE\0\0'; struct.pack_into('<H',binary,68,0x14c)
            files = {'document_prepare_probe.cpp':b'probe', 'document_prepare_probe.exe':bytes(binary),
                     'build.log':b'build', 'trace-0.txt':trace(), 'trace-1.txt':trace(),
                     'stderr-0.txt':b'', 'stderr-1.txt':b''}
            for name,data in files.items(): (path/name).write_bytes(data)
            report = dict(schema=1, complete=True, candidate_tracked_clean=True, candidate_source_stable=True,
                          repeat_equal=True, stderr_empty=True, reference_commit=native.PIN,
                          pointer_bits=64, requested_platform='x64', generated_sha256=native.sha(b'probe'),
                          exe_sha256=native.sha(binary), build_log_sha256=native.sha(b'build'),
                          trace_sha256=[native.sha(trace())]*2)
            (path/'report.json').write_text(json.dumps(report))
            with self.assertRaisesRegex(ValueError,'architecture mismatch'):
                check.native_capture(path)
            (path/'document_prepare_probe.exe').write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError,'artifact hash mismatch'):
                check.native_capture(path)


if __name__ == '__main__':
    unittest.main()
