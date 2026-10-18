import unittest

from usf_document_native import CASES, LEGACY_CASES, compare, validate


class USFTraceControls(unittest.TestCase):
    def trace(self):
        rows = [b"usf-provenance pointer_bits=64 ansi=932 oem=932"]
        rows += [f"usf-case index={i} outcome={i % 2} position={i * 2}".encode() for i in range(CASES)]
        for i in range(LEGACY_CASES, CASES):
            rows += [f"usf-state index={i} phase={p} bytes=52 data={'00' * 52}".encode() for p in range(5)]
            rows += [f"usf-reload index={i} before=1 after=0".encode()]
        rows += [f"usf-complete cases={CASES}".encode()]
        return b"\n".join(rows) + b"\n"

    def test_incomplete_duplicate_exception_or_wrong_architecture_is_not_parity(self):
        good = self.trace()
        compare([good] * 4, 64)
        lines = good.splitlines(keepends=True)
        for bad in (b"".join(lines[:-1]), b"".join(lines[:2] + lines[3:]),
                    b"".join(lines[:2] + lines[1:]),
                    good.replace(b"outcome=0", b"outcome=-101")):
            with self.assertRaises(ValueError):
                validate(bad, 64)
        with self.assertRaises(ValueError):
            validate(good, 32)

    def test_missing_reordered_or_truncated_state_is_not_parity(self):
        good = self.trace()
        for bad in (good.replace(b"phase=4", b"phase=3"),
                    good.replace(b"bytes=52", b"bytes=51"),
                    good.replace(b"usf-reload ", b"missing-reload "),
                    good.replace(b"usf-state ", b"missing-state ")):
            with self.assertRaises(ValueError):
                compare([bad] * 4, 64)

    def test_each_side_must_repeat_and_document_content_must_match(self):
        good = self.trace()
        with self.assertRaises(ValueError):
            compare([good] * 3, 64)
        changed = good.replace(b"position=2\n", b"position=4\n")
        with self.assertRaises(ValueError):
            compare([good] * 3 + [changed], 64)
        # Non-header document/style records are part of the exact trace too.
        changed = good.replace(b"usf-case index=1", b"style changed\nusf-case index=1")
        with self.assertRaises(ValueError):
            compare([good, good, changed, changed], 64)
