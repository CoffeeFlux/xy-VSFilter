import unittest
from overlay_native import CASES, compare, validate


class OverlayControls(unittest.TestCase):
    def trace(self):
        rows=[b'overlay-stages pointer_bits=32']
        for index in range(CASES):
            if index<12800:
                stage=index//10%4;step=index%10;name=['raster','be','gaussian','combined'][stage]
                shift=4 if step==0 else 9 if step==1 else (step-2)//2+(5 if step%2 else 0)
                empty=True;full=stage!=2
            else:name='empty';shift=index-12800;step=2;empty=False;full=True
            delta=32*(1+int(not empty)) if step>=2 else 0
            rows.append(f'stage index={index} name={name} shift={shift} offset=0,0 size=8,8 overlay=2,2,16 empty={int(empty)} full={int(full)} memory_delta={delta} before=0,0,1234,305419896 after=0,0,1234,305419896'.encode())
            for plane,present in [(0,True),(1,not empty)]:
                count=(32 if full else 4) if present else 0
                rows.append(f'stage-plane index={plane} present={int(present)} {"00"*count}'.encode())
        return b'\n'.join(rows+[b'overlay-stages-complete cases=12804'])+b'\n'

    def test_case_order_shapes_knownness_accounting_and_plane_completeness(self):
        good=self.trace();compare([good]*4,32)
        for old,new in [(b'index=1 ',b'index=0 '),(b'name=be ',b'name=gaussian '),
                        (b'shift=9 ',b'shift=4 '),(b'overlay=2,2,16',b'overlay=2,2,15'),
                        (b'memory_delta=32',b'memory_delta=31'),(b'full=0',b'full=1'),
                        (b'present=1',b'present=0'),(b'cases=12804',b'cases=12803')]:
            with self.subTest(old=old),self.assertRaises(ValueError):validate(good.replace(old,new,1),32)
        for bad in [good[:-40],good+b'extra\n']:
            with self.assertRaises(ValueError):validate(bad,32)
        with self.assertRaises(ValueError):validate(good,64)

    def test_reference_instability_pixels_offsets_and_status_are_not_parity(self):
        good=self.trace()
        for old,new in [(b'00'*32,b'00'*31+b'01'),(b'offset=0,0',b'offset=1,0'),
                        (b'after=0,0',b'after=1,0'),(b'1234,305419896',b'1234,0')]:
            bad=good.replace(old,new,1)
            with self.subTest(old=old),self.assertRaises(ValueError):compare([good,good,bad,bad],32)
            with self.assertRaises(ValueError):compare([good,bad,good,good],32)
        with self.assertRaises(ValueError):compare([good]*3,32)


if __name__=='__main__':unittest.main()
