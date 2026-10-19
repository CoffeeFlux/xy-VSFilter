import unittest
from effects_native import INPUTS, SEQUENCE, CASES, compare, validate


class EffectsTraceControls(unittest.TestCase):
    def trace(self):
        rows = [b'effects-provenance pointer_bits=64 ansi=932 oem=932']
        for seed in range(12):
            for case in range(INPUTS + SEQUENCE):
                index = seed * (INPUTS + SEQUENCE) + case
                rows.append(f'effects-case index={index} seed={seed} group={int(case >= INPUTS)} kind=0 accepted=-1 errno=1234 wrap=3 hard=-1 animated=0 input=0:'.encode())
                rows.extend(f'slot {slot} present=0'.encode() for slot in range(5))
            rows.append(f'effects-cleared seed={seed}'.encode())
            rows.extend(f'slot {slot} present=0'.encode() for slot in range(5))
        rows.append(f'effects-complete cases={CASES}'.encode())
        return b'\n'.join(rows) + b'\n'

    def test_missing_misordered_or_uninitialized_observations_refuse_parity(self):
        good = self.trace()
        compare([good] * 4, 64)
        for bad in (good.replace(b'index=1 ', b'index=0 '),
                    good.replace(b'input=0:', b'input=1:'),
                    good.replace(b'slot 0 present=0', b'slot 0 present=1 type=0' + b' 0' * 12),
                    good.replace(b'effects-cleared seed=0', b'effects-cleared seed=1')):
            with self.assertRaises(ValueError):
                validate(bad, 64)
        with self.assertRaises(ValueError):
            validate(good, 32)

    def test_fields_and_repeated_runs_must_match(self):
        good = self.trace()
        for old, new in ((b'errno=1234', b'errno=34'), (b'hard=-1', b'hard=2'),
                         (b'slot 3 present=0', b'slot 3 present=1 type=3' + b' 0' * 12)):
            changed = good.replace(old, new, 1)
            with self.assertRaises(ValueError):
                compare([good, good, changed, changed], 64)
        with self.assertRaises(ValueError):
            compare([good] * 3, 64)
