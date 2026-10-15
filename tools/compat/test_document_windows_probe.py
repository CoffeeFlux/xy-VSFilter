import json
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

import document_windows_probe as probe


class OwnerDependencies(unittest.TestCase):
    def test_generation_uses_real_style_and_mfc_dependencies(self):
        with TemporaryDirectory() as directory:
            out = Path(directory)
            probe.run_owner(out, True)
            report = json.loads((out / 'report.json').read_text())
            header = (out / 'STS.h').read_text()
            current = (probe.ROOT / 'src/subtitles/STS.h').read_text()
            pinned = probe.source('src/subtitles/STS.h').decode('utf-8-sig').replace('\r\n', '\n')
            self.assertIn(probe.owner_style_declarations(pinned, current), header)
            self.assertIn('#include "flyweight_base_types.h"', header)
            self.assertNotIn('struct STSStyle;', header)
            source = (out / 'document_owner_probe.cpp').read_text()
            self.assertLess(source.index('#include <afx.h>'), source.index('#include <atlstr.h>'))
            self.assertNotIn('#include <windows.h>', source)
            for path in ('src/subtitles/flyweight_base_types.h', 'src/subtitles/mru_cache.h'):
                self.assertIn(path, report['candidate_source_sha256'])
                self.assertIn(path, report['reference_source_sha256'])
            self.assertFalse(report['complete'])
            self.assertTrue(report['generation_only'])

    def test_changed_real_style_declaration_refused(self):
        header = probe.source('src/subtitles/STS.h').decode('utf-8-sig').replace('\r\n', '\n')
        with self.assertRaisesRegex(RuntimeError, 'unchanged'):
            probe.owner_style_declarations(header, header.replace('double  fontSize;', 'float  fontSize;'))


if __name__ == '__main__':
    unittest.main()
