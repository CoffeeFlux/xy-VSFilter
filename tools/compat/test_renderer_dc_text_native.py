import unittest

from renderer_dc_text_native import compare, validate


class TextDcControls(unittest.TestCase):
    def trace(self):
        rows = [b'dc-text pointer_bits=32']
        call = b'dc-text-call name=select a=0 b=0 result=1 extent=0,0 before=0,0,1234,123 after=0,0,1234,123'
        for index in range(40):
            rows.append(f'dc-text-case index={index} spacing={index // 20} text={(index // 5) % 4} mode={index % 5} events=3 measurement_events=2 result=1 measured=1,2,3 selected_stock=0 selected_null=0 points=1 measure_status=0,0,1234,123 path_status=0,0,1234,123'.encode())
            rows.append(b'dc-text-path 00:1,2')
            rows.extend([call] * 3)
        rows.append(b'dc-text-complete cases=40')
        return b'\n'.join(rows) + b'\n'

    def test_rejects_missing_cases_events_and_path_payloads(self):
        good = self.trace()
        compare([good] * 4, 32)
        for bad in [good.replace(b'index=3 ', b'index=2 ', 1),
                    good.replace(b'events=3 ', b'events=4 ', 1),
                    good.replace(b'measurement_events=2', b'measurement_events=5', 1),
                    good.replace(b'points=1', b'points=2', 1),
                    good.replace(b'00:1,2', b'00:1,', 1),
                    good.replace(b'cases=40', b'cases=39')]:
            with self.assertRaises(ValueError):
                validate(bad, 32)
        with self.assertRaises(ValueError):
            validate(good, 64)

    def test_exact_paths_selection_status_and_repeat_controls(self):
        good = self.trace()
        for changed in [good.replace(b'00:1,2', b'00:1,3', 1),
                        good.replace(b'selected_stock=0', b'selected_stock=1', 1),
                        good.replace(b'path_status=0,0', b'path_status=0,1', 1),
                        good.replace(b'result=1', b'result=0', 1)]:
            with self.assertRaises(ValueError):
                compare([good, good, changed, changed], 32)
            with self.assertRaises(ValueError):
                compare([good, changed, good, good], 32)
        with self.assertRaises(ValueError):
            compare([good] * 3, 32)


if __name__ == '__main__':
    unittest.main()
