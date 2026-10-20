import unittest
from gaussian_cache_native import CASES,INPUTS,compare,validate
class CacheControls(unittest.TestCase):
    def trace(self):
        rows=[f'gaussian-cache pointer_bits=32 inputs={INPUTS}'];status='0,0,1234,305419896';drop=0
        for i in range(CASES):
            rows += [f'cache index={i} input=3ff0000000000000 retain={int(i>=212)} radius=1 width=3 extended=4 sigma=3ff0000000000000 present=1 shared=1 count=1 bytes=16 before={status} first={status} second={status}','values '+'0'*32,f'release count=0 bytes=0 status={status}']
            if i in [307,403]:
                for j in range(96):rows.append(f'drop index={drop} count=0 bytes=0 status={status}');drop+=1
        return ('\n'.join(rows+[f'gaussian-cache-complete cases={CASES} drops=192'])+'\n').encode()
    def test_order_accounting_shapes_and_completion_are_required(self):
        good=self.trace();compare([good]*4,32)
        for old,new in [(b'index=1 ',b'index=0 '),(b'extended=4',b'extended=3'),(b'bytes=16 ',b'bytes=32 '),(b'values ',b'values 0'),(b'drop index=0',b'drop index=1'),(b'drops=192',b'drops=191')]:
            with self.subTest(old=old),self.assertRaises(ValueError):validate(good.replace(old,new,1),32)
        with self.assertRaises(ValueError):validate(good,64)
        with self.assertRaises(ValueError):validate(good+b'extra\n',32)
    def test_bits_state_status_and_repeats_must_match(self):
        good=self.trace()
        for old,new in [(b'values 00',b'values 01'),(b'first=0,0',b'first=1,0'),(b'count=1 ',b'count=2 ')]:
            bad=good.replace(old,new,1)
            with self.assertRaises(ValueError):compare([good,good,bad,bad],32)
            with self.assertRaises(ValueError):compare([good,bad,good,good],32)
        with self.assertRaises(ValueError):compare([good]*3,32)
