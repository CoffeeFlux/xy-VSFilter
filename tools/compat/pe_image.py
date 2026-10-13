#!/usr/bin/env python3
"""Bounded file-backed PE inspection shared by compatibility checks."""
from pathlib import Path
import struct


class Image:
    def __init__(self, path):
        self.data = Path(path).read_bytes()
        if self.data[:2] != b"MZ":
            raise ValueError("not a PE image")
        pe, = self.unpack("<I", 0x3c)
        if self.data[pe:pe + 4] != b"PE\0\0":
            raise ValueError("missing PE signature")
        self.machine, count, _, _, _, size, _ = self.unpack("<HHIIIHH", pe + 4)
        self.optional = optional = pe + 24
        self.magic, = self.unpack("<H", optional)
        self.directory_offset = {0x10b: 96, 0x20b: 112}.get(self.magic)
        if self.directory_offset is None or size < self.directory_offset or optional + size > len(self.data):
            raise ValueError("unsupported/truncated optional header")
        self.directory_count, = self.unpack("<I", optional + self.directory_offset - 4)
        if self.directory_count > (size - self.directory_offset) // 8:
            raise ValueError("data directories exceed optional header")
        self.header_size, = self.unpack("<I", optional + 60)
        self.image_base, = self.unpack("<I" if self.magic == 0x10b else "<Q", optional + (28 if self.magic == 0x10b else 24))
        self.sections = []
        for index in range(count):
            offset = optional + size + 40 * index
            _, _, address, raw_size, raw_offset = self.unpack("<8sIIII", offset)
            self.sections.append((address, raw_size, raw_offset))

    def unpack(self, fmt, offset):
        size = struct.calcsize(fmt)
        if offset < 0 or offset + size > len(self.data):
            raise ValueError("truncated PE structure")
        return struct.unpack_from(fmt, self.data, offset)

    def directory(self, index):
        if index >= self.directory_count:
            return 0, 0
        return self.unpack("<II", self.optional + self.directory_offset + index * 8)

    def offset_for(self, rva, length=1):
        if 0 <= rva < self.header_size and rva + length <= min(self.header_size, len(self.data)):
            return rva
        for address, raw_size, raw_offset in self.sections:
            delta = rva - address
            if 0 <= delta and delta + length <= raw_size and raw_offset + delta + length <= len(self.data):
                return raw_offset + delta
        raise ValueError("RVA outside file-backed image")

    def string_at(self, rva):
        result = bytearray()
        for index in range(65536):
            byte = self.data[self.offset_for(rva + index)]
            if not byte:
                return result.decode("ascii")
            result.append(byte)
        raise ValueError("unterminated PE string")
