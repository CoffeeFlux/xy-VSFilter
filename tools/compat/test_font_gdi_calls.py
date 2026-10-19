import unittest
from font_gdi_calls import CASES,compare,validate

class GdiTraceControls(unittest.TestCase):
    def trace(self):
        rows=[b'font-gdi pointer_bits=64']
        for i in range(CASES):
            mode=i%3
            events=['getdc','caps','muldiv','releasedc']+['create']*(1 if mode==0 else 2)+['select','metrics','select']+([] if mode==2 else ['delete'])
            rows += [f'font-gdi-case index={i} mode={mode} events={len(events)} count=0 font={int(mode!=2)} ascent=2 descent=1 final=1,0,1,1234,0'.encode()]
            for name in events:
                rows += [f'gdi name={name} a=0 b=0 c=0 result=0 before=0,0,0,1234,0 after=1,0,1,1234,0 data='.encode()+b'0'*(184 if name=='create' else 114 if name=='metrics' else 0)]
        rows += [f'font-gdi-complete cases={CASES}'.encode()]
        return b'\n'.join(rows)+b'\n'
    def test_call_order_fallback_metrics_and_repeat_evidence(self):
        good=self.trace();compare([good]*4,64)
        for bad in [good.replace(b'name=caps',b'name=muldiv',1),good.replace(b'name=delete',b'name=select',1),good.replace(b'events=9',b'events=8',1),good.replace(b'mode=2 events=9 count=0 font=0',b'mode=2 events=9 count=0 font=1',1),good[:-20]]:
            with self.assertRaises(ValueError):validate(bad,64)
        with self.assertRaises(ValueError):compare([good]*3,64)
        with self.assertRaises(ValueError):compare([good,good,good.replace(b'before=0',b'before=1',1),good],64)
        with self.assertRaises(ValueError):validate(good,32)
