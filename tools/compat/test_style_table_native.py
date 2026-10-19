import unittest
from style_table_native import STEPS, compare, validate


class StyleTableTraceControls(unittest.TestCase):
    def trace(self, edges=False):
        steps=720 if edges else STEPS
        rows = [b'style-table pointer_bits=64' + (b' mode=edges' if edges else b'')]
        for i in range(steps):
            rows += [f'drop {i} 0'.encode(), b'eq 1 1 1', b'eq 1 1 1', f'state {i} 0 1 0 -'.encode()]
        final='styles=45 rects=1' if edges else 'styles=1 rects=0'
        rows += [f'style-table-complete steps={steps} {final}'.encode()]
        return b'\n'.join(rows) + b'\n'

    def test_order_comparisons_final_accounting_and_independent_repeats(self):
        good = self.trace()
        self.assertEqual(compare([good] * 4, 64), STEPS*2)
        edges=self.trace(True)
        self.assertEqual(compare([edges]*4,64,True),1440)
        with self.assertRaises(ValueError):validate(edges,64)
        with self.assertRaises(ValueError):validate(good,64,True)
        for bad in [good.replace(b'drop 1 0', b'drop 0 0'),
                    good.replace(b'state 1 0', b'state 1 1'),
                    good.replace(b'eq 1 1 1', b'eq 1 1 2', 1),
                    good.replace(b'styles=1 rects=0', b'styles=0 rects=0')]:
            with self.assertRaises(ValueError): validate(bad, 64)
        with self.assertRaises(ValueError): validate(good, 32)
        with self.assertRaises(ValueError): compare([good] * 3, 64)
        with self.assertRaises(ValueError): compare([good, good, good.replace(b'eq 1 1 1', b'eq 1 1 0', 1), good], 64)
