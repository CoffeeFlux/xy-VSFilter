#!/usr/bin/env python3
"""Read named and ordinal PE exports without loading/registering a Windows DLL."""
import argparse
import json
from pathlib import Path
import struct


def exports(path):
    data = Path(path).read_bytes()

    def unpack(fmt, offset):
        size = struct.calcsize(fmt)
        if offset < 0 or offset + size > len(data):
            raise ValueError("truncated PE structure")
        return struct.unpack_from(fmt, data, offset)

    if data[:2] != b"MZ":
        raise ValueError("not a PE image")
    pe, = unpack("<I", 0x3c)
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("missing PE signature")
    machine, section_count, _, _, _, optional_size, _ = unpack("<HHIIIHH", pe + 4)
    optional = pe + 24
    magic, = unpack("<H", optional)
    directory_offset = {0x10b: 96, 0x20b: 112}.get(magic)
    if directory_offset is None or optional_size < directory_offset + 8:
        raise ValueError("unsupported optional header")
    directory_count, = unpack("<I", optional + directory_offset - 4)
    export_rva, export_size = unpack("<II", optional + directory_offset)
    header_size, = unpack("<I", optional + 60)
    sections = []
    for index in range(section_count):
        offset = optional + optional_size + 40 * index
        _, virtual_size, virtual_address, raw_size, raw_offset = unpack("<8sIIII", offset)
        sections.append((virtual_address, virtual_size, raw_size, raw_offset))

    def offset_for(rva, length=1):
        if rva < header_size and rva + length <= min(header_size, len(data)):
            return rva
        for address, _, raw_size, raw_offset in sections:
            delta = rva - address
            if 0 <= delta and delta + length <= raw_size and raw_offset + delta + length <= len(data):
                return raw_offset + delta
        raise ValueError("RVA outside file-backed image")

    def string_at(rva):
        result = bytearray()
        # PE export names are bounded here to reject corrupt files deliberately.
        for index in range(65536):
            byte = data[offset_for(rva + index)]
            if not byte:
                return result.decode("ascii")
            result.append(byte)
        raise ValueError("unterminated PE export string")

    result = {"machine": machine, "optional_magic": magic, "exports": []}
    if not directory_count or not export_rva:
        return result
    directory = unpack("<IIHHIIIIIII", offset_for(export_rva, 40))
    _, _, _, _, _, base, function_count, name_count, functions, names, ordinals = directory
    if function_count > len(data) // 4 or name_count > len(data) // 4:
        raise ValueError("invalid PE export count")
    names_by_index = {}
    for index in range(name_count):
        name_rva, = unpack("<I", offset_for(names + 4 * index, 4))
        ordinal, = unpack("<H", offset_for(ordinals + 2 * index, 2))
        if ordinal >= function_count:
            raise ValueError("export ordinal outside table")
        names_by_index.setdefault(ordinal, []).append(string_at(name_rva))
    for index in range(function_count):
        function_rva, = unpack("<I", offset_for(functions + index * 4, 4))
        if not function_rva:
            continue
        forwarded = string_at(function_rva) if export_rva <= function_rva < export_rva + export_size else None
        result["exports"].append({"ordinal": base + index, "names": sorted(names_by_index.get(index, [])),
                                  "forwarder": forwarded})
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference")
    parser.add_argument("candidate", nargs="?")
    args = parser.parse_args()
    expected = exports(args.reference)
    if not args.candidate:
        print(json.dumps(expected, indent=2))
        return 0
    actual = exports(args.candidate)
    equal = expected == actual
    print(json.dumps({"equal": equal, "reference": expected, "candidate": actual}, indent=2))
    return 0 if equal else 1


if __name__ == "__main__":
    raise SystemExit(main())
