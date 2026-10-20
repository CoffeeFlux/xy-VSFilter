import unittest
from blur_dispatch_native import CASES,compare,validate
class DispatchControls(unittest.TestCase):
    def trace(self):
        status='0,0,1234,305419896';rows=['blur-dispatch pointer_bits=32 be_inputs=12 gaussian_inputs=16']
        for i in range(CASES):rows.append(f'dispatch index={i} positive={int(i>=384)} be=00000000 gaussian=0000000000000000 sticky={i%2} active=1 result=1 null=0 preserved=1 memory_delta=0 before={status} predicate={status} after={status} missing={status}')
        return ('\n'.join(rows+[f'blur-dispatch-complete cases={CASES}'])+'\n').encode()
    def test_contract_order_and_complete_records_are_required(self):
        good=self.trace();compare([good]*4,32)
        for a,b in [(b'index=1 ',b'index=0 '),(b'result=1',b'result=0'),(b'null=0',b'null=1'),(b'preserved=1',b'preserved=0'),(b'memory_delta=0',b'memory_delta=1'),(b'cases=400',b'cases=399')]:
            with self.assertRaises(ValueError):validate(good.replace(a,b,1),32)
        with self.assertRaises(ValueError):validate(good,64)
        with self.assertRaises(ValueError):validate(good+b'extra\n',32)
    def test_status_returns_and_repeats_must_match(self):
        good=self.trace()
        for a,b in [(b'after=0,0',b'after=1,0'),(b'active=1 result=1',b'active=0 result=0')]:
            bad=good.replace(a,b,1)
            with self.assertRaises(ValueError):compare([good,good,bad,bad],32)
            with self.assertRaises(ValueError):compare([good,bad,good,good],32)
        with self.assertRaises(ValueError):compare([good]*3,32)
