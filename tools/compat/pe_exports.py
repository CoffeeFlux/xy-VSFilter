#!/usr/bin/env python3
"""Read named and ordinal PE exports without loading/registering a Windows DLL."""
import argparse
import json
from pe_image import Image


def exports(path):
    image = Image(path)
    data, unpack, offset_for, string_at = image.data, image.unpack, image.offset_for, image.string_at
    export_rva, export_size = image.directory(0)
    result = {"machine": image.machine, "optional_magic": image.magic, "exports": []}
    if not export_rva:
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
