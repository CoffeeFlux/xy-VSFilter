import subprocess
import unittest

from check_decoder import ROOT, extract_fenced_method, extract_method


class SourceExtractionTests(unittest.TestCase):
    def test_preserves_alternative_opening_branches(self):
        body = "void A() {\n#if OPTION\nif (one) {\n#else\nif (two) {\n#endif\nwork();\n}\n}"
        self.assertEqual(extract_fenced_method(body + "\n\nvoid B() {}", "void A()", "void B()"), body)
        with self.assertRaises(ValueError):
            extract_fenced_method(body + "\n#endif\nvoid B() {}", "void A()", "void B()")
        with self.assertRaises(ValueError):
            extract_fenced_method(body, "void A()", "void B()")

    def test_actual_add_boundaries_and_pinned_equivalence(self):
        signature = "void CSimpleTextSubtitle::Add("
        next_signature = "void CSimpleTextSubtitle::AddSTSEntryOnly("
        current = (ROOT / "src/subtitles/STS.cpp").read_text(encoding="utf-8-sig")
        body = extract_fenced_method(current, signature, next_signature)
        self.assertIn("#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL", body)
        self.assertIn("    sub.readorder  =", body)
        self.assertIn("m_segments.InsertAt", body)
        self.assertNotIn(next_signature, body)
        pinned = subprocess.check_output([
            "git", "show", "4911cacb102bc0cbd41449793e54473265b04e62:src/subtitles/STS.cpp"
        ], cwd=ROOT).decode("utf-8-sig")
        self.assertEqual(extract_fenced_method(pinned, signature, next_signature),
                         extract_method(pinned, signature))


if __name__ == "__main__":
    unittest.main()
