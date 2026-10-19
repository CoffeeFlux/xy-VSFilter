import unittest
from word_transform_native import CASES, compare, expected_cases, validate


class TransformControls(unittest.TestCase):
    def trace(self):
        rows = [b'transform pointer_bits=32']
        for prefix, empty in expected_cases():
            rows.append(prefix + b' before=0,0,1234,305419896 after=1,0,1234,305419896')
            count = 0 if empty else 5
            rows.append(f'transform-path count={count} hash=01234567'.encode() + b' ff:13,-7' * count)
        return b'\n'.join(rows + [f'transform-complete cases={CASES}'.encode()]) + b'\n'

    def test_requires_complete_ordered_inputs_paths_and_status(self):
        good = self.trace()
        compare([good] * 4, 32)
        for old, new in [(b'index=1 ', b'index=0 '), (b'field=1 ', b'field=0 '),
                         (b'bits=8000000000000000', b'bits=0000000000000000'),
                         (b'count=5', b'count=0'), (b' ff:13,-7', b''),
                         (b' ff:13,-7', b' 00:13,-7'), (b'after=1,0,1234,305419896', b'after=1'),
                         (b'cases=4560', b'cases=4559')]:
            with self.subTest(old=old), self.assertRaises(ValueError):
                validate(good.replace(old, new, 1), 32)
        with self.assertRaises(ValueError):
            validate(good, 64)
        with self.assertRaises(ValueError):
            validate(good + b'extra\n', 32)

    def test_points_hashes_exception_flags_errno_and_repeats_must_match(self):
        good = self.trace()
        for old, new in [(b'ff:13,-7', b'ff:14,-7'), (b'hash=01234567', b'hash=01234568'),
                         (b'after=1,0', b'after=0,0'), (b'after=1,0', b'after=1,1'),
                         (b'1234,305419896', b'1235,305419896'), (b'1234,305419896', b'1234,0')]:
            bad = good.replace(old, new, 1)
            with self.subTest(old=old), self.assertRaises(ValueError):
                compare([good, good, bad, bad], 32)
            with self.assertRaises(ValueError):
                compare([good, bad, good, good], 32)
        with self.assertRaises(ValueError):
            compare([good] * 3, 32)


if __name__ == '__main__':
    unittest.main()
