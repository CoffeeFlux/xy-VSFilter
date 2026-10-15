#!/usr/bin/env python3
"""Compare real linked CSimpleTextSubtitle lifecycles on Windows.

Requires the reference and candidate Release libraries built by the native
comparison job. No extracted method implementations or substitute ATL types.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

from dialog_capture import DialogCapture

ROOT = Path(__file__).resolve().parents[2]
PIN = json.loads((ROOT / "tools/compat/reference.json").read_text())["commit"]
SOURCE = ROOT / "tools/compat/document_native_lifecycle.cpp"
LIBRARIES = (
    "strmbaseR.lib", "dsutilR.lib", "subtitlesR.lib", "subpicR.lib",
    "basevideofilterR.lib", "libssfR.lib", "unrarR.lib",
)
SYSTEM_LIBRARIES = (
    "delayimp.lib", "winmm.lib", "dwmapi.lib", "Mfuuid.lib", "strmiids.lib",
    "uuid.lib", "ole32.lib", "oleaut32.lib", "shell32.lib", "shlwapi.lib",
    "advapi32.lib", "user32.lib", "gdi32.lib", "comdlg32.lib", "comctl32.lib",
    "wininet.lib", "ws2_32.lib", "version.lib", "psapi.lib",
)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def git(root, *args):
    return subprocess.check_output(["git", "-C", str(root), *args], timeout=60).decode().strip()


def unknown_dialog_actions():
    return [{"title":"Warning", "text_prefix":warning_text(name), "button":button}
            for name,button in (("UnknownA","ok"),("UnknownB","cancel"),("UnknownOpen","cancel"))]


def warning_text(name):
    return f'Unknown style found: "{name}", changed to "Default"!\n\nPress Cancel to ignore further warnings.'


def validate_dialogs(events):
    actions=unknown_dialog_actions()
    if len(events)!=len(actions): raise ValueError("missing or extra unknown-style dialogs")
    for event,action in zip(events,actions):
        if event.get("title")!=action["title"] or event.get("class")!="#32770" or event.get("response")!=action["button"]:
            raise ValueError("unknown-style dialog identity/response mismatch")
        controls=event.get("controls",[])
        buttons=[c for c in controls if c.get("class")=="Button"]
        if sorted(c.get("id") for c in buttons)!=[1,2]: raise ValueError("unknown-style OK/Cancel controls missing")
        if not any(c.get("class")=="Static" and c.get("text")==action["text_prefix"] for c in controls):
            raise ValueError("unknown-style full warning text mismatch")


def run_dialog_process(command,cwd,stem,timeout):
    """Bounded child observation; all paths kill/reap any still-running child."""
    destination=Path(str(stem)+"-dialogs");destination.mkdir()
    process=subprocess.Popen(command,cwd=cwd,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    observer=DialogCapture(unknown_dialog_actions(),destination,process_id=process.pid)
    try:
        with observer:
            try: stdout,stderr=process.communicate(timeout=timeout)
            except subprocess.TimeoutExpired:
                process.kill();stdout,stderr=process.communicate()
                Path(str(stem)+".stdout").write_bytes(stdout)
                Path(str(stem)+".stderr").write_bytes(stderr)
                raise RuntimeError(f"{stem.name} timed out after {timeout}s; unresolved")
        Path(str(stem)+".stdout").write_bytes(stdout)
        Path(str(stem)+".stderr").write_bytes(stderr)
        (destination/"dialogs.json").write_text(json.dumps(observer.events,indent=2)+"\n",encoding="utf-8")
        if process.returncode: raise RuntimeError(f"{stem.name} exited {process.returncode}; unresolved")
        if observer.error: raise RuntimeError(observer.error)
        validate_dialogs(observer.events)
        return subprocess.CompletedProcess(command,process.returncode,stdout,stderr),observer.events
    finally:
        if process.poll() is None:
            process.kill();process.communicate()


def unknown_rows():
    names=["!@dkdfjlakfkjjklezdv^5132ae","UnknownA","UnknownA","Known","UnknownA","Default","UnknownB","UnknownC"]
    rows=[]
    for stage,values in (("unknown-before",names),("unknown-after",[names[0],"Default","Default","Known","Default","Default","Default","Default"]),("unknown-open-after",["Default"])):
        rows.append(f"unknown-stage {stage} entries={len(values)}".encode())
        for i,name in enumerate(values):
            rows.append((f"unknown-row {stage} {i} {len(name)} "+" ".join(f"{ord(c):04x}" for c in name)).encode())
    return rows


def conversion_rows():
    """Finite native llround outputs and actual/no-op rebuild observations."""
    return [row.encode() for row in (
        "conversion-time-input mode=0 entries=2 segments=2 animated=1 times=-1200000,-400000;400000,1200000",
        "conversion-frame-half mode=1 entries=2 segments=2 animated=0 times=-2,-1;1,2",
        "conversion-frame-noop-zero mode=1 entries=2 segments=2 animated=1 times=-2,-1;1,2",
        "conversion-time-half mode=0 entries=2 segments=2 animated=0 times=-78125,-39063;39063,78125",
        "conversion-time-noop-zero mode=0 entries=2 segments=2 animated=1 times=-78125,-39063;39063,78125",
        "conversion-empty-frame mode=1 entries=0 segments=0 animated=0 times=-",
        "conversion-empty-frame-noop mode=1 entries=0 segments=0 animated=0 times=-",
        "conversion-empty-time mode=0 entries=0 segments=0 animated=0 times=-",
    )]


def frame_search_rows():
    # Selected native-order search results, including the storage-last shortcut
    # for nonmonotone starts; this is a finite table, not a search implementation.
    histories=(
        ("frame-empty",0,[],[(-1,-1,0,0),(0,-1,0,0),(800000,-1,0,0)]),
        ("frame-boundaries",3,[(0,1,1,[0,1]),(2,3,0,[2])],
         [(-1,-1,0,-1),(0,0,0,0),(799999,1,0,0),(800000,1,1,-1),
          (1599999,1,1,-1),(1600000,2,1,1),(2399999,2,1,1),(2400000,2,2,-1)]),
        ("frame-nonmonotone",3,[(0,1,1,[1]),(1,2,0,[2]),(2,3,0,[0])],
         [(-1,-1,0,-1),(0,1,0,0),(39062,1,0,0),(39063,2,1,1),
          (78124,2,1,1),(78125,2,2,2),(117187,2,2,2),(117188,2,3,-1)]))
    rows=[]
    for name,entries,segments,queries in histories:
        count=len(segments);rows.append(f"frame-search-stage {name} entries={entries} segments={count}")
        for q,(time,entry,next_index,containing) in enumerate(queries):
            found=int(count>0 and containing>=0);next_found=int(next_index<count)
            alias=containing if found else count
            start,end,animated,members=segments[containing] if found else (111,222,1,[77])
            snapshot=f"{start},{end},{animated},{len(members)}"+"".join(f",{member}" for member in members)
            rows.append(f"frame-search-row {name} {q} time={time} entry={entry} next={next_found},{next_index},{count} contain={found},{containing},{count} alias={next_index},{alias} snapshot={snapshot}")
        rows.append(f"frame-search-storage {name} segments={count} animated={int(count>0)} callbacks=0")
    return [row.encode() for row in rows]


def open_order_rows():
    return [row.encode() for row in (
        "open-order-callback open-sidecar entries=2 segments=1 screen-before=720,480 screen-after=720,480 wrap=1 collisions=1 scaled=1 simple=1 sidecar-held=1",
        "open-order-stage open-sidecar result=1 empties=2 callbacks=1 entries=2 segments=1 screen=720,480 sidecar-released=1",
        "open-order-callback open-fallback-zero entries=1 segments=1 screen-before=0,0 screen-after=0,0 wrap=0 collisions=0 scaled=0 simple=1 sidecar-held=0",
        "open-order-stage open-fallback-zero result=1 empties=2 callbacks=1 entries=1 segments=1 screen=384,288 sidecar-released=0",
        "open-order-callback open-fallback-live entries=1 segments=1 screen-before=0,0 screen-after=0,177 wrap=0 collisions=0 scaled=0 simple=1 sidecar-held=0",
        "open-order-stage open-fallback-live result=1 empties=2 callbacks=1 entries=1 segments=1 screen=0,177 sidecar-released=0",
        "open-order-callback open-filename entries=1 segments=1 screen-before=640,360 screen-after=640,360 wrap=0 collisions=0 scaled=0 simple=0 sidecar-held=0",
        "open-order-stage open-filename result=1 empties=2 callbacks=1 entries=1 segments=1 screen=640,360 sidecar-released=0",
        "open-order-stage open-rejected result=0 empties=13 callbacks=0 entries=0 segments=0 screen=0,0 sidecar-released=0 position=0",
    )]


def setstr_plan_rows():
    rows=[]
    for stage,index,unicode,units in (
        ("setstr-same-wide",0,1,[0x3042,13,92,78,90]),
        ("setstr-packed-to-wide",0,1,[0x3042,13,92,78,90]),
        ("setstr-same-packed",1,0,[0x82,0xa0,13,92,78,90]),
        ("setstr-wide-to-packed",1,0,[0x82,0xa0,13,92,78,90]),
        ("setstr-nul-tail",0,1,[92,78,0,10,0])):
        rows.append(f"setstr-stage {stage}")
        rows.append(f"setstr-value {stage} index={index} unicode={unicode} {len(units)} "+" ".join(f"{unit:04x}" for unit in units))
    return [row.encode() for row in rows]


def text_plan_rows():
    packed=[ord(c) for c in r"{\i1}"]+[0x82,0xa0]+[ord(c) for c in r"{\i}\NZ"]
    wide=[ord(c) for c in r"{\i1}"]+[0x3042]+[ord(c) for c in r"{\i}\NZ"]
    plain_packed=[ord(c) for c in "<i>"]+[0x82,0xa0]+[ord(c) for c in "</i>\nZ"]
    plain_wide=[ord(c) for c in "<i>あ</i>\nZ"]
    def units(value):return f"{len(value)} "+" ".join(f"{u:04x}" for u in value)
    rows=[]
    for stage in ("text-plan-packed-input","text-plan-wide","text-plan-packed-roundtrip"):
        rows.append(f"text-plan-stage {stage}")
        for ssa in (1,0):
            for native_wide in (1,0):
                value=(wide if native_wide else packed) if ssa else (plain_wide if native_wide else plain_packed)
                rows.append(f"text-plan-get {stage} wide={native_wide} ssa={ssa} "+units(value))
        if stage!="text-plan-packed-input":rows.append(f"text-plan-noop wide={int(stage=='text-plan-wide')} callbacks=0")
    rows += ["text-plan-owned-mutated "+units([82,0,0xd83d,0xde00,81]),
             "text-plan-owned-reread "+units([82,0,0xd83d,0xde00,90]),
             "text-plan-stage text-plan-raw-independent"]
    return [row.encode() for row in rows]


def copy_styles_stages():
    return (b"copy-styles-self-before", b"copy-styles-self-after",
            b"copy-styles-grown-source-before", b"copy-styles-grown-result", b"copy-styles-grown-source-after",
            b"copy-styles-mixed-source-before", b"copy-styles-mixed-result", b"copy-styles-mixed-source-after")


def save_section_bytes():
    common="; For Sub Station Alpha info and downloads,\n; go to http://www.eswat.demon.co.uk/\n; or email kotus@eswat.demon.co.uk\n; \n"
    advanced="; Advanced Sub Station Alpha script format developed by #Anime-Fansubs@EfNET\n; http://www.anime-fansubs.org\n; \n; For additional info and downloads go to http://gabest.org/\n; or email gabest@freemail.hu\n; \n"
    styles={"ssa":"[V4 Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, TertiaryColour, BackColour, Bold, Italic, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, AlphaLevel, Encoding\n",
            "ass":"[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"}
    texts={}
    for fmt in ("ssa","ass"):
        text="[Script Info]\n"+("; This is a Sub Station Alpha v4 script.\n" if fmt=="ssa" else "; This is an Advanced Sub Station Alpha v4+ script.\n")+common+(advanced if fmt=="ass" else "")
        text+="; Note: This file was saved by Subresync.\n; \nScriptType: "+("v4.00" if fmt=="ssa" else "v4.00+")+"\nCollisions: Reverse\n"
        text+=("ScaledBorderAndShadow: Yes\n" if fmt=="ass" else "")+"PlayResX: -17\nPlayResY: 23\nTimer: 100.0000\n\n"+styles[fmt]
        texts["sections-"+fmt]=text
    texts["sections-smi"]='<SAMI>\n<HEAD>\n<STYLE TYPE="text/css">\n<!--\nP {margin-left: 16pt; margin-right: 16pt; margin-bottom: 16pt; margin-top: 16pt;\n   text-align: center; font-size: 18pt; font-family: arial; font-weight: bold; color: #f0f0f0;}\n.UNKNOWNCC {Name:Unknown; lang:en-US; SAMIType:CC;}\n-->\n</STYLE>\n</HEAD>\n\n<BODY>\n</BODY>\n</SAMI>\n'
    texts["sections-srt"]=""
    texts["sections-srt-sidecar"]="ScriptType: v4.00+\nPlayResX: -17\nPlayResY: 23\n\n"+styles["ass"]
    return {name:b"\xef\xbb\xbf"+value.replace("\n","\r\n").encode() for name,value in texts.items()}

def validate_trace(data, pointer_bits, *, require_named_stages=True, require_identity_stages=True, require_interpretation_stages=True, require_layout_stages=True, require_metadata_stages=True, require_selected_copy_stages=True, require_style_rename_stages=True, require_sort_stages=True, require_base_empty_stages=True, require_lcid_stages=True, require_resolution_stages=True, require_layout_size_stages=True, require_conversion_stages=True, require_unknown_stages=True, require_add_style_stages=True, require_copy_styles_stages=True, require_text_plan_stages=True, require_preparation_stages=True, require_setstr_stages=True, require_open_order_stages=True, require_frame_search_stages=True, require_save_body_stages=True, require_save_sections_stages=True, cleanup=False):
    if cleanup:
        # These APIs were removed from the pinned cleanup, not skipped failures.
        require_sort_stages = require_conversion_stages = False
        require_text_plan_stages = require_setstr_stages = False
        require_save_body_stages = require_save_sections_stages = False
    lines = data.splitlines()
    if not lines or lines[0] != f"provenance pointer_bits={pointer_bits}".encode():
        raise ValueError("missing or incorrect native architecture provenance")
    if lines[-1] != b"document-native-lifecycle-v1 complete":
        raise ValueError("missing native lifecycle completion marker")
    for prefix in (b"state ", b"entry ", b"segment ", b"callback ", b"text "):
        if not any(line.startswith(prefix) for line in lines):
            raise ValueError(f"missing native lifecycle trace section {prefix!r}")

    if require_preparation_stages:
        stages = (("preparation-initial",1,1), ("preparation-fields",5,3),
                  ("preparation-add-zero-rejected",5,3), ("preparation-entry-zero",6,3))
        positions = []
        for stage,entries,segments in stages:
            token = f"preparation-stage {stage} entries={entries} segments={segments} animated=1".encode()
            if lines.count(token) != 1 or sum(line.startswith(f"preparation-stage {stage} ".encode()) for line in lines) != 1:
                raise ValueError(f"missing/invalid native preparation stage {stage}")
            positions.append(lines.index(token))
        if positions != sorted(positions):
            raise ValueError("native preparation stage order differs")
        raw = b"preparation-value skipped-lf 9 0058 005c 004e 005c 004e 0000 000a 0059 0000 0"
        if lines.count(raw) != 1 or not positions[0] < lines.index(raw) < positions[1]:
            raise ValueError("missing/invalid bounded native skipped-LF observation")

    if require_save_sections_stages:
        labels=["sections-ssa","sections-ass","sections-smi","sections-srt"]
        expected=[b"save-sections-stage before"]+[f"save-sections-export {name}".encode() for name in labels]+[b"save-sections-stage after"]
        if [line for line in lines if line.startswith(b"save-sections-")]!=expected:
            raise ValueError("missing or incorrect fixed SaveAs section stages")
        rows=[line for line in lines if line.startswith(b"saved-file sections-")]
        names=[name for label in labels for name in (label,label+"-sidecar")]
        if len(rows)!=len(names):raise ValueError("missing fixed SaveAs section bytes")
        expected_bytes=save_section_bytes()
        for row,name in zip(rows,names):
            if name.endswith("-sidecar") and name!="sections-srt-sidecar":
                if row!=f"saved-file {name} exists=0".encode():raise ValueError("unexpected empty script sidecar")
                continue
            match=re.fullmatch(f"saved-file {name} exists=1 bytes=".encode()+rb"([0-9]+)((?: [0-9a-f]{2})+)",row)
            if not match:raise ValueError("invalid section byte trace")
            value=bytes.fromhex(match[2].decode())
            if len(value)!=int(match[1]) or len(value)>1024*1024:raise ValueError("invalid section byte count")
            if name=="sections-srt-sidecar":
                if not value.startswith(expected_bytes[name]) or not value[len(expected_bytes[name]):].startswith(b"Style: Default,Arial,"):
                    raise ValueError("incorrect fixed sidecar header/native style boundary")
            elif value!=expected_bytes[name]:raise ValueError("incorrect fixed SaveAs section bytes")

    if require_save_body_stages:
        expected = [b"save-body-stage time-before"]
        labels = [f"body-{fmt}-utf8" for fmt in ("srt","sub","smi","psb","ssa","ass")] + ["body-txt-le16", "body-frame-srt-le16"]
        expected += [f"save-body-export {label}".encode() for label in labels[:7]]
        expected += [b"save-body-stage time-after", b"save-body-stage frame-before", b"save-body-export body-frame-srt-le16", b"save-body-stage frame-after"]
        if [line for line in lines if line.startswith(b"save-body-")] != expected:
            raise ValueError("missing or incorrect SaveAs body stage/export order")
        observed = [line for line in lines if line.startswith(b"saved-file body-")]
        expected_names = [name for label in labels for name in (label,label+"-sidecar")]
        if len(observed)!=len(expected_names):
            raise ValueError("missing SaveAs body file observations")
        payloads = {}
        for line,name in zip(observed,expected_names):
            absent=name in ("body-ssa-utf8-sidecar","body-ass-utf8-sidecar")
            prefix=f"saved-file {name} exists={int(not absent)}".encode()
            if absent:
                if line!=prefix: raise ValueError("unexpected SaveAs sidecar")
                continue
            match=re.fullmatch(re.escape(prefix)+rb" bytes=([0-9]+)((?: [0-9a-f]{2})+)",line)
            if not match: raise ValueError("invalid SaveAs exact byte trace")
            payload=bytes.fromhex(match[2].decode());count=int(match[1])
            if len(payload)!=count or not 0<count<=1024*1024: raise ValueError("invalid SaveAs byte count")
            payloads[name]=payload
        frame=b"\xff\xfe"+"1\r\n00:00:00,003 --> 00:00:00,011\r\nFrame body\r\n\r\n".encode("utf-16-le")
        if payloads["body-frame-srt-le16"]!=frame or payloads["body-txt-le16"]!=b"\xff\xfe":
            raise ValueError("incorrect supported LE16 body or unsupported TXT bytes")
        srt=b"\xef\xbb\xbf1\r\n00:00:12,345 --> 00:00:23,456\r\nrow one\r\n\r\n2\r\n01:00:00,012 --> 01:00:01,012\r\nrow two\r\n\r\n"
        if payloads["body-srt-utf8"]!=srt: raise ValueError("incorrect interleaved-negative SRT numbering/body")

    if require_frame_search_stages:
        expected = frame_search_rows()
        if cleanup: expected = [re.sub(rb" entry=-?[0-9]+", b"", row) for row in expected]
        if [line for line in lines if line.startswith(b"frame-search-")]!=expected:
            raise ValueError("missing or incorrect FRAME full-owner query/alias/snapshot/retention observations")

    if require_open_order_stages:
        expected = open_order_rows()
        if cleanup: expected = [row.replace(b"sidecar-held=1", b"sidecar-held=0") for row in expected]
        if [line for line in lines if line.startswith(b"open-order-")]!=expected:
            raise ValueError("missing or incorrect Open sidecar/callback/fallback/Empty/rejection ordering")

    if require_setstr_stages:
        expected=setstr_plan_rows()
        if [line for line in lines if line.startswith((b"setstr-stage ",b"setstr-value "))]!=expected:
            raise ValueError("missing or incorrect SetStr converter/flag/CR/LF/raw-tail observations")
        for stage in expected[::2]:
            name=stage.split()[1]
            if lines.count(b"style-rename-segments "+name+b" count=2 animated=1")!=1:
                raise ValueError("SetStr must retain full animated segments without rebuild")

    if require_text_plan_stages:
        if [line for line in lines if line.startswith(b"text-plan-")] != text_plan_rows():
            raise ValueError("missing or incorrect text-plan getter/conversion/owned-output observations")
        for stage in (b"text-plan-wide-noop",b"text-plan-packed-noop",b"text-plan-final"):
            expected=b"style-rename-segments "+stage+b" count=1 animated=1"
            if lines.count(expected)!=1:raise ValueError("text plan must retain animated segments without rebuild")

    if require_copy_styles_stages:
        expected = [b"copy-styles-stage " + name for name in copy_styles_stages()]
        if [line for line in lines if line.startswith(b"copy-styles-stage ")] != expected:
            raise ValueError("missing or incorrect CopyStyles session stages")
        for stage in (b"copy-styles-self-after", b"copy-styles-grown-result", b"copy-styles-mixed-result"):
            prefix = b"style-rename-segments " + stage + b" count="
            observations = [line for line in lines if line.startswith(prefix)]
            if len(observations) != 1 or not re.fullmatch(re.escape(prefix) + rb"[1-9][0-9]* animated=[1-9][0-9]*", observations[0]):
                raise ValueError("CopyStyles complete animated-segment observation missing")

    if require_add_style_stages:
        stages = [b"add-style-stage " + name for name in (
            b"add-style-equal-before", b"add-style-equal-after", b"add-style-name-classes")]
        if [line for line in lines if line.startswith(b"add-style-stage ")] != stages:
            raise ValueError("missing or incorrect AddStyle lifecycle stages")
        retained = [b"add-style-retained ZeroNeg bits=8000000000000000 font 4 004e 0000 d801 005a",
                    b"add-style-retained ZeroPos bits=0000000000000000 font 4 004e 0000 d802 005a"]
        if [line for line in lines if line.startswith(b"add-style-retained ")] != retained:
            raise ValueError("AddStyle duplicate did not retain original signedzero/full font")
        if not all(lines.index(stages[0]) < lines.index(row) < lines.index(stages[1]) for row in retained):
            raise ValueError("AddStyle retention observation order differs")
        name_class=b"add-style-name-classes count=2 default=1 empty=1 default-size=21 empty-size=22 entry 2 0000 004b"
        if lines.count(name_class)!=1 or lines.index(name_class)<lines.index(stages[2]):
            raise ValueError("AddStyle logical-empty/leading-NUL contrast missing")

    if require_unknown_stages:
        if [line for line in lines if line.startswith((b"unknown-stage ",b"unknown-row "))] != unknown_rows():
            raise ValueError("missing or incorrect unknown-style row observations")
        callback=b"unknown-open-callback mode=0 entries=1 segments=1 style 11 0055 006e 006b 006e 006f 0077 006e 004f 0070 0065 006e"
        if lines.count(callback)!=1 or lines.index(callback)>=lines.index(b"unknown-stage unknown-open-after entries=1"):
            raise ValueError("missing or misplaced pre-fallback Open callback")

    if require_conversion_stages:
        expected = conversion_rows()
        observed = [line for line in lines if line.startswith(b"conversion-stage ")]
        if observed != [b"conversion-stage " + row for row in expected]:
            raise ValueError("missing or incorrect conversion stage/state")
        real = {"conversion-frame-half", "conversion-time-half", "conversion-empty-frame", "conversion-empty-time"}
        callbacks = [b"conversion-callback " + row for row in expected if row.split(b" ",1)[0].decode() in real]
        if [line for line in lines if line.startswith(b"conversion-callback ")] != callbacks:
            raise ValueError("missing or incorrect conversion callback state/count")
        for callback in callbacks:
            stage = callback.replace(b"conversion-callback ", b"conversion-stage ",1)
            if lines.index(callback) >= lines.index(stage):
                raise ValueError("conversion callback must observe committed state before return")

    if require_identity_stages:
        stages = ("identity-initial", "identity-read-independent", "identity-copy-asymmetric-path",
                  "identity-copy-independent-source", "identity-callback", "identity-remove-entries",
                  "identity-empty", "identity-self-copy", "identity-open-success")
        positions = []
        for stage in stages:
            matches = [(index, line) for index, line in enumerate(lines)
                       if line.startswith(f"identity-stage {stage} name ".encode())]
            if len(matches) != 1 or b" path " not in matches[0][1]:
                raise ValueError(f"missing/duplicate identity lifecycle stage {stage}")
            positions.append(matches[0][0])
        if positions != sorted(positions):
            raise ValueError("identity lifecycle stage order differs")

    if require_interpretation_stages:
        stages = ("interpretation-constructed", "interpretation-supplied", "interpretation-copy",
                  "interpretation-copy-independent", "interpretation-empty", "interpretation-self-copy",
                  "interpretation-microdvd-open", "interpretation-frame", "interpretation-frame-noop",
                  "interpretation-time", "interpretation-time-noop")
        if cleanup: stages = stages[:7]
        positions = []
        for stage in stages:
            matches = [(index, line) for index, line in enumerate(lines)
                       if line.startswith(f"interpretation-stage {stage} mode=".encode())]
            if len(matches) != 1 or b" type=" not in matches[0][1] or b" encoding=" not in matches[0][1]:
                raise ValueError(f"missing/duplicate interpretation lifecycle stage {stage}")
            positions.append(matches[0][0])
        if positions != sorted(positions):
            raise ValueError("interpretation lifecycle stage order differs")

    if require_layout_stages:
        stages = ("layout-constructed", "layout-supplied", "layout-copy", "layout-copy-independent",
                  "layout-empty", "layout-self-copy", "layout-open-fallback", "layout-open-x1280",
                  "layout-open-y1024", "layout-open-repeated")
        positions = []
        for stage in stages:
            matches = [(index, line) for index, line in enumerate(lines)
                       if line.startswith(f"layout-stage {stage} width=".encode())]
            if len(matches) != 1 or any(token not in matches[0][1] for token in (b" height=", b" wrap=", b" collisions=", b" scaled=")):
                raise ValueError(f"missing/duplicate script layout stage {stage}")
            positions.append(matches[0][0])
        if positions != sorted(positions):
            raise ValueError("script layout stage order differs")
        for stage in stages[-4:]:
            matches = [line for line in lines if line.startswith(f"layout-callback {stage} width=".encode())]
            if len(matches) != 1 or b" height=" not in matches[0]:
                raise ValueError(f"missing/duplicate layout callback observation {stage}")

    if require_metadata_stages:
        stages = ("metadata-constructed", "metadata-supplied", "metadata-copy-forced-only",
                  "metadata-copy-independent", "metadata-empty", "metadata-self-copy",
                  "metadata-forced-style", "metadata-unforced-style", "metadata-color-none",
                  "metadata-color-tv601", "metadata-color-tv709", "metadata-color-tv2020",
                  "metadata-color-pc601", "metadata-color-pc709", "metadata-color-pc2020",
                  "metadata-color-unrecognized", "metadata-color-repeated", "metadata-fallback-srt",
                  "metadata-fallback-microdvd")
        shape = rb"^metadata-(?:stage|callback) [^ ]+ forced=[01] par-type=-?\d+ par-bits [0-9a-f]{16} matrix=-?\d+ range=-?\d+$"
        positions = []
        for stage in stages:
            matches = [(index, line) for index, line in enumerate(lines)
                       if line.startswith(f"metadata-stage {stage} ".encode())]
            if len(matches) != 1 or not re.fullmatch(shape, matches[0][1]):
                raise ValueError(f"missing/duplicate initialized metadata stage {stage}")
            positions.append(matches[0][0])
        if positions != sorted(positions):
            raise ValueError("initialized metadata stage order differs")
        for stage in stages[8:]:
            matches = [line for line in lines if line.startswith(f"metadata-callback {stage} ".encode())]
            if len(matches) != 1 or not re.fullmatch(shape, matches[0]):
                raise ValueError(f"missing/duplicate metadata callback {stage}")

    if require_selected_copy_stages:
        stages = ("copy-source-before", "copy-source-mutated", "copy-destination-after", "copy-source-after",
                  "copy-empty-key-source", "copy-empty-key-result", "copy-empty-key-source-after")
        positions = []
        for stage in stages:
            token = f"copy-state-stage {stage}".encode()
            if lines.count(token) != 1:
                raise ValueError(f"missing/duplicate selected Copy stage {stage}")
            positions.append(lines.index(token))
        if positions != sorted(positions):
            raise ValueError("selected Copy stage order differs")

    if require_layout_size_stages:
        stages = (("layout-size-supplied",320,240,800,600), ("layout-size-copy-retained",320,240,800,600),
                  ("layout-size-source-independent",352,288,800,600), ("layout-size-empty-retained",352,288,800,600),
                  ("layout-size-self-copy-retained",352,288,800,600), ("layout-size-parser-x",352,288,640,600),
                  ("layout-size-parser-y",352,288,640,360), ("layout-size-parser-repeated",352,288,720,600),
                  ("layout-size-parser-nonpositive",352,288,-2,0))
        positions = []
        for stage,sx,sy,dx,dy in stages:
            token = f"layout-size-stage {stage} source={sx},{sy} destination={dx},{dy}".encode()
            if lines.count(token) != 1:
                raise ValueError(f"missing/invalid layout size stage {stage}")
            positions.append(lines.index(token))
            if "parser" in stage:
                callback = f"layout-size-callback {stage} width={dx} height={dy}".encode()
                if lines.count(callback) != 1 or lines.index(callback) >= lines.index(token):
                    raise ValueError(f"missing/invalid layout size callback {stage}")
        if positions != sorted(positions):
            raise ValueError("layout size stage order differs")

    if require_resolution_stages:
        stages = ("resolution-raw-prefix", "resolution-lowercase", "resolution-case-fallback",
                  "resolution-empty-prefix", "resolution-forced", "resolution-forced-replaced",
                  "resolution-output-independent")
        positions = []
        for stage in stages:
            token = f"resolution-stage {stage}".encode()
            if lines.count(token) != 1:
                raise ValueError(f"missing/duplicate resolution stage {stage}")
            positions.append(lines.index(token))
            values = [line for line in lines if line.startswith(f"resolved-style {stage} charset=".encode())]
            if len(values) != 1 or b" style " not in values[0]:
                raise ValueError(f"missing resolved style value {stage}")
            fields = values[0].split(b" style ",1)[1].split()
            try:
                units = int(fields[1])
                font = fields[2:2+units]
                scalars = fields[2+units:]
                floats = (0,11,12,13,14,23,24,25,26,27,28,29,30,31,32)
                colors = (15,17,19,21)
                valid = (units >= 0 and len(font) == units and len(scalars) == 34
                         and all(re.fullmatch(rb"[0-9a-f]{4}", unit) for unit in font)
                         and all(re.fullmatch(rb"[0-9a-f]{16}", scalars[index]) for index in floats)
                         and all(re.fullmatch(rb"[0-9a-f]{8}", scalars[index]) for index in colors)
                         and re.fullmatch(rb"-?[0-9]+", fields[0])
                         and all(re.fullmatch(rb"-?[0-9]+", value) for index,value in enumerate(scalars) if index not in floats+colors))
            except (ValueError, IndexError):
                valid = False
            if not valid:
                raise ValueError(f"truncated/malformed complete style value {stage}")
        if positions != sorted(positions):
            raise ValueError("resolution stage order differs")
        if sum(line.startswith(b"resolution-mutated-output style ") for line in lines) != 1:
            raise ValueError("missing/duplicate independent style output mutation")

    if require_lcid_stages:
        expected = (("lcid-supplied",1031,0), ("lcid-copy-retained",1031,0),
                    ("lcid-source-independent",1033,0), ("lcid-repeated-empty",1033,0),
                    ("lcid-reused-empty",1033,1031))
        positions = []
        for stage, source, destination in expected:
            token = f"lcid-stage {stage} source={source} destination={destination}".encode()
            if lines.count(token) != 1 or sum(line.startswith(f"lcid-stage {stage} ".encode()) for line in lines) != 1:
                raise ValueError(f"missing/invalid supplied LCID stage {stage}")
            positions.append(lines.index(token))
        if positions != sorted(positions):
            raise ValueError("LCID stage order differs")

    if require_base_empty_stages:
        stages = ("empty-populated", "empty-cleared", "empty-repeated", "empty-reused", "empty-final")
        positions = []
        for stage in stages:
            token = f"empty-stage {stage}".encode()
            if lines.count(token) != 1:
                raise ValueError(f"missing/duplicate base Empty stage {stage}")
            positions.append(lines.index(token))
        if positions != sorted(positions):
            raise ValueError("base Empty stage order differs")

    if require_sort_stages:
        stages = ("sort-input", "sort-time", "sort-time-repeat", "sort-readorder", "sort-readorder-repeat")
        positions = []
        for stage in stages:
            token = f"sort-stage {stage}".encode()
            if lines.count(token) != 1:
                raise ValueError(f"missing/duplicate Sort stage {stage}")
            positions.append(lines.index(token))
        if positions != sorted(positions):
            raise ValueError("Sort stage order differs")
        for stage in stages[1:]:
            matches = [line for line in lines if line.startswith(f"sort-callback {stage} ".encode())]
            match = re.fullmatch(rb"sort-callback [a-z-]+ entries=12 segments=([1-9][0-9]*)( (?:[0-9]+)){12}", matches[0]) if len(matches) == 1 else None
            if not match or sorted(map(int, matches[0].split()[4:])) != list(range(12)):
                raise ValueError(f"missing/invalid Sort callback {stage}")
            if not positions[0] < lines.index(matches[0]) < lines.index(f"sort-stage {stage}".encode()):
                raise ValueError("Sort callback ordering differs")

    if require_style_rename_stages:
        stages = ("numeric-repeat-12", "numeric-repeat-13", "raw-nul-before", "raw-nul-to12",
                  "raw-nul-to2", "shrink-post-empty", "shrink-before", "shrink-after")
        positions = []
        for stage in stages:
            token = f"style-rename-stage {stage}".encode()
            if lines.count(token) != 1:
                raise ValueError(f"missing/duplicate style rename stage {stage}")
            positions.append(lines.index(token))
        if positions != sorted(positions):
            raise ValueError("style rename stage order differs")
        for stage in ("numeric-original", "numeric-repeat-12", "numeric-repeat-13", "named-final",
                      "raw-nul-before", "raw-nul-to12", "raw-nul-to2", "shrink-before", "shrink-after"):
            matches = [line for line in lines if line.startswith(f"style-rename-segments {stage} ".encode())]
            match = re.fullmatch(rb"style-rename-segments [a-z0-9-]+ count=(\d+) animated=(\d+)", matches[0]) if len(matches) == 1 else None
            if not match or not 1 <= int(match[2]) <= int(match[1]):
                raise ValueError(f"missing/invalid style rename segment invariant {stage}")

    if require_named_stages:
        for stage in ("named-initial", "named-equal-duplicate", "named-conflict-collision",
                      "named-numeric-collision", "named-copy-replace", "named-copy-independent",
                      "named-copy-append", "named-self-clear", "named-empty"):
            if f"named-stage {stage}".encode() not in lines:
                raise ValueError(f"missing named style lifecycle stage {stage}")
        for observation in (() if cleanup else (b"saved-file ssa exists=1 bytes=", b"saved-file ass exists=1 bytes=",
                            b"saved-file srt exists=1 bytes=", b"saved-file srt-sidecar exists=1 bytes=",
                            b"saved-file ssa-sidecar exists=0", b"saved-file ass-sidecar exists=0",
                            b"saved-file srt-no-map exists=1 bytes=", b"saved-file srt-no-map-sidecar exists=0")):
            if not any(line.startswith(observation) for line in lines):
                raise ValueError(f"missing named style export observation {observation!r}")



def compare_traces(traces, pointer_bits, *, require_named_stages=True, require_identity_stages=True, require_interpretation_stages=True, require_layout_stages=True, require_metadata_stages=True, require_selected_copy_stages=True, require_style_rename_stages=True, require_sort_stages=True, require_base_empty_stages=True, require_lcid_stages=True, require_resolution_stages=True, require_layout_size_stages=True, require_conversion_stages=True, require_unknown_stages=True, require_add_style_stages=True, require_copy_styles_stages=True, require_text_plan_stages=True, require_preparation_stages=True, require_setstr_stages=True, require_open_order_stages=True, require_frame_search_stages=True, require_save_body_stages=True, require_save_sections_stages=True, cleanup=False):
    if len(traces) != 4:
        raise ValueError("two reference and two candidate executions required")
    for trace in traces:
        validate_trace(trace, pointer_bits, require_named_stages=require_named_stages, require_identity_stages=require_identity_stages, require_interpretation_stages=require_interpretation_stages, require_layout_stages=require_layout_stages, require_metadata_stages=require_metadata_stages, require_selected_copy_stages=require_selected_copy_stages, require_style_rename_stages=require_style_rename_stages, require_sort_stages=require_sort_stages, require_base_empty_stages=require_base_empty_stages, require_lcid_stages=require_lcid_stages, require_resolution_stages=require_resolution_stages, require_layout_size_stages=require_layout_size_stages, require_conversion_stages=require_conversion_stages, require_unknown_stages=require_unknown_stages, require_add_style_stages=require_add_style_stages, require_copy_styles_stages=require_copy_styles_stages, require_text_plan_stages=require_text_plan_stages, require_preparation_stages=require_preparation_stages, require_setstr_stages=require_setstr_stages, require_open_order_stages=require_open_order_stages, require_frame_search_stages=require_frame_search_stages, require_save_body_stages=require_save_body_stages, require_save_sections_stages=require_save_sections_stages, cleanup=cleanup)
    if any(trace != traces[0] for trace in traces[1:]):
        raise ValueError("native lifecycle traces differ or are nondeterministic")


def find_library(root, platform, name):
    paths = [p for p in (root / "bin").glob(f"lib_*/{platform}/{name}") if p.is_file()]
    if len(paths) != 1:
        raise ValueError(f"expected one built {platform}/{name} in {root}; found {paths}")
    return paths[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-root", type=Path, required=True)
    parser.add_argument("--candidate-root", type=Path, default=ROOT)
    parser.add_argument("--platform", choices=("Win32", "x64"), required=True)
    parser.add_argument("--document-storage", choices=("native", "rust-experimental"), required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("actual Windows ATL/MFC and built native libraries required")
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    report = {"schema": 1, "complete": False, "equal": False,
              "kind": "linked-native-document-lifecycle", "reference_commit": PIN,
              "platform": args.platform, "document_storage": args.document_storage,
              "probe_sha256": sha(SOURCE.read_bytes()),
              "runner_sha256": sha(Path(__file__).read_bytes()), "dialog_observer_sha256": sha((ROOT/"tools/compat/dialog_capture.py").read_bytes()), "builds": {},
              "environment": {key: os.environ.get(key) for key in
                              ("VCToolsVersion", "WindowsSDKVersion", "VSCMD_ARG_TGT_ARCH")},
              "limits": ["Bounded normal lifecycles and initialized metadata only",
                         "No allocator-failure, concurrent close, or dialog-reentry parity",
                         "Not a real Aegisub/player host test"]}
    report_path = out / "report.json"

    def save():
        report_path.write_text(json.dumps(report, indent=2) + "\n")

    def run(command, cwd, name, timeout):
        (out / f"{name}.command.json").write_text(json.dumps(command, indent=2) + "\n")
        try:
            result = subprocess.run(command, cwd=cwd, stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE, timeout=timeout)
        except subprocess.TimeoutExpired as error:
            (out / f"{name}.stdout").write_bytes(error.stdout or b"")
            (out / f"{name}.stderr").write_bytes(error.stderr or b"")
            raise RuntimeError(f"{name} timed out after {timeout}s; unresolved") from error
        (out / f"{name}.stdout").write_bytes(result.stdout)
        (out / f"{name}.stderr").write_bytes(result.stderr)
        if result.returncode:
            raise RuntimeError(f"{name} exited {result.returncode}; unresolved")
        return result

    save()
    try:
        roots = (args.reference_root.resolve(), args.candidate_root.resolve())
        commits = [git(root, "rev-parse", "HEAD") for root in roots]
        if commits[0] != PIN:
            raise ValueError("reference checkout is not the pinned commit")
        statuses = [git(root, "status", "--porcelain", "--untracked-files=no") for root in roots]
        if any(statuses):
            raise ValueError("tracked native checkout changes prevent committed provenance")
        report["candidate_commit"] = commits[1]
        target = "i686-pc-windows-msvc" if args.platform == "Win32" else "x86_64-pc-windows-msvc"
        bits = 32 if args.platform == "Win32" else 64
        traces = []
        dialog_observations = []
        for label, root in zip(("reference", "candidate"), roots):
            work = out / label
            work.mkdir()
            libraries = [find_library(root, args.platform, name) for name in LIBRARIES]
            defines = ["UNICODE", "_UNICODE", "NDEBUG", "WINVER=0x0601", "_WIN32_WINNT=0x0601",
                       "PSAPI_VERSION=1", "_USE_MATH_DEFINES", "_CRT_SECURE_NO_WARNINGS",
                       "_SCL_SECURE_NO_WARNINGS", "_AFX_NO_MFC_CONTROLS_IN_DIALOGS",
                       "WINDOWS", "_WINDOWS", "WIN32" if args.platform == "Win32" else "WIN64"]
            if label == "reference":
                defines.append("XY_DOCUMENT_PINNED_REFERENCE")
            else:
                defines.append("XY_VSFILTER_RUST_TEXT")
                if args.document_storage == "rust-experimental":
                    defines.append("XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL")
                libraries.append(root / "target" / target / "release/xy_vsfilter_ffi.lib")
            includes = [root / path for path in (
                "src/subtitles", "src/filters/transform/vsfilter", "src/filters/BaseClasses",
                "src/thirdparty/boost_lib", "src/thirdparty/log4cplus/include",
                "src/thirdparty/VirtualDub/h", "src/thirdparty", "include", "include/unrar")]
            exe = work / "document-native-lifecycle.exe"
            command = ["cl", "/nologo", "/Bv", "/O2", "/Oi", "/Ob2", "/fp:precise", "/EHsc",
                       "/std:c++14", "/MT", "/Gy", "/showIncludes"]
            command += ["/D" + define for define in defines]
            command += ["/I" + str(path) for path in includes]
            command += [str(SOURCE), "/Fo:" + str(work / "probe.obj"), "/Fe:" + str(exe),
                        "/link", "/SUBSYSTEM:CONSOLE", "/OPT:REF", "/OPT:ICF", "/VERBOSE:LIB",
                        "/LIBPATH:" + str(root / "lib")]
            command += [str(path) for path in libraries] + list(SYSTEM_LIBRARIES)
            build = {"library_sha256": {str(path): sha(path.read_bytes()) for path in libraries}}
            report["builds"][label] = build
            save()
            compiled = run(command, work, f"{label}-build", 180)
            build["binary_sha256"] = sha(exe.read_bytes())
            headers = {}
            for line in compiled.stdout.decode(errors="replace").splitlines():
                match = re.search(r"([A-Za-z]:\\.+?)\s*$", line)
                if match and Path(match.group(1)).is_file():
                    header = Path(match.group(1))
                    headers[str(header)] = sha(header.read_bytes())
            build["included_header_sha256"] = headers
            build["source_header_sha256"] = {
                Path(path).relative_to(root).as_posix(): digest for path, digest in headers.items()
                if Path(path).is_relative_to(root)
            }
            for repeat in (1, 2):
                name=f"{label}-trace-{repeat}"
                (out/f"{name}.command.json").write_text(json.dumps([str(exe)],indent=2)+"\n")
                result,dialogs=run_dialog_process([str(exe)],work,out/name,20)
                dialog_observations.append(dialogs)
                if result.stderr:
                    raise ValueError(f"{label} execution produced unexpected stderr")
                validate_trace(result.stdout, bits, cleanup=True)
                traces.append(result.stdout)
            if sha(exe.read_bytes()) != build["binary_sha256"]:
                raise ValueError("native probe executable changed during execution")
            for path, expected in build["library_sha256"].items():
                if sha(Path(path).read_bytes()) != expected:
                    raise ValueError("native library changed during execution")
            for path, expected in headers.items():
                if sha(Path(path).read_bytes()) != expected:
                    raise ValueError("included header changed during execution")
        compare_traces(traces, bits, cleanup=True)
        if any(dialogs!=dialog_observations[0] for dialogs in dialog_observations[1:]):
            raise ValueError("native dialog observations differ or are not repeatable")
        if [git(root, "rev-parse", "HEAD") for root in roots] != commits:
            raise ValueError("checkout commit changed during observation")
        if any(git(root, "status", "--porcelain", "--untracked-files=no") for root in roots):
            raise ValueError("tracked native source changed during observation")
        if sha(SOURCE.read_bytes()) != report["probe_sha256"] or sha(Path(__file__).read_bytes()) != report["runner_sha256"]:
            raise ValueError("probe source changed during observation")
        if sha((ROOT/"tools/compat/dialog_capture.py").read_bytes()) != report["dialog_observer_sha256"]:
            raise ValueError("dialog observer changed during observation")
        report.update(dialogs_equal=True, dialog_count=len(dialog_observations[0]), dialog_sha256=[sha(json.dumps(d,sort_keys=True).encode()) for d in dialog_observations], complete=True, equal=True, repeat_equal=True, stderr_empty=True, inputs_unchanged=True,
                      trace_sha256=[sha(trace) for trace in traces], trace_lines=len(traces[0].splitlines()))
    except Exception as error:
        report["unresolved"] = str(error)
        save()
        raise
    save()
    print(json.dumps({"output": str(out), "complete": True, "equal": True,
                      "platform": args.platform, "document_storage": args.document_storage}))


if __name__ == "__main__":
    main()
