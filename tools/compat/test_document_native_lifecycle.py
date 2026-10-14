import unittest

from document_native_lifecycle import compare_traces


TRACE = (b"provenance pointer_bits=64\nstate copy\nentry 0 raw=0041,0000,0042\n"
         b"segment 0 10 20\ncallback changed\ntext 0041\n"
         b"document-native-lifecycle-v1 complete\n")


class NativeLifecycleEvidenceTests(unittest.TestCase):
    def test_requires_two_complete_equal_executions_per_side(self):
        compare_traces([TRACE] * 4, 64)
        with self.assertRaises(ValueError):
            compare_traces([TRACE] * 3, 64)
        with self.assertRaises(ValueError):
            compare_traces([TRACE, TRACE, TRACE, TRACE.rsplit(b"\n", 2)[0]], 64)

    def test_rejects_architecture_and_missing_observations(self):
        with self.assertRaises(ValueError):
            compare_traces([TRACE] * 4, 32)
        for section in (b"entry ", b"segment ", b"callback ", b"text ", b"state "):
            incomplete = b"\n".join(line for line in TRACE.split(b"\n") if not line.startswith(section))
            with self.assertRaises(ValueError):
                compare_traces([incomplete] * 4, 64)

    def test_rejects_suffix_changes_after_embedded_nul(self):
        changed = TRACE.replace(b"0041,0000,0042", b"0041,0000,0043")
        with self.assertRaises(ValueError):
            compare_traces([TRACE, TRACE, changed, changed], 64)

    def test_rejects_repeat_nondeterminism(self):
        changed = TRACE.replace(b"callback changed", b"callback empty")
        with self.assertRaises(ValueError):
            compare_traces([TRACE, changed, TRACE, TRACE], 64)


if __name__ == "__main__":
    unittest.main()
