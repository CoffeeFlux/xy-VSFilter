import unittest
from wide_region_native import CASES, BORDERS, compare, validate

class WideControls(unittest.TestCase):
    def trace(self):
        rows=[b'widen pointer_bits=32']
        for index in range(CASES):
            scenario=index//64;radius=index//2%32;count=int(radius>=2 and scenario%8!=7)
            rows.append(f'widen-case index={index} scenario={scenario} radius={radius} sticky={index%2} result=1 border={BORDERS[radius]} spans={count} before=0,0,1234,305419896 after=1,0,1234,305419896'.encode())
            if count:rows.append(b'widen-span 4000000040000000,4000000040000010')
        return b'\n'.join(rows+[b'widen-complete cases=1536'])+b'\n'
    def test_completion_order_inputs_and_empty_controls(self):
        good=self.trace();compare([good]*4,32)
        for old,new in [(b'index=1 ',b'index=0 '),(b'radius=0',b'radius=1'),(b'border=1 ',b'border=2 '),(b'spans=1',b'spans=0'),(b'spans=0',b'spans=1'),(b'result=1',b'result=0'),(b'cases=1536',b'cases=1535'),(b'after=1,0,1234,305419896',b'after=1')]:
            with self.subTest(old=old),self.assertRaises(ValueError):validate(good.replace(old,new,1),32)
        with self.assertRaises(ValueError):validate(good,64)
        with self.assertRaises(ValueError):validate(good+b'extra\n',32)
    def test_pixels_status_and_both_reference_repeats_must_match(self):
        good=self.trace()
        for old,new in [(b'4000000040000010',b'4000000040000011'),(b'after=1,0',b'after=0,0'),(b'1234,305419896',b'1234,0')]:
            bad=good.replace(old,new,1)
            with self.assertRaises(ValueError):compare([good,good,bad,bad],32)
            with self.assertRaises(ValueError):compare([good,bad,good,good],32)
        with self.assertRaises(ValueError):compare([good]*3,32)

if __name__=='__main__':unittest.main()
