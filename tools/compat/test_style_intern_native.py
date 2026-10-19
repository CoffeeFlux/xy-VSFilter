import unittest

from style_intern_native import CASES, compare, validate


class StyleTraceControls(unittest.TestCase):
    def trace(self):
        rows = [b"style-provenance pointer_bits=64 ansi=932 oem=932"]
        value = (b"style-value rect=0,0,0,0 ints=0,0,0,0,0,0,0,0 font=1:d800 colors="
                 + b"0" * 40 + b" doubles=" + b"0" * 240)
        for i in range(CASES):
            rows.append(f"style-intern index={i} alias=1 copy_alias=1 value_equal=1 self_equal=1 count=1 rects=1 fp=00000000 hash=0000000000000000,0000000000000000".encode())
            rows.extend([value, value])
        rows.extend([b"style-final finite_released=1 retained_nan=90 styles=45 rects=1",
                     f"style-intern-complete cases={CASES}".encode()])
        return b"\n".join(rows) + b"\n"

    def test_complete_raw_values_order_nan_accounting_and_repeats(self):
        good = self.trace()
        compare([good] * 4, 64)
        for bad in (good.replace(b"index=1 ", b"index=0 "),
                    good.replace(b"font=1:d800", b"font=2:d800", 1),
                    good.replace(b"retained_nan=90", b"retained_nan=89"),
                    good.replace(b"copy_alias=1", b"copy_alias=0", 1),
                    good.replace(b"doubles=0", b"doubles=", 1)):
            with self.assertRaises(ValueError):
                validate(bad, 64)
        with self.assertRaises(ValueError):
            validate(good, 32)
        with self.assertRaises(ValueError):
            compare([good] * 3, 64)
        with self.assertRaises(ValueError):
            compare([good, good, good.replace(b"self_equal=1", b"self_equal=0", 1), good], 64)
