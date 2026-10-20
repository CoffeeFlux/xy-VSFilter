import unittest
from gaussian_filter_native import CASES,compare,validate

class FilterControls(unittest.TestCase):
    def trace(self):
        rows=['gaussian-filter pointer_bits=32'];status='0,0,1234,305419896'
        widths=[4,8,12,16,20,24,28,32,36,48,64,128];masks=[4,8,12,16,20,24,28,32,36,64]
        for i in range(CASES):
            w=widths[i//320];m=masks[i//32%10]
            rows += [f'filter index={i} width={w} mask={m} trim={i//8%4} pattern={i//2%4} sticky={i%2} before={status} after={status}','values '+'0'*((w+m+4)*3*8)]
        return ('\n'.join(rows+[f'gaussian-filter-complete cases={CASES}'])+'\n').encode()
    def test_complete_order_shapes_and_architecture_are_required(self):
        good=self.trace();compare([good]*4,32)
        for old,new in [(b'index=1 ',b'index=0 '),(b'mask=4 ',b'mask=8 '),(b'values ',b'values 0'),(b'cases=3840',b'cases=3839')]:
            with self.subTest(old=old),self.assertRaises(ValueError):validate(good.replace(old,new,1),32)
        with self.assertRaises(ValueError):validate(good,64)
        with self.assertRaises(ValueError):validate(good+b'extra\n',32)
    def test_bits_status_and_repeated_runs_must_match(self):
        good=self.trace()
        for old,new in [(b'values 00',b'values 01'),(b'after=0,0',b'after=1,0')]:
            bad=good.replace(old,new,1)
            with self.assertRaises(ValueError):compare([good,good,bad,bad],32)
            with self.assertRaises(ValueError):compare([good,bad,good,good],32)
        with self.assertRaises(ValueError):compare([good]*3,32)
