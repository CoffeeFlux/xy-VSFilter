import unittest
from subviewer_reference_outcomes import observations


class ReferenceObservations(unittest.TestCase):
    def test_missing_and_changed_rows_do_not_validate(self):
        trace=b"subviewer-reference pointer_bits=64\n"+b"\n".join(f"case={i} accepted=1 entries=1 1 0061".encode() for i in range(32))
        self.assertEqual(len(observations(trace,64)),32)
        for bad in (trace.replace(b"case=1 ",b"case=0 "),trace.replace(b"entries=1 1",b"entries=1 2"),trace.rsplit(b"\n",1)[0]):
            with self.assertRaises(ValueError):observations(bad,64)
        with self.assertRaises(ValueError):observations(trace,32)
