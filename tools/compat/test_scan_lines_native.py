import unittest
from scan_lines_native import CASES, compare, validate


class ScanControls(unittest.TestCase):
    def trace(self):
        rows=[b'scan-lines pointer_bits=32']
        for index in range(CASES):
            scenario=index//2
            count=0 if scenario and scenario%23==0 else 65 if scenario>=160 else 8
            size='0,0' if not count else f'{256+scenario},128'
            rows.append(f'scan-case index={index} scenario={scenario} sticky={index%2} points={count} result={int(bool(count))} size={size} next=5 capacity=2048 first=1:3,4 last=3,4 spans={int(bool(count))} before=0,0,1234,305419896 after=1,0,1234,305419896'.encode())
            if count:rows.append(b'scan-span 4000000040000000,4000000040000010')
        return b'\n'.join(rows+[b'scan-lines-complete cases=384'])+b'\n'

    def test_complete_order_inputs_spans_and_control_state_required(self):
        good=self.trace();compare([good]*4,32)
        for old,new in [(b'index=1 ',b'index=0 '),(b'points=8',b'points=9'),
                        (b'capacity=2048',b'capacity=0'),(b'capacity=2048',b'capacity=2049'),
                        (b'spans=1',b'spans=2'),(b'result=1',b'result=0'),
                        (b'after=1,0,1234,305419896',b'after=1'),(b'cases=384',b'cases=383')]:
            with self.subTest(old=old),self.assertRaises(ValueError):validate(good.replace(old,new,1),32)
        with self.assertRaises(ValueError):validate(good,64)
        with self.assertRaises(ValueError):validate(good+b'extra\n',32)

    def test_packed_spans_state_flags_and_reference_repeats_must_match(self):
        good=self.trace()
        for old,new in [(b'4000000040000010',b'4000000040000011'),(b'next=5',b'next=6'),
                        (b'first=1:3,4',b'first=1:3,5'),(b'last=3,4',b'last=3,5'),
                        (b'after=1,0',b'after=0,0'),(b'1234,305419896',b'1234,0')]:
            bad=good.replace(old,new,1)
            with self.subTest(old=old),self.assertRaises(ValueError):compare([good,good,bad,bad],32)
            with self.assertRaises(ValueError):compare([good,bad,good,good],32)
        with self.assertRaises(ValueError):compare([good]*3,32)


if __name__=='__main__':unittest.main()
