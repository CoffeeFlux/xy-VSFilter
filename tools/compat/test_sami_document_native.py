import unittest

from sami_document_native import CASES, compare, validate


class SamiTraceControls(unittest.TestCase):
    def trace(self):
        rows = [b"sami-provenance pointer_bits=64 ansi=1252 oem=437"]
        rows += [f"sami-case index={i} outcome={(-101,0,1)[i%3]} position={i*2}".encode() for i in range(CASES)]
        rows += [f"sami-complete cases={CASES}".encode()]
        return b"\n".join(rows)+b"\n"

    def test_missing_duplicate_reordered_and_changed_traces_are_not_passes(self):
        good = self.trace()
        compare([good]*4,64)
        rows=good.splitlines(keepends=True)
        for bad in (b"".join(rows[:-1]),b"".join(rows[:2]+rows[3:]),
                    b"".join(rows[:2]+rows[1:]), b"".join(rows[:1]+[rows[2],rows[1]]+rows[3:]),
                    good.replace(b"outcome=-101",b"outcome=-102")):
            with self.assertRaises(ValueError):
                validate(bad,64)
        with self.assertRaises(ValueError):
            compare([good]*3,64)
        with self.assertRaises(ValueError):
            compare([good]*3+[good.replace(b"position=2\n",b"position=4\n")],64)
        with self.assertRaises(ValueError):
            validate(good,32)
