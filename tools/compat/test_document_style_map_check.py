import json
from pathlib import Path
import tempfile
import unittest
import document_style_map_check as check
import test_document_atl_contract as atl_tests


class StyleMapCheckTests(unittest.TestCase):
    def native_rows(self): return atl_tests.AtlContractTests().trace()
    def rust(self, rows): return ('\n'.join(json.dumps(row) for row in rows[1:])+'\n').encode()

    def test_complete_semantic_compare(self):
        rows=self.native_rows();data=self.rust(rows)
        self.assertEqual(check.compare_traces(rows,[data,data]),rows[1:])

    def test_negative_comparison_controls(self):
        rows=self.native_rows();data=self.rust(rows)
        cases=[data.replace(b'"bins": 17',b'"bins": 23',1), data.replace(b'"seq": 1,',b'"seq": 2,',1),
               data.replace(b'document-atl-contract-v1 complete',b'incomplete')]
        for broken in cases:
            with self.assertRaises(ValueError):check.compare_traces(rows,[broken,broken])
        with self.assertRaises(ValueError):check.compare_traces(rows,[data,data+b'\n'])
        with self.assertRaises(ValueError):check.compare_traces(rows,[data])

    def test_extra_rust_provenance_rejected(self):
        rows=self.native_rows();data=('\n'.join(json.dumps(r) for r in rows)+'\n').encode()
        with self.assertRaises(ValueError):check.compare_traces(rows,[data,data])

    def test_native_incomplete_report_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            Path(directory,'report.json').write_text(json.dumps({'schema':'document-atl-contract-v1','complete':False}))
            with self.assertRaises(ValueError):check.native_capture(directory)

    def test_retained_native_hash_mismatch_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            report={'schema':'document-atl-contract-v1','complete':True,'repeat_equal':True,'stderr_empty':True,
                    'behavior_capture_complete':True,'platform':'x64','artifact_sha256':{'C:\\out\\probe.cpp':check.sha(b'original')}}
            Path(directory,'report.json').write_text(json.dumps(report));Path(directory,'probe.cpp').write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError,'artifact hash mismatch'):check.native_capture(directory)


if __name__=='__main__':unittest.main()
