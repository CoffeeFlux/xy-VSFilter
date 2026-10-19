import unittest
from rect_intern_native import STEPS, compare, validate


class RectangleTraceControls(unittest.TestCase):
    def trace(self):
        rows=[b'rect-provenance pointer_bits=64 ansi=932 oem=932']
        for i in range(STEPS):
            if i==293:rows.extend([b'rect-move empty_alias=1 distinct=1 count=1',b'rect-move restored_alias=1 count=1'])
            rows.append(f'rect-step index={i} op=0 slot=0 count=1'.encode())
            rows.extend(f'rect-held slot={s} present=1 aliases=ffff value=0,0,0,0'.encode() for s in range(16))
        rows.extend([f'rect-complete steps={STEPS}'.encode(),b'rect-shutdown count=0'])
        return b'\n'.join(rows)+b'\n'

    def test_aliases_raw_longs_order_and_shutdown_are_required(self):
        good=self.trace();compare([good]*4,64)
        for bad in (good.replace(b'index=1 ',b'index=0 '),good.replace(b'aliases=ffff',b'aliases=fffe',1),
                    good.replace(b'value=0,0,0,0',b'value=2147483648,0,0,0'),good.replace(b'rect-shutdown count=0',b'rect-shutdown count=1')):
            with self.assertRaises(ValueError):validate(bad,64)
        with self.assertRaises(ValueError):validate(good,32)
        with self.assertRaises(ValueError):compare([good]*3,64)
        with self.assertRaises(ValueError):compare([good,good,good.replace(b'op=0',b'op=1'),good],64)
