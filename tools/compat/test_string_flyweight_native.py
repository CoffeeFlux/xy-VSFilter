import unittest
from string_flyweight_native import STEPS, compare, validate


class StringTraceControls(unittest.TestCase):
    def trace(self):
        rows=[b'string-provenance pointer_bits=64 ansi=932 oem=932']
        for i in range(STEPS):
            rows.append(f'string-step index={i} op=0 slot=0 max=16384 count=1 hits=0 queries=1'.encode())
            rows.extend(f'held slot={s} id=1 units=2 hex=00410000'.encode() for s in range(12))
        rows.extend([b'string-move source_id=1 intermediate_id=1 destination_id=1 text=1',f'string-complete steps={STEPS}'.encode(),b'string-shutdown released=1'])
        return b'\n'.join(rows)+b'\n'

    def test_complete_ordered_raw_values_and_late_release_are_required(self):
        good=self.trace();compare([good]*4,64)
        for bad in (good.replace(b'index=1 ',b'index=0 '),good.replace(b'units=2',b'units=1'),
                    good.replace(b'string-shutdown released=1\n',b''),good.replace(b'held slot=1 ',b'held slot=0 ')):
            with self.assertRaises(ValueError):validate(bad,64)
        with self.assertRaises(ValueError):validate(good,32)
        with self.assertRaises(ValueError):compare([good]*3,64)
        with self.assertRaises(ValueError):compare([good,good,good.replace(b'hits=0',b'hits=1'),good],64)
