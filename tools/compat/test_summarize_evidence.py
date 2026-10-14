import unittest
from unittest.mock import patch

from summarize_evidence import normalize_windows_source, windows_source_bytes


class WindowsHeaderProvenance(unittest.TestCase):
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
