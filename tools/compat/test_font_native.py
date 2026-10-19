import unittest
from font_native import CASES,compare,validate

class FontTraceControls(unittest.TestCase):
    def trace(self):
        rows=[b'font-provenance pointer_bits=64 ansi=932 oem=932 logfont=92 metric=60']
        for i in range(CASES):
            rows += [f'font-case index={i} count=1 aliases=11 ascent=2 descent=1 extent=50,20 first=1,1234,0 second=0,1234,0 copy=0,1234,0 read=0,1234,0 logfont='.encode()+b'0'*184+b' metric='+b'0'*120,
                     f'font-release index={i} count=0 a=0,1234,0 b=0,1234,0 c=0,1234,0'.encode()]
        rows += [f'font-complete cases={CASES} count=0'.encode()]
        return b'\n'.join(rows)+b'\n'
    def test_repeat_lifetime_abi_and_observation_changes(self):
        good=self.trace();compare([good]*4,64)
        for bad in [good.replace(b'index=1 ',b'index=0 ',1),good.replace(b'aliases=11',b'aliases=10',1),good.replace(b'logfont=92',b'logfont=90'),good[:-20],good.replace(b'font-release index=0 count=0',b'font-release index=0 count=1')]:
            with self.assertRaises(ValueError):validate(bad,64)
        with self.assertRaises(ValueError):compare([good]*3,64)
        with self.assertRaises(ValueError):validate(good,32)
        with self.assertRaises(ValueError):compare([good,good,good.replace(b'first=1',b'first=0',1),good],64)
