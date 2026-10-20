import unittest
import composite_mask_native as observer

def trace():
    rows=['composite-mask pointer_bits=64']
    for index in range(observer.CASES):
        rows.append(f'composite-case index={index} shape=0 border=0 clip=0 sub=0 mask=0 color=1 flags=1 sticky=0')
        for stage in range(3):
            present=index==0 and stage<2
            row=f'composite-stage index={index} stage={stage} present={int(present)} alias={int(present and stage==1)} dirty=0,0,2,1 memory_delta=0 before=0,0,1234,42 after=0,0,1234,42'
            if present:row+=' origin=0,0 size=2,1 pitch=16 pixels=0040'
            rows.append(row)
    rows.append(f'composite-mask-complete cases={observer.CASES}')
    return ('\n'.join(rows)+'\n').encode()

class CompositeObserverTests(unittest.TestCase):
    def test_complete_repeated_observations(self):
        data=trace();observer.compare([data]*4,64)

    def test_truncated_footprint_lost_alias_and_difference_are_rejected(self):
        data=trace()
        for corrupt in [data.replace(b'pixels=0040',b'pixels=00',1),data.replace(b'stage=1 present=1 alias=1',b'stage=1 present=1 alias=0',1)]:
            with self.assertRaises(ValueError):observer.validate(corrupt,64)
        with self.assertRaises(ValueError):observer.compare([data,data,data,data.replace(b'pixels=0040',b'pixels=0041',1)],64)

if __name__=='__main__':unittest.main()
