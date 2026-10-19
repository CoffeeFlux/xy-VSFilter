import unittest
from override_native import INPUTS, CASES, ANIMATIONS, RELATIVES, RAWFONTS, compare, validate

class OverrideTraceControls(unittest.TestCase):
    def trace(self):
        lines=[b'override-provenance pointer_bits=64 ansi=932 oem=932']
        for index in range(CASES):
            seed=index//(INPUTS+20);case=index%(INPUTS+20);group=int(case>=INPUTS);animate=seed%2 if not group else int(case-INPUTS>=10)
            lines.extend([f'override-case index={index} seed={seed} group={group} animate={animate} accepted=1 errno=1234'.encode(),b'style first',b'style second',b'state alignment=0',b'clipper present=0'])
            lines.extend(f'effect slot={slot} present=0'.encode() for slot in range(5))
        for index in range(ANIMATIONS):
            lines.append(f'animation index={index} seed={index//288} animate={index%2} errno=1234 0000000000000000 0000000000000000 0000000000000000'.encode())
        for index in range(RELATIVES):
            lines.append(f'relative index={index} accepted=1 errno=1234 0000000000000000 0000000000000000'.encode())
        for index in range(RAWFONTS):
            lines.extend([f'rawfont index={index} accepted=1 errno=1234'.encode(),b'style raw'])
        lines.append(f'override-complete cases={CASES} inputs={INPUTS} animations={ANIMATIONS} relatives={RELATIVES} rawfonts={RAWFONTS}'.encode())
        return b'\n'.join(lines)+b'\n'
    def test_complete_order_architecture_and_repeats_are_required(self):
        good=self.trace();compare([good]*4,64)
        for bad in (good.replace(b'index=1 ',b'index=0 '),good.replace(b'seed=1 ',b'seed=0 '),good.replace(b'present=0',b'present=1'),good.replace(b'override-complete',b'incomplete')):
            with self.assertRaises(ValueError):validate(bad,64)
        with self.assertRaises(ValueError):validate(good,32)
        with self.assertRaises(ValueError):compare([good]*3,64)
        with self.assertRaises(ValueError):compare([good,good,good.replace(b'errno=1234',b'errno=34'),good],64)
