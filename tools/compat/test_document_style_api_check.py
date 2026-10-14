import tempfile
import unittest
from unittest.mock import patch
from pathlib import Path
import document_style_api_check as check


class StyleRunnerTests(unittest.TestCase):
    def test_commit_id_decodes_git_bytes(self):
        with patch.object(check.com, "git", return_value=(check.PIN + "\r\n").encode("ascii")):
            value = check.commit_id(Path("/reference"))
        self.assertEqual(value, check.PIN)
        self.assertIsInstance(value, str)

    def trace(self):
        return b'provenance pointer_bits=64\nfactory hr=0\ncreate hr=0\nload hr=0\ncount hr=0\nstyles=42\ngetter.long_name hr=6f\npartial.rows=3\n' + b''.join(str(i).encode() + b' 64:sentinel\n' for i in range(42)) + b'document-style-api-v1 complete\n'

    def test_complete_long_trace(self):
        check.validate_trace(self.trace(), b'', 0, 'long-name', 64)

    def test_normal_complete_rows_and_late_status_required(self):
        lines = ['provenance pointer_bits=64', 'factory hr=0', 'create hr=0',
                 'load hr=0', 'count hr=0', 'styles=3', 'baseline hr=0',
                 'getter.wrong_count hr=80070057', 'setter.wrong_count hr=80070057',
                 'after.wrong_count hr=0', 'getter.alias hr=0', 'alias.final value',
                 'getter.null_style hr=0', 'setter.missing_late hr=80004005',
                 'after.missing_late hr=0', 'setter.duplicate hr=0',
                 'after.duplicate hr=0', 'setter.all_null hr=0', 'after.all_null hr=0']
        lines += [f'{i} 64:units style' for _ in range(6) for i in range(3)]
        lines += ['document-style-api-v1 complete']
        data = ('\n'.join(lines) + '\n').encode()
        check.validate_trace(data, b'', 0, 'normal', 64)
        for broken in (data.replace(b'after.all_null hr=0', b''),
                       data.replace(b'2 64:units style\n', b'', 1)):
            with self.assertRaises(ValueError):
                check.validate_trace(broken, b'', 0, 'normal', 64)

    def test_negative_controls(self):
        trace = self.trace()
        for data, stderr, code in [
            (trace.replace(b'document-style-api-v1 complete', b''), b'', 0),
            (trace, b'warning', 0), (trace, b'', 1),
            (trace.replace(b'partial.rows=3', b'partial.rows=0'), b'', 0),
            (trace.replace(b'getter.long_name hr=6f', b'getter.long_name hr=0'), b'', 0),
            (trace.replace(b'pointer_bits=64', b'pointer_bits=32'), b'', 0),
            (trace.replace(b'0 64:sentinel', b''), b'', 0),
            (trace + b'document-style-api-v1 complete\n', b'', 0),
        ]:
            with self.subTest(data=data, stderr=stderr, code=code):
                with self.assertRaises(ValueError):
                    check.validate_trace(data, stderr, code, 'long-name', 64)

    def test_wrapper_has_isolated_helper_and_paths(self):
        import xml.etree.ElementTree as ET
        text = check.wrapper_xml(Path('/repo/src/filters/VSFilter.vcxproj'), Path('/helper/a&b.cpp'), Path('/repo'), Path('/out'), Path('/int'), Path('/libs'))
        root = ET.fromstring(text)
        ns = {'m': 'http://schemas.microsoft.com/developer/msbuild/2003'}
        self.assertEqual(root.find('m:Import', ns).attrib['Project'], 'VSFilter.vcxproj')
        helper = root.find('m:ItemGroup/m:ClCompile', ns)
        self.assertEqual(helper.attrib['Include'], '/helper/a&b.cpp')
        self.assertEqual(helper.find('m:PrecompiledHeader', ns).text, 'NotUsing')
        self.assertIn('/Y-', helper.find('m:AdditionalOptions', ns).text)
        self.assertIn('XY_DOCUMENT_STYLE_TEST_HELPER', helper.find('m:PreprocessorDefinitions', ns).text)
        self.assertEqual(helper.find('m:AdditionalIncludeDirectories', ns).text,
                         '/repo/src;/repo/src/filters;%(AdditionalIncludeDirectories)')
        self.assertIn('/libs;', text)
        self.assertNotIn('RustTextDecoder', text)
        self.assertNotIn('ModuleDefinitionFile', text)

    def test_fixtures_bounded_and_long_name(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'fixture.ass'
            check.fixture(path, 'long-name')
            names = [line.split(',')[0][7:] for line in path.read_text(encoding='utf-8-sig').splitlines() if line.startswith('Style: ')]
            self.assertTrue(3 <= len(names) <= 64)
            self.assertEqual(sum(len(name) >= 64 for name in names), 1)
            self.assertGreater(sum(len(name) < 64 for name in names), 30)


if __name__ == '__main__':
    unittest.main()
