import unittest
from ass_tree_native import INPUTS, CASES, compare, validate, validate_tree


class AssTreeTraceControls(unittest.TestCase):
    def trace(self):
        rows=[b'tree-provenance pointer_bits=64 ansi=932 oem=932']
        for index in range(CASES):
            rows.extend([f'tree-case index={index} fixture={index//3} mode={index%3} accepted=1 errno=1234 units=0'.encode(),b'tree bytes=4 hex=00000000'])
        rows.append(f'tree-complete cases={CASES} inputs={INPUTS} null_refused=1'.encode())
        return b'\n'.join(rows)+b'\n'

    def test_case_order_architecture_and_repeat_are_required(self):
        good=self.trace();compare([good]*4,64)
        for bad in (good.replace(b'index=1 ',b'index=0 '),good.replace(b'mode=2 ',b'mode=1 '),good.replace(b'bytes=4',b'bytes=8')):
            with self.assertRaises(ValueError):validate(bad,64)
        with self.assertRaises(ValueError):validate(good,32)
        with self.assertRaises(ValueError):compare([good]*3,64)
        with self.assertRaises(ValueError):compare([good,good,good.replace(b'errno=1234',b'errno=34'),good],64)

    def test_tree_structure_and_parameter_bounds_are_required(self):
        def words(*values):return b''.join(v.to_bytes(4,'little') for v in values)
        validate_tree(words(1,48,0,1,1)+b'A\x00'+words(1,14,0,0,0))
        for data in (words(1),words(1,55,0,0,0),words(1,14,3,0,0),words(1,14,0,1,100),words(0,0)):
            with self.assertRaises(ValueError):validate_tree(data)
