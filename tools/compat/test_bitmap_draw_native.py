import unittest
import bitmap_draw_native as observer


def trace():
    rows=['bitmap-draw pointer_bits=64']
    for index in range(observer.CASES):
        rows.append(f'bitmap-case index={index} width=0 layout=0 addition=0 origin=0 clip=0 color=1 flags=1 sticky=0 empty=0')
        for stage in range(6):
            present=stage<5
            row=f'bitmap-stage index={index} stage={stage} present={int(present)} alias={int(stage==4)} dirty=0,0,1,1 memory_delta=0 before=0,0,1234,42 after=0,0,1234,42'
            if present:row+=' fields=0,0,0,1,1,64 residue=0 offsets=0,-1,-1,-1 bytes=4 pixels=000000ff'
            rows.append(row)
    rows.append(f'bitmap-draw-complete cases={observer.CASES}')
    return ('\n'.join(rows)+'\n').encode()


class BitmapObserverTests(unittest.TestCase):
    def test_complete_repeated_observations(self):
        data=trace();observer.compare([data]*4,64)

    def test_missing_bytes_wrong_alignment_alias_and_difference_are_rejected(self):
        data=trace()
        for corrupt in [data.replace(b'pixels=000000ff',b'pixels=000000',1),data.replace(b'residue=0',b'residue=1',1),data.replace(b'stage=4 present=1 alias=1',b'stage=4 present=1 alias=0',1)]:
            with self.assertRaises(ValueError):observer.validate(corrupt,64)
        with self.assertRaises(ValueError):observer.compare([data,data,data,data.replace(b'pixels=000000ff',b'pixels=00000000',1)],64)


if __name__=='__main__':unittest.main()
