import unittest
import clipper_native as observer

def trace():
    rows=['clipper pointer_bits=64']
    for index in range(observer.CASES):
        rows.append(f'clip-case index={index} raw=0 shape=0 size=0 effect=-1 inverse=0 scale=0 setting=0 sticky=0')
        for stage in range(3):
            row=f'clip-stage index={index} stage={stage} present={int(index==0)} alias=0 memory_delta=0 before=0,0,1234,42 after=0,0,1234,42'
            if index==0:row+=' position=-1,0 size=2,1 pitch=2 data=1 pixels=0040'
            rows.append(row)
    rows.append(f'clipper-complete cases={observer.CASES}')
    return ('\n'.join(rows)+'\n').encode()

class ClipperObserverTests(unittest.TestCase):
    def test_complete_identical_observations(self):
        data=trace();observer.compare([data]*4,64)

    def test_missing_bytes_and_candidate_difference_are_rejected(self):
        data=trace()
        with self.assertRaises(ValueError):observer.validate(data.replace(b'pixels=0040',b'pixels=00',1),64)
        with self.assertRaises(ValueError):observer.compare([data,data,data,data.replace(b'pixels=0040',b'pixels=0041',1)],64)

if __name__=='__main__':unittest.main()
