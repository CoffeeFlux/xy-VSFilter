#!/usr/bin/env python3
"""Create reproducible malformed ASS inputs for the differential renderer.

This is mutation-based exploration, not a specification of accepted ASS. Keep
seed, generator version and raw fixture hashes with every result.
"""
import argparse
import json
from pathlib import Path

from csri_compare import ROOT, digest
from make_corpus import SEED

VERSION = 1
PIECES = (
    br"\bord2", br"\xbord-1", br"\ybord4", br"\shad0", br"\xshad-2", br"\yshad3",
    br"\blur.5", br"\be2", br"\b-1", br"\i1", br"\u0", br"\s1", br"\fs24junk",
    br"\fnArial", br"\fscx-100", br"\fscy50", br"\fsp-2", br"\frz90", br"\frx10",
    br"\fry-10", br"\fr30", br"\fax.2", br"\fay-.2", br"\fe1", br"\q2", br"\r",
    br"\an7", br"\a11", br"\1c&HFFFF&", br"\2c&H123456&", br"\3c&HFF&",
    br"\4c&H00&", br"\c&H00FF00&", br"\alpha&H80&", br"\1a&H00&", br"\2a&H40&",
    br"\3a&H80&", br"\4a&HFF&", br"\pos(320,180)", br"\move(20,20,500,200,900,100)",
    br"\org(320,180)", br"\t(0,200,\bord5)", br"\t(\t(\blur2),\fs20)",
    br"\clip(20,20,400,300)", br"\iclip(1,m 0 0 l 60 0 60 60)",
    br"\fad(200,300)", br"\fade(0,128,255,0,200,700,900)", br"\k10", br"\K20",
    br"\kf30", br"\ko-10", br"\kt50", br"\p0", br"\pbo-1", br"\unknown(1,2)",
    b"\\", b"(", b")", b",", b" ", b"\t", b"\0", b"\xc2A", b"\xed\xa0\x80",
)


class Random:
    def __init__(self, seed):
        self.state = seed or 0x9e3779b9

    def next(self, limit):
        x = self.state
        x ^= (x << 13) & 0xffffffff
        x ^= x >> 17
        x ^= (x << 5) & 0xffffffff
        self.state = x
        return x % limit


def generate(seed, count):
    random = Random(seed)
    for index in range(count):
        content = bytearray(b"{" + b"".join(PIECES[random.next(len(PIECES))]
                            for _ in range(1 + random.next(24))) + b"}Mutation \\N test")
        operations = []
        for _ in range(1 + random.next(6)):
            kind = random.next(3)
            position = random.next(len(content) + 1)
            if kind == 0:
                chunk = PIECES[random.next(len(PIECES))]
                content[position:position] = chunk
                operations.append(["insert", position, chunk.hex()])
            elif kind == 1 and position < len(content):
                length = 1 + random.next(min(16, len(content) - position))
                del content[position:position + length]
                operations.append(["delete", position, length])
            elif position < len(content):
                byte = random.next(256)
                content[position] = byte
                operations.append(["replace", position, byte])
        data = SEED.replace(b"COMPAT_TEXT", content)
        encoding = random.next(4)
        if encoding == 1:
            data = b"\xef\xbb\xbf" + data
        elif encoding in (2, 3):
            codec, bom = ("utf-16le", b"\xff\xfe") if encoding == 2 else ("utf-16be", b"\xfe\xff")
            # Promote raw bytes to units, preserving NUL; insert raw surrogates
            # in some dialogue text without going through Unicode validation.
            text = data.decode("latin1")
            if index % 3 == 0:
                text = text.replace("Mutation", "Mutation\ud800\udfff")
            data = bom + text.encode(codec, "surrogatepass")
        yield data, {"index": index, "encoding_variant": encoding, "operations": operations}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seed", type=lambda text: int(text, 0), default=0x415353)
    parser.add_argument("--count", type=int, default=64)
    parser.add_argument("--output", required=True, help="new directory within this repository")
    args = parser.parse_args()
    if not 0 <= args.seed <= 0xffffffff or not 1 <= args.count <= 10000:
        parser.error("seed must be a u32; count must be between 1 and 10000")
    output = Path(args.output).resolve()
    output.relative_to(ROOT)  # transfer-friendly paths; no edits to source fixtures
    output.mkdir(parents=True, exist_ok=False)
    cases = []
    for data, provenance in generate(args.seed, args.count):
        index = provenance["index"]
        path = output / f"mutant-{index:05d}.ass"
        path.write_bytes(data)
        cases.append({"id": f"mutation-{args.seed:08x}-{index:05d}", "script": path.relative_to(ROOT).as_posix(),
            "sha256": digest(data), "open": "memory" if index % 2 else "file",
            "width": 640, "height": 360, "stride_padding": 16, "fill_seed": 31,
            "times": ["0", "1", "1.0000001", "2", "3.9999999", "4", "2"],
            "mutation": provenance})
    manifest = {"schema": 1, "generator": "mutate_corpus.py", "generator_version": VERSION,
                "seed": args.seed, "template_sha256": digest(SEED), "cases": cases}
    (output / "cases.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Created {len(cases)} raw inputs: {output / 'cases.json'}")


if __name__ == "__main__":
    main()
