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
                                                    ("drawing-state-spline-close-twice", "drawing-state-control"),
                                                    ("raster-be-one", "raster-control"),
                                                    ("raster-be-fraction-high", "raster-control"),
                                                    ("raster-gaussian-medium", "raster-control"),
                                                    ("subrip-format-italic", "subrip-format-control"),
                                                    ("subrip-format-underline", "subrip-format-control"),
                                                    ("realtext-format-italic", "realtext-format-control"),
                                                    ("microdvd-format-italic", "microdvd-format-control"),
                                                    ("mpl2-format-italic", "mpl2-format-control"))
                                       for mode in ("file", "memory")], "cases": cases}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="verify checked-in fixtures and manifest")
    args = parser.parse_args()
    files = [(path, path.read_bytes()) for path in sorted((ROOT / "test").glob("*.ass"))]
    generated = [(DEST / "generated" / (name + ".ass"), data) for name, data in variants()]
    generated += [(DEST / "generated" / name, data) for name, data in incremental_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in subrip_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in realtext_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in microdvd_variants()]
    generated += [(DEST / "generated" / name, data) for name, data in mpl2_variants()]
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
