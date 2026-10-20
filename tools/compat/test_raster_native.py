import unittest
from raster_native import CASES, compare, validate


class RasterControls(unittest.TestCase):
    def trace(self):
        rows=[b'raster pointer_bits=32'];previous=False
        for index in range(CASES):
            scenario=index//32;radius=index//4%8;shift=index%4
            live=scenario!=15;retained=bool(shift%2 and previous)
            dims='scan=8,8 offset=0,0 size=8,8 overlay=2,2,16 wide_empty=1' if live else 'scan=0,0 offset=0,0 size=0,0 overlay=0,0,0 wide_empty=0'
            delta=32*(int(live)-int(previous)+int(retained))
            rows.append(f'raster-case index={index} scenario={scenario} radius={radius} shift={shift} missing=0 result=1 origin=0,0 {dims} memory_delta={delta} before=0,0,1234,305419896 after=0,0,1234,305419896'.encode())
            for label,present in [('body',live),('border',False),('retained',retained)]:
                rows.append(f'raster-plane {label} present={int(present)} bytes={32 if present else 0} {"00"*32 if present else ""}'.encode())
            previous=live
        return b'\n'.join(rows+[b'raster-complete cases=512'])+b'\n'

    def test_complete_order_geometry_planes_aliases_and_accounting_required(self):
        good=self.trace();compare([good]*4,32)
        for old,new in [(b'index=1 ',b'index=0 '),(b'radius=1 ',b'radius=0 '),
                        (b'missing=0',b'missing=1'),(b'result=1',b'result=0'),
                        (b'overlay=2,2,16',b'overlay=2,2,15'),(b'memory_delta=32',b'memory_delta=0'),
                        (b'body present=1',b'body present=0'),(b'bytes=32',b'bytes=31'),
                        (b'retained present=1',b'retained present=0'),(b'cases=512',b'cases=511')]:
            with self.subTest(old=old),self.assertRaises(ValueError):validate(good.replace(old,new,1),32)
        for bad in [good[:-32],good+b'extra\n']:
            with self.assertRaises(ValueError):validate(bad,32)
        with self.assertRaises(ValueError):validate(good,64)

    def test_pixels_padding_alias_bytes_status_and_reference_repeat_changes_fail(self):
        good=self.trace()
        for old,new in [(b'00'*32,b'00'*31+b'01'),
                        (b'origin=0,0',b'origin=1,0'),(b'offset=0,0',b'offset=1,0'),
                        (b'after=0,0',b'after=1,0'),(b'1234,305419896',b'1234,0')]:
            bad=good.replace(old,new,1)
            with self.subTest(old=old),self.assertRaises(ValueError):compare([good,good,bad,bad],32)
            with self.assertRaises(ValueError):compare([good,bad,good,good],32)
        with self.assertRaises(ValueError):compare([good]*3,32)


if __name__=='__main__':unittest.main()
