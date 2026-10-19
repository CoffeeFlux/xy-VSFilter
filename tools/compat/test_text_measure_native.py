import unittest
from text_measure_native import RAW_BITS, compare, validate


class MeasurementControls(unittest.TestCase):
    def trace(self,raw_canonical=False):
        mode='raw-canonical' if raw_canonical else 'construction'
        rows=[f'text-measure pointer_bits=32 mode={mode}'.encode()]
        for i in range(432):
            if i%4==0:rows.append(f'text-measure-input index={i} bits={RAW_BITS[(i//4)%18]:016x}'.encode())
            rows.append(f'text-measure-case index={i} sticky={i//216} field={(i//72)%3} bits={RAW_BITS[(i//4)%18]:016x} history={i%4} events=3 output=1,2,3 before=0,0,1234,123 after=0,0,1234,123'.encode())
            for name in ('select','extent','select'):
                rows.append(f'text-measure-call name={name} count=1 result=1 width=7 before=0,0,1234,123 after=0,0,1234,123'.encode())
        rows.append(b'text-measure-complete cases=432')
        return b'\n'.join(rows)+b'\n'

    def test_all_cases_bits_event_counts_and_restoration_required(self):
        good=self.trace();compare([good]*4,32)
        for bad in [good.replace(b'index=1 ',b'index=0 ',1),good.replace(b'events=3 ',b'events=4 ',1),good.replace(b'bits=0000000000000000',b'bits=0000000000000001',1),good.replace(b'name=select ',b'name=extent ',1),good.replace(b'cases=432',b'cases=431')]:
            with self.assertRaises(ValueError):validate(bad,32)
        with self.assertRaises(ValueError):validate(good,64)

    def test_outputs_flags_and_both_repeats_must_match(self):
        good=self.trace()
        for changed in [good.replace(b'output=1,2,3',b'output=2,2,3',1),good.replace(b'after=0,0,1234,123',b'after=1,0,1234,123',1),good.replace(b'result=1',b'result=0',1)]:
            with self.assertRaises(ValueError):compare([good,good,changed,changed],32)
            with self.assertRaises(ValueError):compare([good,changed,good,good],32)
        with self.assertRaises(ValueError):compare([good]*3,32)

    def test_raw_input_mode_cannot_accept_construction_or_changed_input(self):
        good=self.trace(True);compare([good]*4,32,True)
        with self.assertRaises(ValueError):validate(good,32)
        with self.assertRaises(ValueError):validate(self.trace(),32,True)
        bad=good.replace(b'text-measure-input index=0 bits=0000000000000000',b'text-measure-input index=0 bits=0000000000000001')
        with self.assertRaises(ValueError):compare([bad]*4,32,True)


if __name__=='__main__':unittest.main()
