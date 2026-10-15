import unittest
from unittest.mock import patch
from pathlib import Path
from tempfile import TemporaryDirectory
import json
import sys

import document_prepare_check as check


def trace():
    lines = ['provenance pointer_bits=64', 'cstring_type class ATL::CStringT<wchar_t,class StrTraitMFC<wchar_t,class ATL::ChTraitsCRT<wchar_t> > >']
    for i, (mode, text, style) in enumerate(check.fixtures()):
        payload = f'{mode} {i} {len(text)} {check.encoded(text)} {len(style)} {check.encoded(style)}'
        lines += ['input ' + payload, 'value ' + payload]
    return ('\n'.join(lines + [check.MARKER]) + '\n').encode()


class PreparationTests(unittest.TestCase):
    def test_pinned_extraction_excludes_preceding_and_following_operations(self):
        raw = check.subprocess.check_output(['git','show',f'{check.PIN}:src/subtitles/STS.cpp'],cwd=check.ROOT)
        extracted = check.extract(raw.decode('utf-8-sig').replace('\r\n','\n'))
        self.assertEqual(set(extracted), {'add','entry'})
        for item in extracted.values():
            self.assertNotIn('FastTrim', item['text'])
            self.assertNotIn('STSEntry', item['text'])
            self.assertNotIn('MBCS', item['text'])
            self.assertNotIn('WebVTT', item['text'])
        self.assertIn('else if(style!=g_default_style)', extracted['add']['text'])
        self.assertNotIn('else if', extracted['entry']['text'])
        generated = check.generate(extracted)
        self.assertLess(generated.index('#include <afxwin.h>'),generated.index('#include <atlstr.h>'))
        self.assertNotIn('using ATL::CStringW',generated)
        self.assertIn('AfxWinInit',generated)

    def test_comparison_rejects_content_lengths_and_missing_observations(self):
        valid = trace()
        self.assertEqual(check.compare_traces(valid,valid,64),len(check.fixtures()))
        with self.assertRaisesRegex(ValueError,'MFC'):
            check.validate_trace(valid.replace(b'StrTraitMFC',b'StrTraitATL'),64)
        changed = valid.replace(b'value add 1 0 -',b'value add 1 1 0041',1)
        with self.assertRaisesRegex(ValueError,'not repeatable'):
            check.compare_traces(valid,changed,64)
        with self.assertRaises(ValueError):
            check.validate_trace(valid.replace(b'value add 1 0 -',b'value add 1 1 -',1),64)
        with self.assertRaises(ValueError):
            check.validate_trace(b'\n'.join(valid.splitlines()[1:]),64)
        with self.assertRaises(ValueError):
            check.validate_trace(valid.replace(check.MARKER.encode(),b''),64)

    def test_failure_invalidates_previous_success(self):
        with TemporaryDirectory() as temp:
            report = Path(temp)/'report.json'
            report.write_text('{"complete":true}')
            with patch.object(sys,'argv',['check','--output',temp,'--generate-only']), patch.object(check.subprocess,'check_output',side_effect=check.subprocess.TimeoutExpired('git',30)):
                self.assertEqual(check.main(),1)
            data = json.loads(report.read_text())
            self.assertFalse(data['complete'])
            self.assertIn('error',data)


if __name__ == '__main__':
    unittest.main()
