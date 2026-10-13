# SPDX-License-Identifier: GPL-2.0-or-later
from pathlib import Path
import struct
import tempfile
import unittest

from pe_exports import exports


def pe_image(plus=False):
    data = bytearray(1024)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3c, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    optional_size = 240 if plus else 224
    struct.pack_into("<HHIIIHH", data, 0x84, 0x8664 if plus else 0x14c, 1, 0, 0, 0, optional_size, 0)
    optional = 0x98
    struct.pack_into("<H", data, optional, 0x20b if plus else 0x10b)
    struct.pack_into("<I", data, optional + 60, 512)
    directory = optional + (112 if plus else 96)
    struct.pack_into("<III", data, directory - 4, 16, 0x1000, 0x100)
    struct.pack_into("<8sIIII", data, optional + optional_size, b".rdata", 512, 0x1000, 512, 512)
    struct.pack_into("<IIHHIIIIIII", data, 512, 0, 0, 0, 0, 0, 7, 3, 1, 0x1040, 0x1050, 0x1054)
    struct.pack_into("<III", data, 576, 0x2000, 0, 0x1080)
    struct.pack_into("<I", data, 592, 0x1060)
    struct.pack_into("<H", data, 596, 0)
    data[608:613] = b"name\0"
    data[640:650] = b"other.fun\0"
    return data


class PeTests(unittest.TestCase):
    def read(self, data):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "fixture.dll"
            path.write_bytes(data)
            return exports(path)

    def test_x86_and_x64_names_ordinals_forwarders_and_gaps(self):
        for plus in (False, True):
            result = self.read(pe_image(plus))
            self.assertEqual(result["machine"], 0x8664 if plus else 0x14c)
            self.assertEqual(result["exports"], [
                {"ordinal": 7, "names": ["name"], "forwarder": None},
                {"ordinal": 9, "names": [], "forwarder": "other.fun"}])

    def test_truncated_or_non_pe_rejected(self):
        for data in (b"", b"MZ", pe_image()[:600]):
            with self.assertRaises(ValueError):
                self.read(data)

    def test_bad_ordinal_rejected(self):
        data = pe_image()
        struct.pack_into("<H", data, 596, 8)
        with self.assertRaises(ValueError):
            self.read(data)


if __name__ == "__main__":
    unittest.main()
