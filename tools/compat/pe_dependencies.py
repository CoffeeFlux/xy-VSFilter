#!/usr/bin/env python3
"""Reject added normal/delay imports or changed OS/subsystem requirements.

This checks declared dependencies, not OS compatibility of generated machine
code or dynamically loaded APIs. Windows 7 and older-CPU execution remain tests.
"""
import argparse
import json
from pe_image import Image


def dependencies(path):
    image = Image(path)
    width = 8 if image.magic == 0x20b else 4
    fmt = "<Q" if width == 8 else "<I"

    def symbols(table, rva_based=True):
        result = []
        for index in range(len(image.data) // width):
            value, = image.unpack(fmt, image.offset_for(table + index * width, width))
            if not value:
                return sorted(set(result))
            if value & (1 << (width * 8 - 1)):
                result.append("ordinal:" + str(value & 0xffff))
            else:
                if not rva_based:
                    value -= image.image_base
                result.append("name:" + image.string_at(value + 2))
        raise ValueError("unterminated import table")

    def imports(delay=False):
        rva, size = image.directory(13 if delay else 1)
        if not rva:
            return {}
        step = 32 if delay else 20
        result = {}
        for offset in range(0, size - step + 1, step):
            descriptor = image.unpack("<8I" if delay else "<5I", image.offset_for(rva + offset, step))
            if not any(descriptor):
                return {name: sorted(set(values)) for name, values in sorted(result.items())}
            rva_based = True
            if delay:
                attributes, name, _, iat, table, _, _, _ = descriptor
                if attributes & ~1:
                    raise ValueError("unknown delay import attributes")
                rva_based = bool(attributes & 1)
                if not rva_based:
                    name -= image.image_base
                    iat = iat - image.image_base if iat else 0
                    table = table - image.image_base if table else 0
            else:
                table, _, _, name, iat = descriptor
            library = image.string_at(name).lower()
            result.setdefault(library, []).extend(symbols(table or iat, rva_based))
        raise ValueError("unterminated import descriptors")

    return {"machine": image.machine,
            "os_version": list(image.unpack("<HH", image.optional + 40)),
            "subsystem_version": list(image.unpack("<HH", image.optional + 48)),
            "subsystem": image.unpack("<H", image.optional + 68)[0],
            "imports": imports(), "delay_imports": imports(True)}


def compare(reference, candidate):
    added = {}
    for kind in ("imports", "delay_imports"):
        for library, symbols in candidate[kind].items():
            additions = sorted(set(symbols) - set(reference[kind].get(library, [])))
            if additions or library not in reference[kind]:
                added[f"{kind}:{library}"] = additions
    changed = {key: [reference[key], candidate[key]] for key in
               ("machine", "os_version", "subsystem_version", "subsystem") if reference[key] != candidate[key]}
    return {"compatible_declared_requirements": not added and not changed,
            "added_imports": added, "changed_headers": changed,
            "reference": reference, "candidate": candidate}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference")
    parser.add_argument("candidate")
    args = parser.parse_args()
    report = compare(dependencies(args.reference), dependencies(args.candidate))
    print(json.dumps(report, indent=2))
    return 0 if report["compatible_declared_requirements"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
