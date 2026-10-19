import unittest

from style_intern_native import CASES, compare, validate, validate_lifecycle


class StyleTraceControls(unittest.TestCase):
    def test_lifecycle_requires_sticky_status_move_aliases_and_late_release(self):
        good=b'style-move copy=1 empty=1 distinct=1 count=1\nstyle-move restored=1 source_empty=1 count=1\nstyle-sticky before=00000008 after=00000008 domains=0000000800000000,0000000800000000 count=0 rects=0\nstyle-lifecycle-complete count=1\nstyle-late count=0 rects=0 valid=1\n'
        validate_lifecycle(good)
        for bad in [good.replace(b'empty=1',b'empty=0'),good.replace(b'after=00000008',b'after=00000000'),good.replace(b'valid=1',b'valid=0'),good.replace(b'00000008',b'00000000')]:
            with self.assertRaises(ValueError):validate_lifecycle(bad)
    def trace(self):
        rows = [b"style-provenance pointer_bits=64 ansi=932 oem=932"]
        value = (b"style-value rect=0,0,0,0 ints=0,0,0,0,0,0,0,0 font=1:d800 colors="
                 + b"0" * 40 + b" doubles=" + b"0" * 240)
        for i in range(CASES):
            rows.append(f"style-intern index={i} alias=1 copy_alias=1 value_equal=1 self_equal=1 count=1 rects=1 fp=00000000 hash=0000000000000000,0000000000000000".encode())
            rows.append(f"style-fp index={i} first=00000000 second=00000000 copy=00000000 read=00000000 equality=00000000 assigned=00000000 assigned_domain=0000000000000000 domains=0000000000000000,0000000000000000,0000000000000000,0000000000000000,0000000000000000".encode())
            rows.extend([value, value])
        rows.extend([b"style-release-fp fp=00000000 domains=0000000000000000",b"style-final finite_released=1 retained_nan=90 styles=45 rects=1",
                     f"style-intern-complete cases={CASES}".encode()])
        return b"\n".join(rows) + b"\n"

    def test_complete_raw_values_order_nan_accounting_and_repeats(self):
        good = self.trace()
        compare([good] * 4, 64)
        for bad in (good.replace(b"index=1 ", b"index=0 "),
                    good.replace(b"font=1:d800", b"font=2:d800", 1),
                    good.replace(b"retained_nan=90", b"retained_nan=89"),
                    good.replace(b"copy_alias=1", b"copy_alias=0", 1),
                    good.replace(b"read=00000000", b"read=", 1),
                    good.replace(b"doubles=0", b"doubles=", 1)):
            with self.assertRaises(ValueError):
                validate(bad, 64)
        with self.assertRaises(ValueError):
            validate(good, 32)
        with self.assertRaises(ValueError):
            compare([good] * 3, 64)
        with self.assertRaises(ValueError):
            compare([good, good, good.replace(b"self_equal=1", b"self_equal=0", 1), good], 64)

    def test_domain_only_changes_cannot_pass_comparison(self):
        good=self.trace()
        bad=good.replace(b'domains=0000000000000000,',b'domains=0000000100000000,',1)
        validate(bad,64)
        with self.assertRaises(ValueError):compare([good,good,bad,bad],64)
        with self.assertRaises(ValueError):validate(good.replace(b' domains=0000000000000000,',b' ',1),64)
        same=good.replace(b'retained_nan=90 styles=45',b'retained_nan=135 styles=90')
        compare([same]*4,64,'same-values')
        compare([same]*4,64,'raw-twins')
        with self.assertRaises(ValueError):validate(good,64,'same-values')

    def test_add_style_names_values_status_and_completion(self):
        value=self.trace().splitlines()[3]
        rows=[b'style-add-provenance pointer_bits=64']
        for index in range(360):
            count=1+index%2
            rows.append(f'style-add index={index} count={count} first=00000000 second=00000000 domains=0000000000000000,0000000000000000'.encode())
            rows.extend([b'style-add-name present=1',value])
            rows.append(f'style-add-name present={count-1}'.encode())
            if count==2:rows.append(value)
        rows.append(b'style-add-complete cases=360')
        good=b'\n'.join(rows)+b'\n'
        compare([good]*4,64,'add-style')
        for bad in [good.replace(b'index=1 ',b'index=0 ',1),good.replace(b'present=1',b'present=0',1),good.replace(b'cases=360',b'cases=359'),good.replace(b'font=1:d800',b'font=2:d800',1),good.replace(b' domains=',b' status=',1)]:
            with self.assertRaises(ValueError):validate(bad,64,'add-style')
        bad=good.replace(b'domains=0000000000000000,',b'domains=0000000100000000,',1)
        with self.assertRaises(ValueError):compare([good,good,bad,bad],64,'add-style')
