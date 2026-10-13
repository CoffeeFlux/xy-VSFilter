#!/usr/bin/env python3
"""Create a deterministic starter corpus; subtitle bytes are never normalized.

The time regex schedules samples only. It does NOT determine parser semantics.
The checked-in corpus is a starting point, not exhaustive coverage of ASS.
"""
import argparse
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
DEST = ROOT / "test/compat"
SEED = r"""[Script Info]
ScriptType: v4.00+
PlayResX: 640
PlayResY: 360
[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: Default,Arial,36,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,0,0,0,0,100,100,0,0,1,2,1,2,10,10,10,1
[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
Dialogue: 0,0:00:01.00,0:00:04.00,Default,,0,0,0,,{\pos(320,180)\bord2}COMPAT_TEXT
""".replace("\n", "\r\n").encode("ascii")


def variants():
    yield "plain", SEED.replace(b"COMPAT_TEXT", b"Compatibility")
    for name, content in {
        "unclosed-tag": br"{\t(0,300,\frz90)hello",
        "unclosed-paren": br"{\clip(1,2,3,4\t(\bord5)}hello",
        "nested-tags": br"{\bord1{\bord8}\bord2}hello}",
        "unknown-tags": br"{\notatag123\bord2\an99}hello",
        "duplicate-tags": br"{\bord1\bord8\bord0\alpha&HFF&\alpha&H00&}hello",
        "numeric-junk": br"{\fs12junk\fscx+00100\bord-0\blur.5}hello",
        "color-junk": br"{\1c&H12zz&\alpha&H123456789&}hello",
        "reversed-transform": br"{\t(900,100,\frz90)\move(10,10,500,100,900,100)}hello",
        "drawing": br"{\p1}m 0 0 l 60 0 60 60 0 60{\p0}hello",
        "malformed-drawing": br"{\p1}m 0 0 l 60 x b 1 2 3{\p0}hello",
        "karaoke": br"{\k0}a{\kf-10}b{\ko50}c{\kf20}d",
        "whitespace": b"  a\t b \\N\\n\\h  c  ",
        "embedded-nul": b"before\x00after",
        "utf8-invalid-tail": b"before\xc2Aafter",
        "utf8-overlong": b"before\xe0\x80\x80after",
        "utf8-surrogate": b"before\xed\xa0\x80after",
        "utf8-out-of-range": b"before\xf4\xbf\xbf\xbfafter",
        "utf8-invalid-lead": b"before\xc0\x80after",
        "utf8-truncated": b"before\xf0\x80",
    }.items():
        data = SEED.replace(b"COMPAT_TEXT", content)
        if name.startswith("utf8-"):
            yield name + "-bom", b"\xef\xbb\xbf" + data
            yield name + "-no-bom", data
        else:
            yield name, data
    plain = SEED.replace(b"COMPAT_TEXT", b"Compatibility")
    yield "utf16-le", b"\xff\xfe" + plain.decode("ascii").encode("utf-16le")
    yield "utf16-be", b"\xfe\xff" + plain.decode("ascii").encode("utf-16be")
    yield "utf16-unpaired", b"\xff\xfe" + plain.decode("ascii").replace("Compatibility", "before\ud800after").encode("utf-16le", "surrogatepass")
    yield "no-final-newline", plain.rstrip(b"\r\n")
    yield "utf8-eof-partial", b"\xef\xbb\xbf" + plain.rstrip(b"\r\n") + b"\xf0\x80"
    yield "utf16-eof-partial", b"\xff\xfe" + plain.decode("ascii").encode("utf-16le") + b"x"
    yield "cr-only", plain.replace(b"\r\n", b"\r")
    yield "lf-only", plain.replace(b"\r\n", b"\n")
    # Invalid font binaries still exercise the attachment decoder; the font
    # registration result must match the original. These are not real fonts.
    for length in range(10):
        section = b"[Fonts]\r\nfontname:compat-invalid.ttf\r\n" + b"!" * length + b"\r\n\r\n"
        yield f"font-length-{length}", plain.replace(b"[V4+ Styles]", section + b"[V4+ Styles]")
    section = "[Fonts]\r\nfontname:compat-invalid.ttf\r\n\u00ff\ud800\uffff\x00!!\r\n\r\n"
    raw_font = plain.decode("ascii").replace("[V4+ Styles]", section + "[V4+ Styles]")
    yield "font-raw-units", b"\xff\xfe" + raw_font.encode("utf-16le", "surrogatepass")
    # Move a multibyte sequence across the original 4096-byte read boundary.
    prefix, suffix = plain.split(b"Compatibility", 1)
    for offset in (4093, 4094, 4095, 4096):
        yield f"refill-{offset}", b"\xef\xbb\xbf" + prefix + b"x" * (offset - len(prefix) - 3) + b"\xf0\x9f\x98\x80" + suffix


def timestamps(data):
    matches = re.findall(rb"Dialogue:[^,]*,(\d+):(\d+):(\d+(?:\.\d+)?),(\d+):(\d+):(\d+(?:\.\d+)?)", data)
    if not matches:
        return ["0", "1", "2", "3.9999999", "4", "2"]
    first = matches[0]
    start = Decimal(first[0].decode()) * 3600 + Decimal(first[1].decode()) * 60 + Decimal(first[2].decode())
    end = Decimal(first[3].decode()) * 3600 + Decimal(first[4].decode()) * 60 + Decimal(first[5].decode())
    tick = Decimal("0.0000001")
    middle = (start + end) / 2
    return [str(t) for t in (Decimal(0), start - tick, start, start + tick, middle, end - tick, end, end + tick, middle, start)]


def make_cases(files):
    cases = []
    for path, data in files:
        for mode in ("file", "memory"):
            cases.append({"id": path.stem + "-" + mode, "script": path.relative_to(ROOT).as_posix(),
                "sha256": hashlib.sha256(data).hexdigest(), "open": mode, "width": 1280, "height": 720,
                "times": timestamps(data), "stride_padding": 16, "fill_seed": 31})
    # Same source with additional geometry/stride/background configurations.
    path = DEST / "generated/plain.ass"
    for width, height, padding, bottom_up in ((641, 359, 12, False), (640, 360, 0, True)):
        cases.append({"id": f"geometry-{width}-{height}", "script": path.relative_to(ROOT).as_posix(),
            "sha256": hashlib.sha256(dict(files)[path]).hexdigest(), "open": "memory",
            "width": width, "height": height, "stride_padding": padding, "bottom_up": bottom_up,
            "fill_seed": 173, "times": ["0", "1", "2", "4", "2"]})
    return {"schema": 1, "description": "Starter differential corpus; not exhaustive compatibility evidence.", "cases": cases}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="verify checked-in fixtures and manifest")
    args = parser.parse_args()
    files = [(path, path.read_bytes()) for path in sorted((ROOT / "test").glob("*.ass"))]
    generated = [(DEST / "generated" / (name + ".ass"), data) for name, data in variants()]
    manifest = json.dumps(make_cases(files + generated), indent=2) + "\n"
    expected = generated + [(DEST / "cases.json", manifest.encode("utf-8"))]
    for path, data in expected:
        if args.check:
            if not path.exists() or path.read_bytes() != data:
                raise SystemExit(f"Corpus is stale: {path}")
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
    print(f"{len(files)} existing scripts, {len(generated)} generated scripts, {len(make_cases(files + generated)['cases'])} cases")


if __name__ == "__main__":
    main()
