import unittest
from html_tag_native import INPUTS, CASES, compare, validate

class HtmlTraceControls(unittest.TestCase):
    def trace(self):
        rows=[b'html-provenance pointer_bits=64 ansi=932 oem=932']
        for index in range(CASES):
            group=int(index>=INPUTS*4);seed=index//INPUTS if not group else (index-INPUTS*4)//24
            rows.extend([f'html-case index={index} seed={seed} group={group} accepted=1 errno=1234 ktype=7 kstart=10 kend=-9 input=0:'.encode(),b'style current',b'style original'])
        rows.append(f'html-complete cases={CASES} inputs={INPUTS}'.encode())
        return b'\n'.join(rows)+b'\n'
    def test_missing_or_misordered_observations_refuse_parity(self):
        good=self.trace();compare([good]*4,64)
        for bad in (good.replace(b'index=1 ',b'index=0 '),good.replace(b'seed=3',b'seed=2'),good.replace(b'input=0:',b'input=1:'),good.replace(b'style original\n',b'')):
            with self.assertRaises(ValueError):validate(bad,64)
        with self.assertRaises(ValueError):validate(good,32)
    def test_state_style_errno_and_repetition_must_match(self):
        good=self.trace()
        for before,after in [(b'kend=-9',b'kend=9'),(b'style current',b'style different'),(b'errno=1234',b'errno=34')]:
            with self.assertRaises(ValueError):compare([good,good,good.replace(before,after),good.replace(before,after)],64)
        with self.assertRaises(ValueError):compare([good]*3,64)
