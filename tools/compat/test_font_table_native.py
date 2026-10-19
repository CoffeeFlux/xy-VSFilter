import unittest
from font_table_native import STEPS, compare, validate


class FontTableTraceControls(unittest.TestCase):
    def trace(self):
        rows=[b'font-table pointer_bits=64']
        for i in range(STEPS):
            rows += [f'drop {i} 0'.encode()]
            if i==0: rows += [b'create 1',b'eq 1 1 0',b'destroy 1']
            rows += [f'state {i} 0 7 '.encode()+b'0'*176+b' -']
        rows += [f'font-table-complete steps={STEPS} fonts=7'.encode()]
        return b'\n'.join(rows)+b'\n'

    def test_lifecycle_order_architecture_aliases_and_repeats(self):
        good=self.trace()
        self.assertEqual(compare([good]*4,64),1)
        for bad in [good.replace(b'destroy 1',b'destroy 2'),
                    good.replace(b'create 1',b'create 1\ncreate 1'),
                    good.replace(b'destroy 1\n',b''),
                    good.replace(b'drop 1 0',b'drop 0 0'),
                    good.replace(b'eq 1 1 0',b'eq 1 1 2'),
                    good.replace(b'fonts=7',b'fonts=0'),
                    good.replace(b'0'*176+b' -',b'01'+b'0'*174+b' -',1),
                    good[:-20]]:
            with self.assertRaises(ValueError): validate(bad,64)
        with self.assertRaises(ValueError): validate(good,32)
        with self.assertRaises(ValueError): compare([good]*3,64)
        with self.assertRaises(ValueError): compare([good,good,good.replace(b'eq 1 1 0',b'eq 1 1 1'),good],64)
