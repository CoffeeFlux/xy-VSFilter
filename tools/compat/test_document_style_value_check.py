import unittest
from pathlib import Path
from unittest.mock import patch
import document_style_value_check as check


def wire():
    data = bytearray(196)
    data[64:68] = (2).to_bytes(4, 'little')
    data[72:80] = (0x8000000000000000).to_bytes(8, 'little')
    data[192:] = b'\x00\xd8\x00\x00'
    return bytes(data)


def trace(native, values=None):
    values = values or [wire(), wire()]
    lines = ['provenance pointer_bits=64'] if native else []
    lines += [f'value {i} {value.hex()}' for i, value in enumerate(values)]
    lines += [f'equal {i} {j} 1' for i in range(len(values)) for j in range(len(values))]
    if native:
        lines += [f'summary values={len(values)} equalities={len(values)**2}']
    lines += [check.NATIVE_COMPLETE if native else check.RUST_COMPLETE]
    return ('\n'.join(lines) + '\n').encode()


class StyleValueRunnerTests(unittest.TestCase):
    def test_complete_pair(self):
        self.assertEqual(check.compare_traces([trace(True)] * 2, [trace(False)] * 2, 64), 2)

    def test_commit_bytes(self):
        with patch.object(check.com, 'git', return_value=(check.PIN + '\r\n').encode()):
            self.assertEqual(check.commit_id(Path('/reference')), check.PIN)

    def test_truncated_matrix_duplicate_wrong_order_and_completion(self):
        valid = trace(True)
        for bad in (valid.replace(b'equal 1 1 1\n', b''),
                    valid.replace(b'equal 1 1 1', b'equal 0 0 1'),
                    valid.replace(check.NATIVE_COMPLETE.encode(), b''),
                    valid.replace(b'equalities=4', b'equalities=3'),
                    valid + b'extra\n'):
            with self.subTest(bad=bad):
                with self.assertRaises(ValueError): check.parse_trace(bad, 64, True)

    def test_truncated_wire_and_invalid_fields(self):
        original = wire()
        for length in (0, 191, 192, 195):
            with self.assertRaises(ValueError): check.validate_wire(original[:length].hex(), 64)
        for index, value in ((56, 2), (59, 1), (68, 1)):
            bad = bytearray(original); bad[index] = value
            with self.assertRaises(ValueError): check.validate_wire(bad.hex(), 64)
        with self.assertRaises(ValueError): check.validate_wire(original.hex() + '0', 64)

    def test_float_bits_are_compared_without_numeric_coercion(self):
        changed = bytearray(wire()); changed[79] = 0  # negative zero -> positive zero
        rust = trace(False, [bytes(changed), wire()])
        with self.assertRaisesRegex(ValueError, 'wire bits'):
            check.compare_traces([trace(True)] * 2, [rust] * 2, 64)

    def test_equality_mismatch_and_repeat_nondeterminism(self):
        native, rust = trace(True), trace(False)
        changed = rust.replace(b'equal 0 1 1', b'equal 0 1 0')
        with self.assertRaisesRegex(ValueError, 'equality'):
            check.compare_traces([native] * 2, [changed] * 2, 64)
        with self.assertRaisesRegex(ValueError, 'nondeterministic'):
            check.compare_traces([native] * 2, [rust, changed], 64)
        with self.assertRaises(ValueError): check.compare_traces([native], [rust] * 2, 64)

    def test_value_bound(self):
        with self.assertRaises(ValueError): check.parse_trace(trace(False, [wire()] * 257), 64, False)


if __name__ == '__main__':
    unittest.main()
