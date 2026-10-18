import unittest

from usf_document_native import CASES, LEGACY_CASES, SCALAR_CASES, SCALAR_VALUES, LOAD_INPUTS, LOAD_CASES, compare, validate


class USFTraceControls(unittest.TestCase):
    def trace(self):
        rows = [b"usf-provenance pointer_bits=64 ansi=932 oem=932"]
        rows += [f"usf-case index={i} outcome={i % 2} position={i * 2}".encode() for i in range(CASES)]
        for i in range(LEGACY_CASES, CASES):
            rows += [f"usf-state index={i} phase={p} bytes=52 data={'00' * 52}".encode() for p in range(5)]
            rows += [f"usf-reload index={i} before=1 after=0".encode()]
        rows += [f"usf-scalar index={i} kind=1 argument=0 result=-1 errno=1234 units=0 data=".encode() for i in range(SCALAR_CASES)]
        rows += [f"usf-scalars-complete cases={SCALAR_CASES} values={SCALAR_VALUES}".encode()]
        for i in range(LOAD_CASES):
            rows += [f"usf-load-state index={i} phase={p} bytes=52 data={'00' * 52}".encode() for p in range(3)]
            rows += [f"usf-load index={i} mode=0 apartment=0 preload=0 outcome=0 bytes=0".encode()]
        rows += [f"usf-bstr index={i} length=0 constructed=0 assigned=0 lower=00000000 data=".encode() for i in range(5)]
        rows += [b"usf-array vt=8209 original_locks=1 copied_locks=0 original_features=128 copied_features=128", b"usf-msxml-desktop value=1"]
        rows += [f"usf-loads-complete cases={LOAD_CASES} inputs={LOAD_INPUTS}".encode()]
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
                    good.replace(b"usf-state ", b"missing-state "),
                    good.replace(b"usf-scalar ", b"missing-scalar "),
                    good.replace(b"units=0", b"units=1"),
                    good.replace(b"apartment=0 preload=0 outcome=0", b"apartment=0 preload=0 outcome=1"),
                    good.replace(b"usf-load-state ", b"missing-load-state ")):
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
