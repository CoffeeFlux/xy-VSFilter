import unittest
import tempfile
from pathlib import Path
from unittest.mock import patch

from summarize_evidence import normalize_windows_source, windows_source_bytes, production_dll, document_lifecycle_requirements


class WindowsHeaderProvenance(unittest.TestCase):
    def test_lifecycle_archive_requirements_follow_verified_invocations(self):
        self.assertEqual(document_lifecycle_requirements(b"old probe"),
                         {"require_named_stages": False, "require_identity_stages": False, "require_interpretation_stages": False})
        source=b"int main() {\n    named_style_lifecycle();\n    identity_lifecycle();\n}"
        self.assertEqual(document_lifecycle_requirements(source),
                         {"require_named_stages": True, "require_identity_stages": True, "require_interpretation_stages": False})
        self.assertTrue(document_lifecycle_requirements(b'    interpretation_lifecycle();')['require_interpretation_stages'])
        self.assertFalse(document_lifecycle_requirements(b'// identity_lifecycle();\n"identity_lifecycle();"')['require_identity_stages'])

    def test_production_dll_excludes_instrumented_copies(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            production = root / 'candidate/bin/lib_17.0/x64/Release/VSFilter.dll'
            helper = root / 'candidate/artifacts/native/x64/document-style-api/candidate/VSFilter/VSFilter.dll'
            for path in (production, helper):
                path.parent.mkdir(parents=True)
                path.write_bytes(b'fixture')
            self.assertEqual(production_dll(root, 'candidate', 'VSFilter.dll'), production)
            production.unlink()
            with self.assertRaises(AssertionError):
                production_dll(root, 'candidate', 'VSFilter.dll')

    def test_compiler_spelling_resolves_to_tracked_path(self):
        self.assertEqual(normalize_windows_source(r"src\subtitles\..\DSUtil\DSUtil.h"),
                         "src/dsutil/dsutil.h")
        with patch("summarize_evidence.source_paths", return_value={"src/dsutil/dsutil.h": "src/dsutil/DSUtil.h"}), \
             patch("summarize_evidence.subprocess.run") as run:
            run.return_value.stdout = b"source"
            self.assertEqual(windows_source_bytes("commit", "src/subtitles/../DSUtil/DSUtil.h"), b"source")
            self.assertEqual(run.call_args.args[0], ["git", "show", "commit:src/dsutil/DSUtil.h"])

    def test_escape_and_untracked_headers_are_refused(self):
        for name in ("../outside.h", "src/../../outside.h", "/outside.h", r"C:\outside.h"):
            with self.assertRaises(ValueError):
                normalize_windows_source(name)
        with patch("summarize_evidence.source_paths", return_value={}):
            with self.assertRaises(KeyError):
                windows_source_bytes("commit", "src/untracked.h")


if __name__ == "__main__":
    unittest.main()
