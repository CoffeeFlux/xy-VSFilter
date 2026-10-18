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
        "lexer-empty-commands": br"{\\\ (\b2)\pos(320,,180,)\bord(4)}hello",
        "lexer-nested-parentheses": br"{\t(0,900,\t(100,300,\bord4)\shad2)\pos(320,(180))}hello",
        "lexer-whitespace-parameters": b"{\\  bord 2 \t\\t( , 0 , 900 , \\blur3,4 )\\pos( 320 ,\t180 )}hello",
        "lexer-command-prefixes": br"{\borders4\fscxy50\K20\kf10\alpha&&HH00&&\unknown(1,2)}hello",
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
    # Font-independent asymmetric edges make blur changes visible. Fractional
    # strengths also exercise the separable filter after render-resolution scaling.
    for name, tags in {
        "control": br"\be0\blur0",
        "be-one": br"\be1",
        "be-three": br"\be3",
        "be-fraction-low": br"\be.125",
        "be-fraction-high": br"\be.375",
        "be-mixed": br"\be.625",
        "be-mixed-repeat": br"\be1.125",
        "be-multiple": br"\be2.75",
        "gaussian-small": br"\blur.5",
        "gaussian-medium": br"\blur1.5",
        "gaussian-large": br"\blur3.25",
        "combined": br"\be1.125\blur1.5\bord2\shad1",
        "animated": br"\be0\blur0\t(0,2000,\be1.75\blur2)",
        "clipped": br"\be.375\blur1.5\clip(280,140,310,165)",
    }.items():
        drawing = (br"{\an7\pos(280.125,140.375)\bord0\shad0\p1" + tags
                   + br"}m 0 0 l 41 0 41 3 5 3 5 31 0 31{\p0}")
        yield "raster-" + name, SEED.replace(br"{\pos(320,180)\bord2}COMPAT_TEXT", drawing)
    plain = SEED.replace(b"COMPAT_TEXT", b"Compatibility")
    # Exercise CreateSegments through the actual DLL, with overlapping glyphs
    # so entry order changes affect pixels. Sample every boundary below, then
    # seek backwards. Zero-duration entries can split otherwise active ranges.
    timelines = {
        "overlap": [(1, 4, 0), (2, 5, 0), (3, 6, 0)],
        "overlap-order-reversed": [(3, 6, 0), (2, 5, 0), (1, 4, 0)],
        "duplicates-and-zero": [(1, 5, 0), (1, 5, 0), (2, 2, 0), (4, 4, 0)],
        "touching-and-gaps": [(0, 1, 0), (1, 2, 0), (3, 4, 0), (4, 6, 0)],
        "nested-layers": [(0, 6, 2), (1, 5, -1), (2, 4, 0), (3, 3, 0)],
        "reversed-and-empty": [(5, 1, 0), (1, 6, 0), (2, 2, 0), (4, 3, 0)],
        "equal-starts": [(1, 6, 0), (1, 2, 0), (1, 5, 0), (1, 4, 0)],
        "equal-ends": [(4, 6, 0), (0, 6, 0), (3, 6, 0), (1, 6, 0)],
    }
    for name, entries in timelines.items():
        script = SEED.split(b"Dialogue:", 1)[0]
        for index, (start, end, layer) in enumerate(entries):
            color = ("0000FF", "00FF00", "FF0000", "00FFFF")[index % 4]
            # Same opaque glyph at the same location exposes ordering without
            # relying on collision-placement differences.
            text = r"{\pos(320,180)\an5\bord0\shad0\1c&H" + color + r"&}MMMM"
            script += (f"Dialogue: {layer},0:00:{start:05.2f},0:00:{end:05.2f},"
                       f"Default,,0,0,0,,{text}\r\n").encode("ascii")
        yield "timeline-" + name, script
    # TryNextStr mutates delimiters and leaves an undelimited final field in
    # place. Keep raw code units and odd whitespace at the numeric/field boundary.
    for name, fields in {
        "space-prefixes": " 0, 0: 00: 01. 00, 0: 00: 04. 00, Default, actor, 0, 0, 0, ,visible",
        "tab-prefixes": "\t0,\t0:\t00:\t01.\t00,0:00:04.00,\tDefault,\tactor,\t0,0,0,,visible",
        "final-field-reuse": "0,0:00:01.00,0:00:04.00,Default,,0,0,0,visible",
        "numeric-final-reuse": "0,0:00:01.00,0:00:04.00,Default,,0",
        "empty-numeric": "0,0:00:01.00,0:00:04.00,Default,,,0,0,,visible",
        "nul-in-actor": "0,0:00:01.00,0:00:04.00,Default,actor\0tail,0,0,0,,visible",
        "raw-actor-units": "0,0:00:01.00,0:00:04.00,Default,\ud800\uffff,0,0,0,,visible",
        "unicode-style-space": "0,0:00:01.00,0:00:04.00,\u2003Default,\u00a0actor,0,0,0,,visible",
    }.items():
        text = SEED.decode("ascii").split("Dialogue:", 1)[0] + "Dialogue: " + fields + "\r\n"
        yield f"field-{name}", b"\xff\xfe" + text.encode("utf-16le", "surrogatepass")
    for name, style in {
        "final-field-reuse": "Style: Default,Arial,36",
        "empty-numeric": "Style: Default,Arial,,&H00FFFFFF",
        "unicode-prefixes": "Style: \u2003Default,\u00a0Arial,\t36,&H00FFFFFF",
        "embedded-nul": "Style: Default,Arial\0tail,36,&H00FFFFFF",
    }.items():
        text = re.sub(r"Style: [^\r\n]*", lambda _: style, plain.decode("ascii"))
        yield f"string-field-{name}", b"\xff\xfe" + text.encode("utf-16le", "surrogatepass")
    yield "drawing-state-control", SEED.replace(b"COMPAT_TEXT", b"after")
    for name, path in {
        "repeated-separators": "***m 0 0***l 60 0 60 60 0 60***",
        "leading-space": " \t m 0 0 l 60 0 60 60 0 60",
        "uppercase-commands": "M 0 0 L 60 0 m 0 0 l 60 0 60 60 0 60",
        "hex-exponent-split": "m 0 0 l 0x1p+4 0 60 60 0 60",
        "tab-after-command": "m\t0\t0 l\t60\t0\t60\t60\t0\t60",
        "nul-at-start": "\0m 0 0 l 60 0 60 60 0 60",
        "nul-after-token": "m 0 0\0l 60 0 60 60 0 60",
        "two-nuls": "m 0 0\0\0l 60 0 60 60 0 60",
        "nul-after-separator": "m 0 0*\0l 60 0 60 60 0 60",
        "nul-before-separator": "m 0 0\0*l 60 0 60 60 0 60",
        "unicode-space": "\u2003m\u00a00 0 l 60 0 60 60 0 60",
        "surrogate": "m 0 0 l 60 0\ud800 60 60 0 60",
        "noncharacter": "m 0 0 l 60 0\uffff 60 60 0 60",
    }.items():
        text = SEED.decode("ascii").replace("COMPAT_TEXT", "{\\p1}" + path + "{\\p0}after")
        yield f"drawing-lex-{name}", b"\xff\xfe" + text.encode("utf-16le", "surrogatepass")
    for name, path in {
        "empty-move": b"m",
        "missing-move": b"n 0 0 l 60 0 60 60 0 60",
        "late-move": b"n 0 0 m 10 10 l 60 10 60 60",
        "empty-move-then-n": b"m n 0 0 l 60 0 60 60 0 60",
        "bezier-one": b"m 0 0 b 60 60 l 0 60",
        "bezier-two": b"m 0 0 b 60 0 60 60 l 0 60",
        "bezier-four": b"m 0 0 b 20 60 40 60 60 0 80 20 l 0 60",
        "spline-short": b"m 0 0 s 60 0 60 60 c l 0 60",
        "spline-rollback-patch": b"m 0 0 60 0 60 60 s 30 30 junk p 0 60 c",
        "spline-close-twice": b"m 0 0 s 60 0 60 60 0 60 p 0 30 c c",
        "spline-close-after-move": b"m 0 0 s 60 0 60 60 0 60 m 80 80 c",
        "exponent": b"m 0 0 l 6e1 0 6e1 6e1 0 6e1",
        "fractional": b"m -.5 -.5 l 60.125 .5 60.75 60.25 .25 60.5",
        "command-splitting": b"junk*M 0 0*m 0 0 l 60 0*60 60 l 0 60",
    }.items():
        yield f"drawing-state-{name}", SEED.replace(b"COMPAT_TEXT", br"{\p1}" + path + br"{\p0}after")
    font = (DEST / "fonts/Abel-Regular.ttf").read_bytes()
    if hashlib.sha256(font).hexdigest() != "8809dcad25318225052f88333e208c5aad4adcb7b2c934c135735ec19aa410b4":
        raise ValueError("pinned test font has changed")
    encoded = bytearray()
    for start in range(0, len(font), 3):
        block = font[start:start + 3]
        bits = int.from_bytes(block.ljust(3, b"\0"), "big")
        encoded.extend(((bits >> shift) & 63) + 33 for shift in (18, 12, 6, 0)[:len(block) + 1])
    attachment = b"[Fonts]\r\nfontname:Abel-Regular.ttf\r\n" + b"\r\n".join(
        encoded[start:start + 80] for start in range(0, len(encoded), 80)) + b"\r\n\r\n"
    font_script = plain.replace(b",Arial,", b",Abel,")
    yield "font-valid-control", font_script
    yield "font-valid-attached", font_script.replace(b"[V4+ Styles]", attachment + b"[V4+ Styles]")
    yield "utf16-le", b"\xff\xfe" + plain.decode("ascii").encode("utf-16le")
    yield "utf16-be", b"\xfe\xff" + plain.decode("ascii").encode("utf-16be")
    yield "utf16-unpaired", b"\xff\xfe" + plain.decode("ascii").replace("Compatibility", "before\ud800after").encode("utf-16le", "surrogatepass")
    for name, text in {
        "lexer-unicode-whitespace": "{\\\u2003bord2\u2003\\pos(\u00a0320,\u3000180)}hello",
        "lexer-raw-units": "{\\\ud800\udfff(1,2)\\bord\ud8002\\pos(320,180)}hello",
        "lexer-nul-command": "{\\bord2\x00\\bord8}hello",
    }.items():
        yield name, b"\xff\xfe" + plain.decode("ascii").replace("Compatibility", text).encode("utf-16le", "surrogatepass")
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
    # ReadLine now lives in Rust. Exercise error recovery and EOF on either
    # side of its actual 4096-byte buffer (the BOM is read separately).
    for bom in (b"", b"\xef\xbb\xbf"):
        label = "bom" if bom else "no-bom"
        for name, payload in {
            "invalid-lead": b"\xc0\x80after",
            "masked-tail": b"\xf0A\r\n",  # CR/LF are consumed as tail bytes
            "out-of-range": b"\xf4\xbf\xbf\xbfafter",
            "nul": b"\xe0\x80\x80after",
        }.items():
            yield f"reader-{label}-{name}", bom + prefix + b"x" * (4095 - len(prefix)) + payload + suffix
        yield f"reader-{label}-incomplete-eof", bom + prefix + b"x" * (4095 - len(prefix)) + b"\xf0\x9f"
    for codec, bom in (("utf-16le", b"\xff\xfe"), ("utf-16be", b"\xfe\xff")):
        units = prefix.decode("ascii")
        # Separate raw surrogate units across two native reads, then preserve
        # an unpaired surrogate, embedded NUL and ordinary text after it.
        text = units + "x" * (2047 - len(units)) + "\ud83d\ude00\ud800\0after" + suffix.decode("ascii")
        yield f"reader-{codec}-surrogate-refill", bom + text.encode(codec, "surrogatepass")


def incremental_variants():
    # ASS file loading uses AddSTSEntryOnly. SubRip exercises the incremental
    # Add bridge during parsing (Open later rebuilds segments; the extracted
    # insertion oracle is what verifies intermediate timeline state).
    sequences = {
        "overlap": [(1, 4), (2, 5), (3, 6)],
        "out-of-order": [(4, 6), (0, 2), (1, 5), (2, 3)],
        "gaps-bridged": [(0, 1), (3, 4), (5, 6), (0, 6)],
        "nested": [(0, 6), (1, 5), (2, 4)],
        "touching": [(0, 1), (1, 2), (2, 4)],
        "zero-and-reversed": [(0, 6), (2, 2), (5, 1), (4, 5)],
        "duplicates": [(1, 4), (1, 4), (1, 4)],
        "empty-text": [(0, 6), (1, 4), (2, 5), (3, 6)],
    }
    for name, intervals in sequences.items():
        text = ""
        for index, (start, end) in enumerate(intervals):
            payload = "" if name == "empty-text" and index % 2 else f"<i>cue {index}</i>\r\nsecond line"
            text += (f"{index + 1}\r\n00:00:{start:02},000 --> 00:00:{end:02},000\r\n"
                     f"{payload}\r\n\r\n")
        data = b"\xff\xfe" + text.encode("utf-16le") if name == "out-of-order" else text.encode("ascii")
        yield "incremental-" + name + ".srt", data


def subrip_variants():
    for name, payload in {
        "control": "Formatting sample\r\nsecond line",
        "italic": "<i>Formatting sample</i>\r\nsecond line",
        "bold": "<b>Formatting sample</b>\r\nsecond line",
        "underline": "<u>Formatting sample</u>\r\nsecond line",
        "uppercase": "<I>Formatting sample</I>\r\nsecond line",
        "mixed": "<i><b>Formatting</b> <u>sample</u></i>\r\nsecond line",
        "malformed": "<b<i>>Formatting</b> <u>sample</i>\r\nsecond line",
        "raw-utf16": "<i>Formatting \ud800sample\udfff</i>\r\nsecond line",
    }.items():
        text = "1\r\n00:00:00,000 --> 00:00:04,000\r\n" + payload + "\r\n\r\n"
        data = (b"\xff\xfe" + text.encode("utf-16le", "surrogatepass")
                if name == "raw-utf16" else text.encode("ascii"))
        yield "subrip-format-" + name + ".srt", data


def subrip_cue_variants():
    timing = "00:00:00,000 --> 00:00:04,000\r\n"
    for name, body in {
        "control": "Caption sample\r\nsecond line\r\n",
        "number-in-caption": "Caption sample\r\n2\r\nsecond line\r\n",
        "blank-number": "Caption sample\r\n\r\n2\r\n00:00:01,000 --> 00:00:03,000\r\nNext caption\r\n",
        "sticky-blank": "Caption sample\r\n\r\nintervening line\r\n2\r\n00:00:01,000 --> 00:00:03,000\r\nNext caption\r\n",
        "number-with-tail": "Caption sample\r\n\r\n2suffix\r\nsecond line\r\n",
        "number-trailing-space": "Caption sample\r\n\r\n2  \r\n00:00:01,000 --> 00:00:03,000\r\nNext caption\r\n",
        "empty-lines": "Caption sample\r\n\r\n\r\nsecond line\r\n",
        "final-no-newline": "Caption sample",
    }.items():
        yield "subrip-cue-" + name + ".srt", ("1\r\n" + timing + body).encode("ascii")


def text_run_variants():
    for name, payload in {
        "control": br"one two three",
        "repeated-spaces": br"one  two   three",
        "hard-breaks": br"one\Ntwo\Nthree",
        "soft-wrap-zero": br"{\q0}one\ntwo\nthree",
        "soft-wrap-two": br"{\q2}one\ntwo\nthree",
        "hard-space": br"one\htwo three",
        "tab": b"one	two three",
        "karaoke": br"{\k20}one two{\k30}three four\Nfive",
    }.items():
        yield "text-runs-" + name + ".ass", SEED.replace(b"COMPAT_TEXT", payload)



def wrap_width_variants():
    body = b"one two three four five six seven eight nine ten eleven twelve"
    cases = [("style-" + str(style), ("{\\q" + str(style) + "}").encode("ascii") + body)
             for style in range(4)]
    cases += [
        ("oversized-word", br"{\q0}WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW then small words"),
        ("hard-break-tail", br"{\q3}a very long series of words\Nshort tail with more words"),
        ("negative-spacing", br"{\q3\fsp-24}one two three four five six seven"),
        ("zero-scale", br"{\q0\fscx0}one two three four five"),
    ]
    for name, payload in cases:
        yield "wrap-width-" + name + ".ass", SEED.replace(b"COMPAT_TEXT", payload)



def line_layout_variants():
    seed = SEED.replace(br"\pos(320,180)", b"").replace(b"Default,,0,0,0,,", b"Default,,17,31,23,,")
    for alignment in range(1, 10):
        payload = ("{\\an" + str(alignment) + "\\xbord7\\ybord3}wide first line\\N{\\fs24\\xbord2\\ybord8}short").encode("ascii")
        yield "line-layout-align-" + str(alignment) + ".ass", seed.replace(b"COMPAT_TEXT", payload)
    for name, payload in {
        "empty-first": br"{\an5}\Nsecond\Nthird",
        "repeated-empty": br"{\an8}first\N\N\Nlast",
        "trailing-empty": br"{\an2}first\Nsecond\N",
    }.items():
        yield "line-layout-" + name + ".ass", seed.replace(b"COMPAT_TEXT", payload)



def drawing_preparation_variants():
    for name, path in {
        "control": br"m 0 0 l 64 0 64 64 0 64",
        "adjacent": br"m0 0l64 0l64 64l0 64",
        "asterisks": br"**m 0 0***l 64 0**l 64 64*l 0 64",
        "prefix-junk": br"junk m 0 0 l 64 0 64 64 0 64",
        "embedded-command-letters": br"m 0 0 line64 0 line64 64 line0 64",
        "hex-exponent-letter": br"m 0 0 l 0x1p+6 0 64 64 0 64",
        "uppercase": br"m 0 0 L64 0l64 64l0 64",
        "tab": b"m\t0 0\tl\t64 0 64 64 0 64",
    }.items():
        payload = br"{\p1}" + path + br"{\p0}after"
        yield "drawing-prepare-" + name + ".ass", SEED.replace(b"COMPAT_TEXT", payload)



def drawing_storage_variants():
    square = br"m 0 0 l 80 0 80 50 0 50"
    for name, payload in {
        "control": br"{\p1}" + square + br"{\p0}after",
        "fractional-scale": br"{\p1\fscx123.25\fscy77.5\pbo-3}" + square + br"{\p0}after",
        "drawing-scale-two": br"{\p2\pbo4}" + square + br"{\p0}after",
        "spline-copy": br"{\p2\fscx150}m 0 0 s 0 100 100 100 100 0 c{\p0}after",
        "vector-clip": br"{\clip(1,m 200 100 l 440 100 440 260 200 260)}Clipped sample",
        "inverse-clip": br"{\iclip(2,m 500 250 l 780 250 780 430 500 430)}Clipped sample",
    }.items():
        yield "drawing-storage-" + name + ".ass", SEED.replace(b"COMPAT_TEXT", payload)



def path_alignment_variants():
    for name, x in (("below-negative", "-1.015625"), ("negative-boundary", "-1"),
                    ("above-negative", "-0.984375"), ("positive-fraction", "0.015625")):
        path = f"m {x} -1.015625 l 64 -1.015625 64 64 {x} 64".encode("ascii")
        yield "path-align-" + name + ".ass", SEED.replace(b"COMPAT_TEXT", br"{\p1}" + path + br"{\p0}after")



def span_merge_variants():
    for name, payload in {
        "control": br"{\xbord0\ybord0}W I O  gap",
        "horizontal-border": br"{\xbord8\ybord0}W I O  gap",
        "small-border": br"{\xbord1.5\ybord0}W I O  gap",
        "drawing-holes": br"{\xbord6\ybord0\p1}m 0 0 l 80 0 80 60 0 60 m 20 20 l 20 40 60 40 60 20{\p0}",
        "italic-karaoke": br"{\xbord7\ybord0\i1\k30}wide {\k40}words",
        "scaled": br"{\xbord9\ybord0\fscx75\fscy150}Horizontal outline",
    }.items():
        yield "span-merge-" + name + ".ass", SEED.replace(b"COMPAT_TEXT", payload)


def arc_queue_variants():
    for name, payload in {
        "control": br"{\xbord0\ybord0}O W I loops",
        "round": br"{\xbord5\ybord5}O W I loops",
        "tall": br"{\xbord1\ybord9}O W I loops",
        "wide": br"{\xbord9\ybord1}O W I loops",
        "holes": br"{\xbord4\ybord6\p1}m 0 0 l 80 0 80 60 0 60 m 20 20 l 20 40 60 40 60 20{\p0}",
        "spline": br"{\xbord2.5\ybord3.75\p1}m 0 0 s 0 80 80 80 80 0 c{\p0}",
        "rotated": br"{\xbord5\ybord7\frz23\i1}Outline curves",
        "animated": br"{\xbord1\ybord1\t(0,2000,\xbord9\ybord6)}Changing outline",
    }.items():
        yield "arc-queue-" + name + ".ass", SEED.replace(b"COMPAT_TEXT", payload)


def clip_fade_variants():
    for name, effect in {
        "banner-control": b"Banner;3;0;0",
        "banner-edge": b"Banner;3;0;128",
        "banner-overlap": b"Banner;3;0;800",
        "banner-reverse": b"Banner;3;1;128",
        "scroll-control": b"Scroll up;40;320;4;0",
        "scroll-up": b"Scroll up;40;320;4;80",
        "scroll-down": b"Scroll down;40;320;4;80",
        "scroll-overlap": b"Scroll up;-40;280;4;400",
    }.items():
        data = SEED.replace(b",0,0,0,,", b",0,0,0," + effect + b",")
        data = data.replace(br"{\pos(320,180)\bord2}COMPAT_TEXT", br"{\bord0\shad0}Fade mask comparison WWW 0123456789 WWW")
        yield "clip-fade-" + name + ".ass", data


def clip_mask_variants():
    shape = br"{\an7\pos(260,140)\bord0\shad0\p1}m 0 0 l 100 0 100 60 0 60{\p0}"
    for name, tag in {
        "normal": br"{\clip(m 280 140 l 330 140 330 190 280 190)}",
        "inverse": br"{\iclip(m 280 140 l 330 140 330 190 280 190)}",
        "offscreen": br"{\clip(m -30 -20 l 310 -20 310 170 -30 170)}",
        "shifted": br"{\clip(m 280.125 140.875 l 330.625 140.875 330.625 190.25 280.125 190.25)}",
        "scaled": br"{\clip(2,m 560 280 l 660 280 660 380 560 380)}",
        "empty": br"{\clip(m 0 0 l 0 0)}",
    }.items():
        yield "clip-mask-" + name + ".ass", SEED.replace(b"COMPAT_TEXT", tag + shape)


def gauss_planes_variants():
    for name, payload in {
        "body-light": br"{\bord0\shad0\be0\blur0.5}Gaussian planes",
        "body-wide": br"{\bord0\shad0\be0\blur2.5}Gaussian planes",
        "border-light": br"{\bord2\shad0\be0\blur0.5}Gaussian planes",
        "border-wide": br"{\bord2\shad0\be0\blur4}Gaussian planes",
    }.items():
        yield "gauss-planes-" + name + ".ass", SEED.replace(b"COMPAT_TEXT", payload)


def blur_planes_variants():
    for name, payload in {
        "control": br"{\bord0\shad0\be0}Holes and stems",
        "body": br"{\bord0\shad0\be3}Holes and stems",
        "border": br"{\bord2\shad0\be3}Holes and stems",
        "fractional": br"{\bord2\shad0\be1.25}Holes and stems",
    }.items():
        yield "blur-planes-" + name + ".ass", SEED.replace(b"COMPAT_TEXT", payload)


def variance_variants():
    for name, payload in {
        "control": br"{\an7\pos(280,140)\bord2.5\shad0}Shifted outline",
        "diagonal": br"{\an7\move(280,140,280.875,140.875,0,3000)\bord2.5\shad0}Shifted outline",
        "reverse": br"{\an7\move(280.875,140.875,280,140,0,3000)\bord2.5\shad0}Shifted outline",
        "drawing": br"{\an7\move(280,140.875,280.875,140,0,3000)\bord0\shad0\p1}m 0 0 l 63.875 0 63.875 47.125 0 47.125{\p0}",
    }.items():
        yield "variance-" + name + ".ass", SEED.replace(br"{\pos(320,180)\bord2}COMPAT_TEXT", payload)


def span_coverage_variants():
    for phase in range(8):
        tag = f"{{\\an7\\pos({280 + phase / 8},{140 + (7 - phase) / 8})\\bord2.5\\shad0\\p1}}".encode("ascii")
        path = b"m 0 0 l 63.875 0 63.875 47.125 0 47.125 m 12.125 12 l 12.125 35 52 35 52 12"
        payload = tag + path + br"{\p0} Phase"
        yield f"span-coverage-phase-{phase}.ass", SEED.replace(br"{\pos(320,180)\bord2}COMPAT_TEXT", payload)


def scan_row_variants():
    for name, path in {
        "single": b"m 0 0 l 80 0 80 60 0 60",
        "double-same": b"m 0 0 l 80 0 80 60 0 60 m 0 0 l 80 0 80 60 0 60",
        "opposite": b"m 0 0 l 80 0 80 60 0 60 m 0 0 l 0 60 80 60 80 0",
        "hole": b"m 0 0 l 80 0 80 60 0 60 m 20 20 l 20 40 60 40 60 20",
        "touching": b"m 0 0 l 40 0 40 60 0 60 m 40 0 l 80 0 80 60 40 60",
        "crossing": b"m 0 0 l 80 60 0 60 80 0",
    }.items():
        payload = br"{\bord0\shad0\p1}" + path + br"{\p0} after"
        yield "scan-row-" + name + ".ass", SEED.replace(b"COMPAT_TEXT", payload)


def ellipse_arc_variants():
    for name, payload in {
        "fraction-below": br"{\xbord2.124\ybord2.124}Rounded corners",
        "fraction-above": br"{\xbord2.126\ybord2.126}Rounded corners",
        "thin-wide": br"{\xbord14\ybord0.5\p1}m 0 0 l 64 0 64 48 0 48{\p0}",
        "thin-tall": br"{\xbord0.5\ybord14\p1}m 0 0 l 64 0 64 48 0 48{\p0}",
    }.items():
        yield "ellipse-arc-" + name + ".ass", SEED.replace(b"COMPAT_TEXT", payload)


def ellipse_storage_variants():
    for name, payload in {
        "lookup-edge": br"{\xbord8\ybord8}O W I table",
        "beyond-lookup": br"{\xbord8.125\ybord8.125}O W I table",
        "narrow-tall": br"{\xbord1\ybord20}O W I table",
        "wide-short": br"{\xbord20\ybord1}O W I table",
    }.items():
        yield "ellipse-storage-" + name + ".ass", SEED.replace(b"COMPAT_TEXT", payload)


def path_buffer_variants():
    for name, payload in {
        "control": br"{\fsp0}AV fi O W",
        "spaced": br"{\fsp3}AV fi O W",
        "negative": br"{\fsp-2}AV fi O W",
        "fractional": br"{\fsp0.125}AV fi O W",
    }.items():
        yield "path-buffer-" + name + ".ass", SEED.replace(b"COMPAT_TEXT", payload)
    for name, text in (("combining", "A\u0301 fi O W"), ("surrogate-pair", "A\U0001d11eV fi O W")):
        payload = br"{\fsp1.5}" + text.encode("utf-8")
        yield "path-buffer-" + name + ".ass", b"\xef\xbb\xbf" + SEED.replace(b"COMPAT_TEXT", payload)


def layout_rect_variants():
    prefix = SEED[:SEED.index(b"Dialogue:")]
    for name, alignment, second_layer, border, collisions, multiline in (
        ("bottom", 2, 0, 2, "Normal", False),
        ("split-layers", 2, 1, 2, "Normal", False),
        ("top", 8, 0, 2, "Normal", False),
        ("middle", 5, 0, 2, "Normal", False),
        ("reverse", 2, 0, 2, "Reverse", False),
        ("borders", 2, 0, 8, "Normal", False),
        ("multiline", 2, 0, 2, "Normal", True),
        ("top-borders", 8, 0, 8, "Normal", True),
    ):
        header = prefix.replace(b"[V4+ Styles]", ("Collisions: " + collisions + "\r\n[V4+ Styles]").encode("ascii"))
        tags = "{\\an" + str(alignment) + "\\bord" + str(border) + "}"
        text = ""
        for index, (start, end, layer) in enumerate(((0, 4, 0), (1, 3, second_layer), (2, 5, 0))):
            body = tags + "Collision " + chr(65 + index)
            if multiline:
                body += "\\Nsecond line"
            text += f"Dialogue: {layer},0:00:0{start}.00,0:00:0{end}.00,Default,,0,0,0,,{body}\r\n"
        yield "layout-rect-" + name + ".ass", header + text.encode("ascii")


def legacy_pipe_variants():
    for kind, header, suffix in (("old-subrip", "{0:0:0}{0:0:4}", ".sub"),
                                 ("vplayer", "0:0:0:", ".txt")):
        for name, payload in {
            "control": "Formatting sample second line",
            "pipes": "Formatting sample|second line",
            "repeated-pipes": "Formatting sample||second line|",
            "unicode": "Formatting \u03b1sample|second line",
        }.items():
            text = header + payload + "\r\n"
            data = b"\xff\xfe" + text.encode("utf-16le") if name == "unicode" else text.encode("ascii")
            yield kind + "-" + name + suffix, data


def realtext_variants():
    for name, payload in {
        "control": "Formatting sample<br/>second line",
        "italic": "<i>Formatting sample</i><br/>second line",
        "uppercase": "<I>Formatting sample</I><br/>second line",
        "font-bounds": '<font size="13">small</font><font size="20">normal</font><font size="26">large</font>',
        "formatting-only": "<i></i><b></b><br/>",
        "clear-carry": '<i>first<time begin="2" end="4"/><clear/>second</i>',
    }.items():
        text = '<window><time begin="0" end="4"/>' + payload + '</window>\r\n'
        yield "realtext-format-" + name + ".rt", text.encode("ascii")


def ass_header_variants():
    for name, header in (
        ("scaled-substring", b"ScaledBorderAndShadow: notyes\r\n"),
        ("scaled-control", b"ScaledBorderAndShadow: no\r\n"),
        ("collisions-substring", b"Collisions: unreversed\r\n"),
        ("collisions-control", b"Collisions: normal\r\n"),
        ("matrix-prefix-retained", b"YCbCr Matrix: pc.709junk\r\nYCbCr Matrix: unknown\r\n"),
    ):
        yield "ass-header-" + name + ".ass", SEED.replace(b"[V4+ Styles]", header + b"[V4+ Styles]").replace(b"COMPAT_TEXT", b"Header sample")
    yield "ass-header-playres-x-only.ass", SEED.replace(b"PlayResX: 640", b"PlayResX: 1280").replace(b"PlayResY: 360\r\n", b"").replace(b"COMPAT_TEXT", b"Header sample")
    yield "ass-header-playres-y-only.ass", SEED.replace(b"PlayResY: 360", b"PlayResY: 1024").replace(b"PlayResX: 640\r\n", b"").replace(b"COMPAT_TEXT", b"Header sample")
    legacy = (b"[Script Info]\r\nScriptType: v4.00+\r\nPlayResX: 640\r\nPlayResY: 360\r\n"
              b"[V4 Styles]\r\nScriptType: unknown\r\n"
              b"Style: Default,Arial,36,16777215,255,0,0,0,0,1,2,1,2,10,10,10,0,1\r\n"
              b"[Events]\r\nDialogue: 7,0:00:01.00,0:00:04.00,Default,,0,0,0,,Header sample\r\n")
    yield "ass-header-unknown-script-preserves-style.ass", legacy


def ass_style_variants():
    for name, version, alpha, alignment, border, size in (
        ("v3", 3, 0, 2, 1, "36"),
        ("v4-alpha-low", 4, -1, 2, 1, "36"),
        ("v4-alpha-high", 4, 512, 2, 1, "36"),
        ("v4-alignment", 4, 0, 15, 1, "36"),
        ("v5-outline", 5, 0, 2, 1, "36"),
        ("v5-box", 5, 0, 2, 3, "36"),
        ("v6-bottom", 6, 0, 2, 1, "36"),
        ("invalid-float", 5, 0, 2, 1, "junk"),
    ):
        fields = ["Default", "Arial", size, "&H00ffffff", "&H000000ff", "0", "&H80000000", "0", "0"]
        if version >= 5:
            fields += ["0", "0", "100", "100", "0", "0"]
        if version >= 4:
            fields += [str(border)]
        fields += ["2", "1", str(alignment), "10", "10", "10"]
        if version >= 6:
            fields += ["30"]
        if version <= 4:
            fields += [str(alpha)]
        fields += ["1"]
        if version >= 6:
            fields += ["2"]
        header = "[Script Info]\r\nPlayResX: 640\r\nPlayResY: 360\r\n"
        if version >= 4:
            header += "ScriptType: v4.00" + "+" * (version - 4) + "\r\n"
            header += "[V4" + "+" * (version - 4) + " Styles]\r\n"
        dialogue = ("Marked=0" if version <= 4 else "0") + ",0:00:01.00,0:00:04.00,Default,,0,0,0,"
        if version >= 6:
            dialogue += "30,"
        dialogue += ",Style sample"
        script = header + "Style: " + ",".join(fields) + "\r\n[Events]\r\nDialogue: " + dialogue + "\r\n"
        yield "ass-style-" + name + ".ass", script.encode("ascii")


def ass_dialogue_variants():
    header = SEED.split(b"Dialogue:", 1)[0]
    normal = b"0,0:00:01.00,0:00:04.00,Default,,0,0,0,,visible"
    for name, script_header, fields in (
        ("v3", header.replace(b"ScriptType: v4.00+\r\n", b""), b"Marked=" + normal),
        ("v4", header.replace(b"ScriptType: v4.00+", b"ScriptType: v4.00"), b"Marked=" + normal),
        ("v6", header.replace(b"ScriptType: v4.00+", b"ScriptType: v4.00++"), normal.replace(b"0,0,0,,visible", b"0,0,0,30,,visible")),
        ("missing-tail-fields", header, b"0,0:00:01.00,0:00:04.00,Default,,0,0,0"),
        ("empty-text", header, normal.replace(b"visible", b"")),
        ("hex-numbers", header, b"&H0,&H0:&H0:&H1.&H0,&H0:&H0:&H4.&H0,Default,,0x0,&h0,0,,visible"),
        ("invalid-time", header, normal.replace(b"0:00:01.00", b"0:xx:01.00")),
        ("style-effect-prefix", header, normal.replace(b"Default", b"***dEfAuLt").replace(b",,visible", b",shared,shared text")),
    ):
        yield "ass-dialogue-" + name + ".ass", script_header + b"Dialogue: " + fields + b"\r\n"


def encoding_variants():
    # Deliberately omit a Unicode BOM. Preserve raw legacy bytes and select the
    # Windows charset through the style and inline overrides.
    for name, charset, payload in (
        ("western", 0, b"caf\xe9"),
        ("western-control", 0, b"cafe"),
        ("japanese", 128, b"\x82\xa0\x82\xa2 legacy"),
        ("simplified-chinese", 134, b"\xd6\xd0\xce\xc4 legacy"),
        ("traditional-chinese", 136, b"\xa4\xa4\xa4\xe5 legacy"),
        ("korean", 129, b"\xb0\xa1\xb3\xaa legacy"),
        ("johab", 130, b"\x88\x61 legacy"),
        ("switch-and-trailing", 0, b"caf\xe9{\\fe128}\x82\xa0{\\fe}caf\xe9{\\fe128}\x81"),
    ):
        script = SEED.replace(b"10,10,10,1\r\n", f"10,10,10,{charset}\r\n".encode("ascii"))
        yield "encoding-" + name + ".ass", script.replace(b"COMPAT_TEXT", payload)


def microdvd_style_variants():
    for name, tags in (
        ("control", "{f:Arial}{s:24}{p:1}"),
        ("italic", "{f:Arial}{s:24}{p:1}{y:i}"),
        ("color", "{f:Arial}{s:24}{c:$00ff00}"),
        ("repeated-flags", "{f:Arial}{s:24}{Y:BIUS}{y:}"),
        ("unclosed-font", "{s:24}{f:Arial"),
        ("nested-ignored", "{unknown{y:i}}{f:Arial}{s:24}"),
    ):
        data = "{Named}" + tags + "\r\n{0}{100}Style sample\r\n"
        yield "microdvd-style-" + name + ".sub", data.encode("ascii")


def microdvd_variants():
    for name, payload in {
        "control": "Formatting sample|second line",
        "italic": "{y:i}Formatting sample|second line",
        "global-italic": "{Y:i}Formatting sample|second line",
        "charset": r"{h:128}Formatting sample|{h:0}second line",
        "embedded-charset": r"{\fe128}Formatting sample|{\fe}second line",
        "nested": r"{{\fe134}Formatting} sample|{unknown}second line",
        "empty-lines": "|Formatting||sample|",
        "mixed": "{c:$123456}{f:Arial}{s:24}{y:bius}Formatting|sample",
    }.items():
        yield "microdvd-format-" + name + ".sub", ("{0}{100}" + payload + "\r\n").encode("ascii")


def mpl2_variants():
    for name, payload in {
        "control": "Formatting sample|second line",
        "italic": "/Formatting sample|/second line",
        "interior-slash": "Formatting /sample|second /line",
        "empty-lines": "/|/Formatting||/sample|/",
        "mixed": "/{Y:b}Formatting|/{f:Arial}sample",
        "unicode": "/Formatting \u03b1sample|/second line",
    }.items():
        text = "[0][40]" + payload + "\r\n"
        data = b"\xff\xfe" + text.encode("utf-16le") if name == "unicode" else text.encode("ascii")
        yield "mpl2-format-" + name + ".mpl", data


def sami_variants():
    for name, payload in {
        "control": "Formatting sample<BR>second line",
        "italic": "<i>Formatting sample</i><br>second line",
        "uppercase": "<I>Formatting sample</I><BR>second line",
        "entities": "Formatting&NBSP;sample&QuOt;text&QUOT;<bR>second line",
        "mixed": "<B><I>Formatting</i> sample</b><Br>second line",
        "unicode": "<I>Formatting \u03b1sample</I><BR>second line",
        "font-black": "<font color=black>Formatting sample</font><BR>second line",
        "font-zero": "<font color=000000>Formatting sample</font><BR>second line",
        "font-multiple": '<font color="red" color="#123456">Formatting sample</font><BR>second line',
        "font-unclosed": "Formatting <font color=red",
    }.items():
        text = ("<SAMI>\r\n<BODY>\r\n<SYNC Start=0><P>" + payload +
                "\r\n<SYNC Start=4000><P>&nbsp;\r\n</BODY>\r\n</SAMI>\r\n")
        data = b"\xff\xfe" + text.encode("utf-16le") if name == "unicode" else text.encode("ascii")
        yield "sami-format-" + name + ".smi", data


def sami_file_variants():
    for name, body in {
        "control": "<SYNC START=0>Caption sample\r\n<SYNC START=4000>Tail",
        "title-skips-sync": "<TITLE>title</TITLE><SYNC START=0>Skipped\r\n<SYNC START=1000>Caption sample\r\n<SYNC START=4000>Tail",
        "comment-spans-sync": "<!--\r\n<SYNC START=0>Skipped\r\n-->\r\n<SYNC START=1000>Caption sample\r\n<SYNC START=4000>Tail",
        "prefix-timestamp": "0000000000001<SYNC START=20>Caption sample\r\n<SYNC START=4000>Tail",
        "timestamp-letters": "<SYNC START=1a0b0c0ms>Caption sample\r\n<SYNC START=4000>Tail",
        "same-line-comment": "<SYNC START=0>Skipped<!--closed-->\r\n<SYNC START=1000>Caption sample\r\n<SYNC START=4000>Tail",
        "blank-lines": " \t\r\n<SYNC START=0>Caption sample\r\n\r\n<SYNC START=4000>Tail",
        "final-unclosed-caption": "<SYNC START=0>Caption sample",
    }.items():
        yield "sami-file-" + name + ".smi", ("<sami>\r\n" + body + "\r\n").encode("ascii")


def document_layout_variants():
    # Ordinary finite ASS header inputs exercise RenderEx's all-or-nothing
    # document layout pair selection against the 1280x720 corpus video size.
    # No distinct-pixel assertion: both sides are measured by the renderer gate.
    headers = {
        "control": b"",
        "positive-pair": b"LayoutResX: 960\r\nLayoutResY: 480\r\n",
        "width-only": b"LayoutResX: 960\r\n",
        "height-only": b"LayoutResY: 480\r\n",
        "zero-width": b"LayoutResX: 0\r\nLayoutResY: 480\r\n",
        "negative-height": b"LayoutResX: 960\r\nLayoutResY: -1\r\n",
        "repeated-final-pair": b"LayoutResX: 640\r\nLayoutResY: 360\r\nLayoutResX: 960\r\nLayoutResY: 480\r\n",
        "repeated-final-fallback": b"LayoutResX: 960\r\nLayoutResY: 480\r\nLayoutResX: 0\r\n",
    }
    for name, header in headers.items():
        data = SEED.replace(b"[V4+ Styles]", header + b"[V4+ Styles]")
        yield "document-layout-" + name + ".ass", data.replace(b"COMPAT_TEXT", b"Layout pair sample")


def xombie_variants():
    style = "Primary,Caption,Arial,36,16777215,255,0,0,0,0,0,128,0,0,0,0,0,100,100,0,0,0,0,1,2,1,2,10,10,10,1"
    header = "Version=1.0\r\nScreenHorizontal=640\r\nScreenVertical=360\r\n"
    line = "Line=D,id,0,0:00:01.234,0:00:04.345,Primary,Caption,Actor,0,0,0,Xombie compatibility\r\n"
    def script(fields=style, body=line):
        return (header + "Style=" + fields + "\r\n" + body).encode("ascii")
    yield "xombie-control.xss", script()
    for name, field, value in (("italic",13,"1"),("blur",16,"1"),("angles",22,"12"),("scale",17,"75")):
        fields=style.split(",");fields[field]=value
        yield "xombie-"+name+".xss",script(",".join(fields))
    yield "xombie-final-fragment.xss",script(body=line+line.replace("id,0,","id2,1,").rstrip("\r\n"))
    yield "xombie-reversed-lines.xss",script(body=line.replace("01.234","02.234").replace("id,0,","id2,1,")+line)
    text=script().decode("ascii").replace("Xombie compatibility","before\ud800\x00after")
    yield "xombie-raw-units.xss",b"\xff\xfe"+text.encode("utf-16le","surrogatepass")
    font=(DEST/"fonts/Abel-Regular.ttf").read_bytes()
    if hashlib.sha256(font).hexdigest()!="8809dcad25318225052f88333e208c5aad4adcb7b2c934c135735ec19aa410b4":raise ValueError("pinned test font has changed")
    encoded=bytearray()
    for start in range(0,len(font),3):
        block=font[start:start+3];bits=int.from_bytes(block.ljust(3,b"\0"),"big")
        encoded.extend(((bits>>shift)&63)+33 for shift in (18,12,6,0)[:len(block)+1])
    attached=b"fontname=Abel-Regular.ttf\r\n"+b"\r\n".join(encoded[i:i+80] for i in range(0,len(encoded),80))+b"\r\n\r\n"
    control=script(style.replace(",Arial,",",Abel,"))
    yield "xombie-font-control.xss",control
    yield "xombie-font-attached.xss",attached+control


def text_parser_variants():
    timing="1\r\n0:0:1,234 --> 0:0:4,567\r\n"
    for name,text in {
        "srt-control":timing+"Visiblediscarded\r\nsecond\r\n",
        "srt-nul":timing+"Visible\x00discarded\r\nsecond\r\n",
        "srt-separators":timing.replace("0:0:1,234 --> 0:0:4,567","0x0x1.234 --> 0x0x4.567")+"Visible\r\nsecond\r\n",
        "srt-numeric-eof":timing+"Visible\r\n\r\n2",
        "srt-final-blank":timing+"Visible\r\n \t",
        "mpl-control":"[12][45]Visible\r\n",
        "mpl-missing-bracket":"[12][45Visible\r\n",
        "mpl-final-fragment":"[12][45]Visible\r\n[20][30]ignored",
        "mpl-raw-units":"[12][45]Visible\x00\ud800tail\r\n",
        "mpl-spaces":" \t[12][45]  Visible \t\r\n",
    }.items():
        raw="\x00" in text
        data=b"\xff\xfe"+text.encode("utf-16le","surrogatepass") if raw else text.encode("ascii")
        yield "text-parser-"+name+(".srt" if name.startswith("srt-") else ".mpl"),data


def legacy_parser_variants():
    for name, text in {
        "old-control": "{0:0:1}{0:0:4}Visible\r\n",
        "old-missing-brace": "{0:0:1}{0:0:4Visible\r\n",
        "old-pipe-header": "{0:0:1|}{0:0:4}Visible|second\r\n",
        "old-final-fragment": "{0:0:1}{0:0:4}Visible\r\n{0:0:2}{0:0:3}ignored",
        "vp-control": "0:0:1:Visible\r\n",
        "vp-caption-space": "0:0:1: Visible\r\n",
        "vp-missing-colon": "0:0:1Visible\r\n",
        "vp-final-fragment": "0:0:1:Visible\r\n0:0:2:ignored",
        "vp-raw-units": "0:0:1:Visible\x00\ud800tail\r\n",
        "vp-dbcs": "0:0:1:{\\fe128}A\x81|B|C\r\n",
    }.items():
        data = (b"\xff\xfe" + text.encode("utf-16le", "surrogatepass")
                if "\x00" in text else text.encode("latin1"))
        yield "legacy-parser-"+name+(".sub" if name.startswith("old-") else ".txt"), data


def microdvd_parser_variants():
    for name, text in {
        "control": "{0}{100}First caption\r\n{50}{150}Second caption\r\n",
        "style-sticky": "{Default}\r\n{0}{100}First caption\r\n{50}{150}Second caption\r\n",
        "brace-control": "{0}{100}Visible\r\n",
        "missing-brace": "{0}{100Visible\r\n",
        "provisional": "{0}{}First caption\r\n{25}{}Second caption\r\n{50}{100}Third caption\r\n",
        "empty-provisional": "{0}{}\r\n{25}{100}Second caption\r\n{50}{150}Third caption\r\n",
        "style-switch": "{First}{y:b}\r\n{0}{}First caption\r\n{sEcOnD}{y:i}{c:$102030}\r\n{25}{100}Second caption\r\n{50}{150}Third caption\r\n",
        "final-fragment": "{0}{}First caption\r\n{25}{100}ignored",
        "raw-units": "{0}{100}Visible\x00\ud800tail\r\n",
        "dbcs": "{Style}{h:128}\r\n{0}{}A\x81|B|C\r\n{50}{100}Tail\r\n",
    }.items():
        data = (b"\xff\xfe"+text.encode("utf-16le", "surrogatepass")
                if "\x00" in text else text.encode("latin1"))
        yield "microdvd-parser-"+name+".sub", data


def sami_parser_variants():
    first="<SYNC START=1000>First caption\r\n"
    end="<SYNC START=14000>&nbsp;\r\n"
    for name, text in {
        "control":"<sami>\r\n"+first+end,
        "title-skip":"<sami>\r\n<TITLE>hidden</TITLE>"+first+end,
        "fixed-column":"<sami>\r\n0000000000001"+first+end,
        "overflow":"<sami>\r\n"+first.replace("1000","4294968296")+end,
        "letters":"<sami>\r\n"+first.replace("1000","1a0b0c0ms")+end,
        "comment-close":"<sami>\r\n"+first+"<!--\r\n<SYNC START=2000>hidden-->\r\n<SYNC START=4000>Last caption\r\n"+end,
        "final-fragment":"<sami>\r\n"+first+"<SYNC START=2000>ignored",
        "raw-units":"<sami>\r\n<SYNC START=1000>First\x00\ud800discarded\r\n"+end,
        "dbcs":"<sami>\r\n<SYNC START=1000>{\\fe128}A\x81|B<br>C\r\n"+end,
        "inline-marker":"<sami>"+first+end,
    }.items():
        data=(b"\xff\xfe"+text.encode("utf-16le","surrogatepass") if "\x00" in text else text.encode("latin1"))
        yield "sami-parser-"+name+".smi", data



def vtt_parser_variants():
    cue="00:01.000 --> 00:03.000\r\n<c.red>First caption</c>\r\n\r\n"
    tail="00:03.000 --> 00:05.000\r\nLast caption\r\n\r\n"
    for name,text in {
        "control":"WEBVTT\r\n"+cue+tail,
        "color-replace":"WEBVTT\r\nSTYLE\r\n::cue(.red) {color:lime;}\r\n\r\n"+cue+tail,
        "alignment":"WEBVTT\r\n"+cue.replace("00:03.000\r\n","00:03.000 align:start\r\n")+tail,
        "style-arrow":"WEBVTT\r\nSTYLE\r\n::cue {color:rgb(256,257,258);}\r\n"+cue+tail,
        "style-replace":"WEBVTT\r\nSTYLE\r\n::cue(.red) {color:lime;}\r\n\r\nStyle:\r\n::cue(.red) {background:blue;}\r\n\r\n"+cue+tail,
        "comment":"WEBVTT\r\nSTYLE\r\n/*closed*/::cue {/*inside*/color:yellow;}\r\n\r\n"+cue+tail,
        "duplicate":"WEBVTT\r\n"+cue+cue+tail,
        "style-after-cue":"WEBVTT\r\n"+cue+"STYLE\r\n::cue {color:lime;}\r\n\r\n"+tail,
        "final-fragment":"WEBVTT\r\n"+cue+"00:03.000 --> 00:05.000\r\nignored",
        "raw-units":"WEBVTT\r\n"+cue.replace("First caption","First\x00\ud800discarded")+tail,
    }.items():
        data=(b"\xff\xfe"+text.encode("utf-16le","surrogatepass") if "\x00" in text else text.encode("ascii"))
        yield "vtt-parser-"+name+".vtt",data



def realtext_parser_variants():
    cue='<time begin="1" end="5"/>First caption'
    for name,text in {
        "control":cue,
        "map-replace":cue+'<time begin="1" end="5"/><clear/>Replacement caption',
        "italic":'<i>'+cue+'</i>',
        "map-order":'<time begin="3" end="5"/>Later<time begin="1" end="3"/><clear/>Earlier',
        "missing-end":'<time begin="3"/>First<time begin="1"/><clear/>Last',
        "restore":'<i>'+cue+'<clear/>Last caption</i>',
        "final-fragment":cue+'\r\nignored',
        "first-line":'Leading caption'+cue,
        "raw-units":cue.replace('First caption','First\x00\ud800discarded'),
        "overflow":cue.replace('"1"','"4294967297"').replace('"5"','"4294967301"'),
        "helper-control":'<time begin="1" end="5"/><font size="20">First caption</font>',
        "helper-red":'<time begin="1" end="5"/><font color="red" size="20">First caption</font>',
        "helper-bold":'<time begin="1" end="5"/><font size="20"><b>First caption</b></font>',
        "helper-small":'<time begin="1" end="5"/><font size="13">First caption</font>',
        "helper-large":'<time begin="1" end="5"/><font size="26">First caption</font>',
        "helper-negative":'<time begin="1" end="5"/><font size="-1">First caption</font>',
        "helper-duplicate":'<time begin="1" end="5"/><FONT SIZE="13" SiZe="20">First caption</FONT>',
        "helper-unquoted":'<time begin=1 end=5/><font color=red size=20>First caption</font>',
        "helper-mixed-quotes":'<time begin="1\' end=\'5"/><font size="20\'>First caption</font>',
        "helper-fractions":'<time begin="0:1.25" end="0:4.75"/><font size="20">First caption</font>',
    }.items():
        # ExtractString rejects unterminated trailing text. Keep the closing tag
        # on a separate line so raw-NUL fixtures retain it after line truncation.
        if name=='final-fragment':
            text=text.replace('\r\nignored','\r\n</window>\r\nignored')
        else:
            text+='\r\n</window>\r\n'
        text='<window>\r\n'+text
        data=(b"\xff\xfe"+text.encode("utf-16le","surrogatepass") if "\x00" in text else text.encode("ascii"))
        yield "realtext-parser-"+name+".rt",data


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
            if path.stem.startswith(("timeline-", "incremental-")):
                tick = Decimal("0.0000001")
                times = [str(Decimal(t) + offset) for t in range(7) for offset in (-tick, Decimal(0), tick)]
                cases[-1]["times"] = times + list(reversed(times)) + ["1", "3", "1", "5", "3"]
            if path.stem.startswith("clip-fade-"):
                # Fast scrolls can enter and leave between start/midpoint samples.
                # Keep the visible fade control and sample the early motion too.
                cases[-1]["times"] = ["0", "0.9999999", "1", "1.0000001", "1.125", "1.25", "1.5", "1.75", "2", "2.5", "3.9999999", "4", "4.0000001", "1.25", "2.5", "1"]
            if path.stem.startswith("layout-rect-"):
                cases[-1]["times"] = ["0", "0.9999999", "1", "1.5", "1.9999999", "2", "2.5", "3", "3.5", "4", "4.5", "5", "4", "3", "2", "1", "0", "2", "1", "3"]
            if path.stem.startswith("xombie-"):
                cases[-1]["times"] = ["0", "1.2339999", "1.234", "1.2340001", "2", "3", "4.3449999", "4.345", "4.3450001", "2", "1.234", "0"]
            if path.stem.startswith("text-parser-"):
                cases[-1]["times"] = ["0", "1.1999999", "1.2", "1.2339999", "1.234", "2", "4.4999999", "4.5", "4.5669999", "4.567", "2", "1.234"]
            if path.stem.startswith("legacy-parser-"):
                cases[-1]["times"] = ["0", "0.9999999", "1", "2", "2.3499999", "2.35", "2.37", "2.3999999", "2.4", "3.9999999", "4", "2.37", "1"]
            if path.stem.startswith("microdvd-parser-"):
                # Reference MS2RT(frame) then FRAME conversion inflates time by 10000.
                cases[-1]["times"] = ["0", "9999.9999999", "10000", "19999.9999999", "20000", "24000", "30000", "39999.9999999", "40000", "59999.9999999", "60000", "30000", "10000"]
            if path.stem.startswith("sami-parser-"):
                cases[-1]["times"] = ["0", "0.9999999", "1", "1.0000001", "2", "3.9999999", "4", "10.9999999", "11", "12", "13.9999999", "14", "2", "12"]
            if path.stem.startswith("vtt-parser-"):
                cases[-1]["times"] = ["0", "0.9999999", "1", "1.0000001", "2", "2.9999999", "3", "3.0000001", "4", "4.9999999", "5", "2"]
            if path.stem.startswith("realtext-parser-"):
                cases[-1]["times"] = ["0", "0.9999999", "1", "1.0000001", "2", "2.9999999", "3", "4", "4.9999999", "5", "2", "1"]
            warning_styles = {"field-tab-prefixes": "\tDefault", "field-unicode-style-space": "\u2003Default"}
            if path.stem in warning_styles:
                cases[-1]["dialog_actions"] = [{"title": "Warning",
                    "text_prefix": 'Unknown style found: "' + warning_styles[path.stem] + '", changed to "Default"!',
                    "button": "cancel"}]
    # Same source with additional geometry/stride/background configurations.
    path = DEST / "generated/plain.ass"
    for width, height, padding, bottom_up in ((641, 359, 12, False), (640, 360, 0, True)):
        cases.append({"id": f"geometry-{width}-{height}", "script": path.relative_to(ROOT).as_posix(),
            "sha256": hashlib.sha256(dict(files)[path]).hexdigest(), "open": "memory",
            "width": width, "height": height, "stride_padding": padding, "bottom_up": bottom_up,
            "fill_seed": 173, "times": ["0", "1", "2", "4", "2"]})
    for name in ("raster-be-mixed", "raster-gaussian-medium"):
        path = DEST / "generated" / (name + ".ass")
        for width, height, padding, bottom_up in ((641, 359, 12, False), (640, 360, 0, True)):
            cases.append({"id": f"{name}-geometry-{width}-{height}", "script": path.relative_to(ROOT).as_posix(),
                "sha256": hashlib.sha256(dict(files)[path]).hexdigest(), "open": "memory",
                "width": width, "height": height, "stride_padding": padding, "bottom_up": bottom_up,
                "fill_seed": 173, "times": ["0", "1", "2", "3.9999999", "4", "2", "1"]})
    return {"schema": 1, "description": "Starter differential corpus; not exhaustive compatibility evidence.",
            "expect_distinct_frames": [[a + "-" + mode, b + "-" + mode]
                                       for a, b in (("font-valid-attached", "font-valid-control"),
                                                    ("text-parser-srt-nul", "text-parser-srt-control"),
                                                    ("text-parser-mpl-missing-bracket", "text-parser-mpl-control"),
                                                    ("legacy-parser-old-missing-brace", "legacy-parser-old-control"),
                                                    ("legacy-parser-vp-caption-space", "legacy-parser-vp-control"),
                                                    ("microdvd-parser-style-sticky", "microdvd-parser-control"),
                                                    ("microdvd-parser-missing-brace", "microdvd-parser-brace-control"),
                                                    ("realtext-parser-map-replace", "realtext-parser-control"),
                                                    ("realtext-parser-italic", "realtext-parser-control"),
                                                    ("realtext-parser-helper-red", "realtext-parser-helper-control"),
                                                    ("realtext-parser-helper-fractions", "realtext-parser-helper-control"),
                                                    ("vtt-parser-color-replace", "vtt-parser-control"),
                                                    ("vtt-parser-alignment", "vtt-parser-control"),
                                                    ("sami-parser-title-skip", "sami-parser-control"),
                                                    ("sami-parser-fixed-column", "sami-parser-control"),
                                                    ("xombie-italic", "xombie-control"),
                                                    ("xombie-font-attached", "xombie-font-control"),
                                                    ("drawing-state-spline-close-twice", "drawing-state-control"),
                                                    ("raster-be-one", "raster-control"),
                                                    ("raster-be-fraction-high", "raster-control"),
                                                    ("raster-gaussian-medium", "raster-control"),
                                                    ("subrip-format-italic", "subrip-format-control"),
                                                    ("subrip-format-underline", "subrip-format-control"),
                                                    ("realtext-format-italic", "realtext-format-control"),
                                                    ("microdvd-format-italic", "microdvd-format-control"),
                                                    ("mpl2-format-italic", "mpl2-format-control"),
                                                    ("sami-format-italic", "sami-format-control"),
                                                    ("sami-format-font-black", "sami-format-font-zero"),
                                                    ("encoding-western", "encoding-western-control"),
                                                    ("ass-style-v5-box", "ass-style-v5-outline"),
                                                    ("ass-header-scaled-substring", "ass-header-scaled-control"),
                                                    ("microdvd-style-italic", "microdvd-style-control"),
                                                    ("sami-file-title-skips-sync", "sami-file-control"),
                                                    ("subrip-cue-number-in-caption", "subrip-cue-control"),
                                                    ("old-subrip-pipes", "old-subrip-control"),
                                                    ("vplayer-pipes", "vplayer-control"),
                                                    ("layout-rect-bottom", "layout-rect-split-layers"),
                                                    ("text-runs-soft-wrap-zero", "text-runs-soft-wrap-two"),
                                                    ("wrap-width-style-0", "wrap-width-style-2"),
                                                    ("line-layout-align-1", "line-layout-align-9"),
                                                    ("drawing-storage-control", "drawing-storage-fractional-scale"),
                                                    ("span-merge-control", "span-merge-horizontal-border"),
                                                    ("arc-queue-control", "arc-queue-round"),
                                                    ("path-buffer-control", "path-buffer-spaced"),
                                                    ("scan-row-single", "scan-row-hole"),
                                                    ("span-coverage-phase-0", "span-coverage-phase-7"),
                                                    ("variance-control", "variance-diagonal"),
                                                    ("blur-planes-control", "blur-planes-body"),
                                                    ("clip-fade-banner-control", "clip-fade-banner-edge"),
        ("clip-fade-scroll-control", "clip-fade-scroll-up"),
        ("clip-mask-normal", "clip-mask-inverse"),
        ("gauss-planes-body-light", "gauss-planes-body-wide"))
                                       for mode in ("file", "memory")], "cases": cases}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="verify checked-in fixtures and manifest")
    args = parser.parse_args()
    files = [(path, path.read_bytes()) for path in sorted((ROOT / "test").glob("*.ass"))]
    generated = [(DEST / "generated" / (name + ".ass"), data) for name, data in variants()]
    generated += [(DEST / "generated" / name, data) for name, data in incremental_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in subrip_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in subrip_cue_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in legacy_pipe_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in layout_rect_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in document_layout_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in text_run_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in wrap_width_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in line_layout_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in drawing_preparation_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in drawing_storage_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in path_alignment_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in span_merge_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in arc_queue_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in clip_fade_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in clip_mask_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in gauss_planes_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in blur_planes_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in variance_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in span_coverage_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in scan_row_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in ellipse_arc_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in ellipse_storage_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in path_buffer_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in realtext_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in ass_header_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in ass_style_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in ass_dialogue_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in encoding_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in microdvd_style_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in microdvd_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in mpl2_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in sami_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in sami_file_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in xombie_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in text_parser_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in legacy_parser_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in microdvd_parser_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in sami_parser_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in vtt_parser_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in realtext_parser_variants()]
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
