import unittest
from style_table_native import STEPS, compare, validate


class StyleTableTraceControls(unittest.TestCase):
    def trace(self):
        rows = [b'style-table pointer_bits=64']
        for i in range(STEPS):
            rows += [f'drop {i} 0'.encode(), b'eq 1 1 1', f'state {i} 0 1 0 -'.encode()]
        rows += [f'style-table-complete steps={STEPS} styles=1 rects=0'.encode()]
        return b'\n'.join(rows) + b'\n'

    def test_order_comparisons_final_accounting_and_independent_repeats(self):
        good = self.trace()
        self.assertEqual(compare([good] * 4, 64), STEPS)
        for bad in [good.replace(b'drop 1 0', b'drop 0 0'),
                    good.replace(b'state 1 0', b'state 1 1'),
                    good.replace(b'eq 1 1 1', b'eq 1 1 2', 1),
                    good.replace(b'styles=1 rects=0', b'styles=0 rects=0')]:
            with self.assertRaises(ValueError): validate(bad, 64)
        with self.assertRaises(ValueError): validate(good, 32)
        with self.assertRaises(ValueError): compare([good] * 3, 64)
        with self.assertRaises(ValueError): compare([good, good, good.replace(b'eq 1 1 1', b'eq 1 1 0', 1), good], 64)
