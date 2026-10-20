import subprocess
import unittest

from check_decoder import ROOT, extract_definition, extract_fenced_method, extract_method, select_undefined_ifdef


class SourceExtractionTests(unittest.TestCase):
    def test_composite_historical_storage_selects_native_branch(self):
        source=(ROOT/'src/subtitles/Rasterizer.cpp').read_text(encoding='utf-8-sig')
        body=extract_definition(source,'SharedPtrAlphaMask Rasterizer::CompositeAlphaMask(')
        native=select_undefined_ifdef(body,'XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL')
        self.assertNotIn('CompositeOwner',native)
        self.assertNotIn('result.owner',native)
        self.assertIn('overlay->FillAlphaMash',native)
        self.assertIn('XyRust::PlaneAllocation',native)

    def test_preserves_alternative_opening_branches(self):
        body = "void A() {\n#if OPTION\nif (one) {\n#else\nif (two) {\n#endif\nwork();\n}\n}"
        self.assertEqual(extract_fenced_method(body + "\n\nvoid B() {}", "void A()", "void B()"), body)
        with self.assertRaises(ValueError):
            extract_fenced_method(body + "\n#endif\nvoid B() {}", "void A()", "void B()")
        with self.assertRaises(ValueError):
            extract_fenced_method(body, "void A()", "void B()")

    def test_selects_native_owner_branch_preserving_leaf_conditions(self):
        source="""before
#ifdef OWNER
bad();
#else
native();
#if LEAF
leaf();
#else
baseline();
#endif
#endif
after
"""
        selected=select_undefined_ifdef(source,"OWNER")
        self.assertEqual(selected,"before\nnative();\n#if LEAF\nleaf();\n#else\nbaseline();\n#endif\nafter\n")
        with self.assertRaises(ValueError):select_undefined_ifdef("#ifdef OWNER\n", "OWNER")
        with self.assertRaises(ValueError):select_undefined_ifdef("#if defined(OWNER)\n#endif\n", "OWNER")

    def test_actual_prepared_suffix_preprocesses_in_both_leaf_modes(self):
        import shutil
        compiler=shutil.which("c++")
        if not compiler:self.skipTest("C++ preprocessor unavailable")
        source=(ROOT/"src/subtitles/STS.cpp").read_text(encoding="utf-8-sig")
        body=extract_fenced_method(source,"void CSimpleTextSubtitle::Add(","void CSimpleTextSubtitle::AddSTSEntryOnly(")
        native=select_undefined_ifdef(body,"XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL")
        self.assertNotIn("AddFinalSuffix",native)
        start=native.index("    sub.readorder  =")
        suffix=native[start:]
        self.assertIn("m_segments.InsertAt",suffix)
        for defines in ([],["-DXY_VSFILTER_RUST_TEXT"]):
            result=subprocess.run([compiler,"-E","-P","-x","c++",*defines,"-"],input=suffix,text=True,capture_output=True)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn("sub.readorder",result.stdout)
            self.assertIn("XyRust::SegmentInsertion" if defines else "m_segments.InsertAt",result.stdout)

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
            "git", "show", "67da07cda068088b93e6de300f56cbad15ab6c5d:src/subtitles/STS.cpp"
        ], cwd=ROOT).decode("utf-8-sig")
        self.assertEqual(extract_fenced_method(pinned, signature, next_signature),
                         extract_method(pinned, signature))


if __name__ == "__main__":
    unittest.main()
