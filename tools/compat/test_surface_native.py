import unittest
import surface_native as observer


def trace():
    rows=['surface pointer_bits=64']
    for index in range(observer.CASES):
        rows.append(f'surface-case index={index} kind=0 width=0 height=0 direction=0 position=0 alias=0 sticky=0 empty=0')
        for stage in range(2):
            delta='0:01' if index==0 else '-'
            rows.append(f'surface-stage index={index} stage={stage} memory_delta=0 before=0,0,1234,42 after=0,0,1234,42 delta={delta}')
    rows.append(f'surface-complete cases={observer.CASES}')
    return ('\n'.join(rows)+'\n').encode()


class SurfaceObserverTests(unittest.TestCase):
    def test_complete_repeated_lossless_deltas(self):
        observer.compare([trace()]*4,64)

    def test_noncanonical_delta_bounds_missing_rows_and_difference_are_rejected(self):
        data=trace()
        for corrupt in [data.replace(b'delta=0:01',b'delta=0:00',1),data.replace(b'delta=0:01',b'delta=4096:01',1),data.replace(b'delta=0:01',b'delta=0:01,0:02',1),data.replace(b'stage=1',b'stage=0',1)]:
            with self.assertRaises(ValueError):observer.validate(corrupt,64)
        with self.assertRaises(ValueError):observer.compare([data,data,data,data.replace(b'delta=0:01',b'delta=0:02',1)],64)


if __name__=='__main__':unittest.main()
