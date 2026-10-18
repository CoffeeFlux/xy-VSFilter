import unittest
from shared_color_native import VALUES, CASES, compare, validate

class ColorTraceControls(unittest.TestCase):
    def trace(self):
        rows=[b'colors-provenance pointer_bits=64 ansi=932 oem=932']
        for index in range(CASES):
            rows.append(f'colors-case index={index} value={index//8} kind={index%8} errno=1234 outcome=1 input=0: output=0:'.encode())
            if index%8>=5: rows.append(b'style observation')
        rows.append(f'colors-complete cases={CASES} values={VALUES}'.encode())
        return b'\n'.join(rows)+b'\n'
    def test_complete_repeated_observations_required(self):
        good=self.trace();compare([good]*4,64)
        for bad in (good.replace(b'kind=1',b'kind=0'),good.replace(b'input=0:',b'input=1:'),good.replace(b'style ',b'missing '),good[:good.rfind(b'colors-complete')]):
            with self.assertRaises(ValueError):validate(bad,64)
        with self.assertRaises(ValueError):validate(good,32)
        with self.assertRaises(ValueError):compare([good]*3,64)
    def test_pixels_are_not_required_to_detect_style_or_errno_changes(self):
        good=self.trace()
        for changed in (good.replace(b'style observation',b'style different'),good.replace(b'errno=1234',b'errno=34')):
            with self.assertRaises(ValueError):compare([good,good,changed,changed],64)
