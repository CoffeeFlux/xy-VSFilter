import unittest
from gaussian_coefficients_native import CASES, INPUTS, compare, validate


class CoefficientControls(unittest.TestCase):
    def trace(self):
        status='0,0,1234,305419896'
        rows=[f'gaussian-coefficients pointer_bits=32 inputs={INPUTS}']
        for index in range(CASES):
            rows += [f'coefficient index={index} input=3ff0000000000000 radius=1 width=3 extended=4 sigma=3ff0000000000000 present=1 shared=1 count=1 memory_delta=32 before={status} first={status} second={status} copy={status}',
                     'values '+32*'0',f'release count=0 memory_delta=0 status={status}']
        rows.append(f'gaussian-coefficients-complete cases={CASES}')
        return ('\n'.join(rows)+'\n').encode()

    def test_case_order_shapes_allocation_and_completion_are_required(self):
        good=self.trace();compare([good]*4,32)
        for old,new in [(b'index=1 ',b'index=0 '),(b'extended=4',b'extended=3'),
                        (b'memory_delta=32',b'memory_delta=16'),(b'values ',b'values 0'),
                        (b'cases=212',b'cases=211')]:
            with self.subTest(old=old),self.assertRaises(ValueError):validate(good.replace(old,new,1),32)
        with self.assertRaises(ValueError):validate(good,64)
        with self.assertRaises(ValueError):validate(good+b'extra\n',32)

    def test_stable_pixels_status_sigma_and_sharing_must_match_reference(self):
        good=self.trace()
        for old,new in [(b'values '+32*b'0',b'values '+31*b'0'+b'1'),
                        (b'first=0,0',b'first=1,0'),(b'sigma=3ff0',b'sigma=3ff1')]:
            bad=good.replace(old,new,1)
            with self.subTest(old=old),self.assertRaises(ValueError):compare([good,good,bad,bad],32)
            with self.assertRaises(ValueError):compare([good,bad,good,good],32)
        with self.assertRaises(ValueError):compare([good]*3,32)


if __name__=='__main__':unittest.main()
