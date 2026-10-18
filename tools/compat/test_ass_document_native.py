import unittest

from ass_document_native import CASES, compare, validate


class AssTraceControls(unittest.TestCase):
    def trace(self):
        rows = [b"ass-provenance pointer_bits=64 ansi=1252 oem=437"]
        rows += [f"ass-case index={i} outcome={(-101,0,1)[i%3]} position={i*2}".encode() for i in range(CASES)]
        rows += [b"ass-font index=0 bytes=2 data=007f", b"ass-font-complete calls=1", f"ass-complete cases={CASES}".encode()]
        return b"\n".join(rows)+b"\n"

    def test_missing_duplicate_reordered_and_changed_traces_are_not_passes(self):
        good = self.trace()
        compare([good]*4,64)
        rows=good.splitlines(keepends=True)
        for bad in (b"".join(rows[:-1]),b"".join(rows[:2]+rows[3:]),
                    b"".join(rows[:2]+rows[1:]), b"".join(rows[:1]+[rows[2],rows[1]]+rows[3:]),
                    good.replace(b"outcome=-101",b"outcome=-102"),
                    good.replace(b"bytes=2",b"bytes=3"),
                    good.replace(b"ass-font index=0",b"ass-font index=1"),
                    good.replace(b"calls=1",b"calls=0")):
            with self.assertRaises(ValueError):
                validate(bad,64)
        with self.assertRaises(ValueError):
            compare([good]*3,64)
        with self.assertRaises(ValueError):
            compare([good]*3+[good.replace(b"position=2\n",b"position=4\n")],64)
        with self.assertRaises(ValueError):
            validate(good,32)
