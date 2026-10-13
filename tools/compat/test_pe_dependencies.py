from pathlib import Path
import struct
import tempfile
import unittest

from pe_dependencies import dependencies, compare
from test_pe_exports import pe_image


def fixture(plus=False, delay_va=False):
    data = pe_image(plus) + bytearray(1024)
    optional = 0x98
    directory = optional + (112 if plus else 96)
    base = 0x180000000 if plus else 0x400000
    struct.pack_into("<Q" if plus else "<I", data, optional + (24 if plus else 28), base)
    section = optional + (240 if plus else 224)
    struct.pack_into("<I", data, section + 16, 1536)
    struct.pack_into("<HH", data, optional + 40, 6, 1)
    struct.pack_into("<HH", data, optional + 48, 6, 0)
    struct.pack_into("<H", data, optional + 68, 2)
    struct.pack_into("<II", data, directory + 8, 0x1100, 40)
    struct.pack_into("<5I", data, 768, 0x1140, 0, 0, 0x1160, 0x1140)
    struct.pack_into("<3Q" if plus else "<3I", data, 832, 0x1180, (1 << (63 if plus else 31)) | 17, 0)
    data[864:877] = b"KERNEL32.dll\0"
    data[896:904] = b"\0\0Sleep\0"
    struct.pack_into("<II", data, directory + 13 * 8, 0x1200, 64)
    bias = base if delay_va else 0
    struct.pack_into("<8I", data, 1024, 0 if delay_va else 1, 0x1260 + bias, 0,
                     0x1240 + bias, 0x1240 + bias, 0, 0, 0)
    struct.pack_into("<2Q" if plus else "<2I", data, 1088, 0x1280 + bias, 0)
    data[1120:1131] = b"USER32.dll\0"
    data[1152:1164] = b"\0\0DrawTextW\0"
    return data


class DependencyTests(unittest.TestCase):
    def read(self, data):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "fixture.dll"
            path.write_bytes(data)
            return dependencies(path)

    def test_normal_delay_and_ordinal_imports(self):
        for plus, delay_va in ((False, False), (True, False), (False, True)):
            result = self.read(fixture(plus, delay_va))
            self.assertEqual(result["imports"], {"kernel32.dll": ["name:Sleep", "ordinal:17"]})
            self.assertEqual(result["delay_imports"], {"user32.dll": ["name:DrawTextW"]})
            self.assertEqual(result["os_version"], [6, 1])
            self.assertTrue(compare(result, result)["compatible_declared_requirements"])

    def test_new_dependencies_and_raised_requirements_fail(self):
        baseline = self.read(fixture())
        candidate = self.read(fixture())
        candidate["imports"]["kernel32.dll"].append("name:WaitOnAddress")
        candidate["delay_imports"]["new.dll"] = ["name:New"]
        candidate["subsystem_version"] = [10, 0]
        result = compare(baseline, candidate)
        self.assertFalse(result["compatible_declared_requirements"])
        self.assertEqual(result["added_imports"], {"imports:kernel32.dll": ["name:WaitOnAddress"],
                                                  "delay_imports:new.dll": ["name:New"]})
        self.assertEqual(result["changed_headers"], {"subsystem_version": [[6, 0], [10, 0]]})

    def test_truncated_and_invalid_import_tables_fail(self):
        for broken in (fixture()[:1155], fixture(True)[:900]):
            with self.assertRaises(ValueError):
                self.read(broken)
        broken = fixture()
        struct.pack_into("<I", broken, 832, 0x777777)
        with self.assertRaises(ValueError):
            self.read(broken)


if __name__ == "__main__":
    unittest.main()
