import unittest
from path_data_native import NAMES, COUNTS, compare, validate

class PathControls(unittest.TestCase):
    def trace(self):
        rows=[b'path-storage pointer_bits=32']
        for i,(name,count) in enumerate(zip(NAMES,COUNTS)):
            if name in ['failed-begin','failed-end','ignored-close']:rows.append(f'path-result index={i} value={int(name=="ignored-close")}'.encode())
            if name in ['aligned','aligned-empty']:rows.append(f'path-geometry index={i} values=1,2,3,4'.encode())
            rows.append(f'path-state index={i} name={name} count={count} hash=00000001 status=0,0,1234,123'.encode())
            rows.extend([b'path-point type=6 x=13 y=-7']*count)
        return b'\n'.join(rows+[b'path-storage-complete cases=39'])+b'\n'
    def test_complete_ordered_states_counts_and_controls_required(self):
        good=self.trace();compare([good]*4,32)
        for bad in [good.replace(b'index=1 ',b'index=0 ',1),good.replace(b'count=3',b'count=0',1),good.replace(b'path-point type=6 x=13 y=-7\n',b'',1),good.replace(b'path-geometry index=18 values=1,2,3,4\n',b'',1),good.replace(b'cases=39',b'cases=38'),good.replace(b'path-result index=35 value=0',b'path-result index=35 value=1')]:
            with self.assertRaises(ValueError):validate(bad,32)
        with self.assertRaises(ValueError):validate(good,64)
    def test_points_hashes_geometry_status_and_repeats_must_match(self):
        good=self.trace()
        for bad in [good.replace(b'x=13',b'x=14',1),good.replace(b'hash=00000001',b'hash=00000002',1),good.replace(b'values=1,2,3,4',b'values=1,2,4,4',1),good.replace(b'status=0,0',b'status=1,0',1)]:
            with self.assertRaises(ValueError):compare([good,good,bad,bad],32)
            with self.assertRaises(ValueError):compare([good,bad,good,good],32)
        with self.assertRaises(ValueError):compare([good]*3,32)

if __name__=='__main__':unittest.main()
