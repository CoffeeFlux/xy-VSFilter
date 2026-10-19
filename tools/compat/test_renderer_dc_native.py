import unittest

from renderer_dc_native import compare, validate


class RendererDcControls(unittest.TestCase):
    def trace(self):
        rows = [b'renderer-dc pointer_bits=64']
        for mode in range(6):
            rows.append(f'dc-case mode={mode} events=10'.encode())
            rows.extend(f'dc-phase index={i} events={count} status=0,0,1234,123'.encode()
                        for i, count in enumerate([4, 4, 4, 4, 4, 5, 9, 10]))
            for i, name in enumerate(['create', 'background', 'color', 'map', 'delete'] * 2):
                handle = 0 if mode == 1 or name == 'create' else i // 5 + 1
                value = {'background': 1, 'color': 0xffffff, 'map': 1}.get(name, 0)
                rows.append(f'dc-call name={name} handle={handle} value={value} result=0 before=0,0,1234,123 after=0,0,1234,123'.encode())
        rows.append(b'renderer-dc-complete cases=6')
        return b'\n'.join(rows) + b'\n'

    def test_requires_sharing_release_recreation_null_calls_and_order(self):
        good = self.trace()
        compare([good] * 4, 64)
        for bad in [good.replace(b'index=5 events=5', b'index=5 events=4'),
                    good.replace(b'name=background', b'name=map'),
                    good.replace(b'name=delete handle=0', b'name=delete handle=1'),
                    good.replace(b'handle=2', b'handle=1'),
                    good.replace(b'cases=6', b'cases=5'), good[:-80]]:
            with self.assertRaises(ValueError):
                validate(bad, 64)
        with self.assertRaises(ValueError):
            validate(good, 32)

    def test_requires_repeats_and_exact_status_and_results(self):
        good = self.trace()
        for changed in [good.replace(b'result=0', b'result=1', 1),
                        good.replace(b'after=0,0,1234,123', b'after=0,1,1234,123', 1),
                        good.replace(b'after=0,0,1234,123', b'after=0,0,2345,123', 1)]:
            with self.assertRaises(ValueError):
                compare([good, good, changed, changed], 64)
            with self.assertRaises(ValueError):
                compare([good, changed, good, good], 64)
        with self.assertRaises(ValueError):
            compare([good] * 3, 64)


if __name__ == '__main__':
    unittest.main()
