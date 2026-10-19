#!/usr/bin/env python3
"""Generate/run Windows ATL/CRT primitives, facades and experimental Rust owner checks.

Run from an x86 or x64 VS developer prompt with ATL installed. --generate-only
works on macOS. No unsafe reference histories, network calls or production writes.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import shlex
import subprocess
import sys

# Historical core/primitive regression oracle, including APIs removed by cleanup.
# Current production compatibility is checked by document_native_lifecycle/CSRI.
PIN = "4911cacb102bc0cbd41449793e54473265b04e62"
ROOT = Path(__file__).resolve().parents[2]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def source(path):
    return subprocess.check_output(["git", "show", f"{PIN}:{path}"], cwd=ROOT, timeout=60)


def function(text, signature):
    start = text.index(signature)
    opening = text.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


def run_facade(out, generate_only):
    """Actual ATL candidate facade against pinned search bodies/direct storage."""
    out.mkdir(parents=True, exist_ok=True)
    report_path = out / "report.json"
    report = {"schema": 1, "complete": False, "reference_commit": PIN,
              "kind": "native-backed-facade-isolation", "rust_parity": False}
    def save():
        report_path.write_text(json.dumps(report, indent=2) + "\n")
    save()  # Invalidate stale success before any external command.
    runner = Path(__file__).resolve()
    report["runner_sha256"] = sha(runner.read_bytes())
    report["candidate_commit"] = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, timeout=60).decode().strip()
    candidate_paths = ["src/subtitles/STS.h", "src/subtitles/STS.cpp", "src/subtitles/RTS.cpp"]
    candidate_bytes = {p: (ROOT / p).read_bytes() for p in candidate_paths}
    pinned_bytes = {p: source(p) for p in candidate_paths}
    report["candidate_source_sha256"] = {p: sha(b) for p, b in candidate_bytes.items()}
    report["reference_source_sha256"] = {p: sha(b) for p, b in pinned_bytes.items()}
    def decode(b):
        return b.decode("utf-8-sig").replace("\r\n", "\n")
    header, current = decode(candidate_bytes[candidate_paths[0]]), decode(candidate_bytes[candidate_paths[1]])
    pinned = decode(pinned_bytes[candidate_paths[1]])
    entry = re.search(r"typedef struct\s*\{[^}]+\} STSEntry;", header)
    if not entry:
        raise RuntimeError("Current STSEntry extraction failed")
    segment = function(header, "class STSSegment") + ";"
    facade_signatures = [
        "size_t CSimpleTextSubtitle::GetEntryCount(",
        "size_t CSimpleTextSubtitle::GetSegmentCount(",
        "STSEntry CSimpleTextSubtitle::GetEntrySnapshot(",
        "REFERENCE_TIME CSimpleTextSubtitle::GetEntryEnd(",
        "int CSimpleTextSubtitle::GetEntryLayer(",
        "int CSimpleTextSubtitle::GetEntryReadOrder(",
        "CString CSimpleTextSubtitle::GetEntryEffect(",
        "bool CSimpleTextSubtitle::GetSegmentEntryCount(",
        "bool CSimpleTextSubtitle::FindNextSegment(",
        "bool CSimpleTextSubtitle::SearchSubs2Snapshot(",
        "bool CSimpleTextSubtitle::IsSegmentAnimated(",
        "void CSimpleTextSubtitle::OrSegmentAnimated("]
    search_signatures = [
        "const STSSegment* CSimpleTextSubtitle::SearchSubs(",
        "STSSegment* CSimpleTextSubtitle::SearchSubs2(",
        "REFERENCE_TIME CSimpleTextSubtitle::TranslateSegmentStart(",
        "REFERENCE_TIME CSimpleTextSubtitle::TranslateSegmentEnd("]
    candidate_functions = [function(current, sig) for sig in facade_signatures + search_signatures]
    reference_functions = [function(pinned, sig).replace("CSimpleTextSubtitle::", "PinnedReference::") for sig in search_signatures]
    def shell(name, functions):
        declarations = [body[:body.index("{")].replace(name + "::", "").strip() + ";" for body in functions]
        return ("class " + name + " { public:\n"
                "enum {TIME, FRAME}; int m_mode = TIME;\n"
                "CAtlArray<STSEntry> m_entries; CAtlArray<STSSegment> m_segments;\n"
                + "\n".join(declarations) + "\n};\n" + "\n".join(functions))
    include = ("// Current actual declarations and candidate facade; pinned search comparison.\n"
               "#define UNITS_FLOAT (10000000.0) // DirectShow adapter constant\n"
               + entry.group(0) + "\n" + segment + "\n"
               + shell("CSimpleTextSubtitle", candidate_functions) + "\n"
               + shell("PinnedReference", reference_functions) + "\n")
    generated = out / "document_facade_extracted.inc"
    generated.write_text(include, encoding="utf-8")
    local = ROOT / "tools/compat/document_facade_probe.cpp"
    cpp = out / local.name
    shutil.copyfile(local, cpp)
    report.update({"probe_sha256": sha(cpp.read_bytes()), "generated_sha256": sha(generated.read_bytes()),
                   "extracted_candidate_methods": facade_signatures + search_signatures,
                   "extracted_reference_methods": search_signatures,
                   "toolchain_status": "unfixed Windows environment; actual headers/compiler/libraries recorded",
                   "limits": ["TIME mode fixed; native non-Rust search branch only; trace logging disabled",
                              "minimal class shell, not renderer/host integration or complete class ABI",
                              "no allocation-failure or reentrant/concurrent mutation equivalence",
                              "schedule loops modeled from renderer; full GetSubtitle/ParseEffect not executed"]})
    save()
    if generate_only:
        report.update({"generated": True, "generation_only": True})
        save()
        return
    if os.name != "nt":
        raise SystemExit("Facade execution requires Windows")
    compiler = shutil.which("cl")
    if not compiler:
        raise SystemExit("Facade probe requires VS developer prompt with ATL")
    exe = out / "document_facade_probe.exe"
    cmd = [compiler, "/nologo", "/Bv", "/showIncludes", "/std:c++17", "/EHsc", "/O2", "/Oi", "/Ob2", "/MT",
           "/fp:precise", "/DNDEBUG", "/DUNICODE", "/D_UNICODE", "/D_CRT_SECURE_NO_WARNINGS",
           str(cpp), f"/Fe:{exe}", f"/Fo:{out / 'document_facade_probe.obj'}", "/link", "/VERBOSE:LIB"]
    report["build_command"] = cmd
    report["compiler_sha256"] = sha(Path(compiler).read_bytes())
    report["environment"] = {k: os.environ.get(k) for k in
                             ["VCToolsVersion", "VCToolsInstallDir", "WindowsSDKVersion", "WindowsSdkDir",
                              "VSCMD_ARG_TGT_ARCH", "VSCMD_ARG_HOST_ARCH", "INCLUDE", "LIB"]}
    save()
    try:
        built = subprocess.run(cmd, cwd=out, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
    except subprocess.TimeoutExpired as error:
        (out / "build.log").write_bytes(error.stdout or b"")
        report["unresolved_build"] = "timeout after 60 seconds"
        save()
        raise SystemExit("Facade build timeout; unresolved")
    (out / "build.log").write_bytes(built.stdout)
    report["build_exit_code"] = built.returncode
    headers, libraries = {}, {}
    build_text = built.stdout.decode(errors="replace")
    for line in build_text.splitlines():
        match = re.search(r"([A-Za-z]:\\.+?)\s*$", line)
        if match and Path(match.group(1)).is_file():
            headers[match.group(1)] = sha(Path(match.group(1)).read_bytes())
    for match in re.finditer(r"([A-Za-z]:\\[^\r\n]*?\.lib)(?=[:\s(]|$)", build_text, re.I):
        if Path(match.group(1)).is_file():
            libraries[match.group(1)] = sha(Path(match.group(1)).read_bytes())
    report["included_header_sha256"] = headers
    report["linker_observed_library_sha256"] = libraries
    report["library_scope"] = "Paths observed in /VERBOSE:LIB, including searched libraries, not proof all contributed code."
    save()
    if built.returncode:
        raise SystemExit("Facade build failed; see facade/build.log")
    report["binary_sha256"] = sha(exe.read_bytes())
    save()
    traces, errors = [], []
    for run in (1, 2):
        try:
            result = subprocess.run([str(exe)], cwd=out, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
        except subprocess.TimeoutExpired as error:
            (out / f"trace-{run}.txt").write_bytes(error.stdout or b"")
            (out / f"stderr-{run}.txt").write_bytes(error.stderr or b"")
            report["unresolved_execution"] = {"run": run, "reason": "timeout"}
            save()
            raise SystemExit("Facade timeout; unresolved")
        (out / f"trace-{run}.txt").write_bytes(result.stdout)
        (out / f"stderr-{run}.txt").write_bytes(result.stderr)
        lines = result.stdout.splitlines()
        if (result.returncode or result.stderr or not lines
                or not lines[0].startswith(b"provenance ")
                or lines[-1] != b"document-facade-v1 complete"
                or not any(line.startswith(b"checks ") for line in lines)
                or not any(line == b"isolation complete" for line in lines)):
            report["unresolved_execution"] = {"run": run, "exit_code": result.returncode,
                                              "reason": "failed assertions/stderr/incomplete trace"}
            save()
            raise SystemExit("Facade observation failed or incomplete; unresolved")
        traces.append(result.stdout)
        errors.append(result.stderr)
    report.update({"trace_sha256": [sha(t) for t in traces], "stderr_sha256": [sha(e) for e in errors],
                   "repeat_equal": traces[0] == traces[1] and errors[0] == errors[1],
                   "trace_lines": len(traces[0].splitlines())})
    stable = (sha(local.read_bytes()) == report["probe_sha256"]
              and sha(cpp.read_bytes()) == report["probe_sha256"]
              and sha(generated.read_bytes()) == report["generated_sha256"]
              and sha(runner.read_bytes()) == report["runner_sha256"]
              and sha(exe.read_bytes()) == report["binary_sha256"]
              and all(sha((ROOT / p).read_bytes()) == h for p, h in report["candidate_source_sha256"].items())
              and subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, timeout=60).decode().strip() == report["candidate_commit"])
    report["candidate_stable"] = stable
    if not stable or not report["repeat_equal"]:
        save()
        raise SystemExit("Facade source changed or repeated traces differ; unresolved")
    report["complete"] = True
    save()


def owner_style_declarations(pinned_header, current_header):
    """Exact real value declarations; never a layout-compatible surrogate."""
    declarations = []
    for signature in ("struct STSStyleBase", "struct STSStyle: public STSStyleBase"):
        declaration = function(pinned_header, signature) + ";"
        if function(current_header, signature) + ";" != declaration:
            raise RuntimeError("Owner probe requires unchanged " + signature)
        declarations.append(declaration)
    return "\n".join(declarations)


def owner_style_dependency(pinned, current):
    # Check all native template code after excluding ONLY the two exact adapter
    # selection seams. FwRect now selects the current Rust adapter on BOTH sides
    # of this storage-only harness; its behavior is independently checked by
    # rect_intern_native against the actual cleanup class, not by this harness.
    alias='typedef XyFlyWeight<CStringW, 16*1024, CStringElementTraits<CStringW>> XyFwStringW;'
    seam='#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL\n#include "xy_rust_string_flyweight.h"\ntypedef XyRust::StringFlyweight XyFwStringW;\n#else\n'+alias+'\n#endif'
    if current.count(seam)==1:
        current=current.replace(seam,alias)
    alias='typedef XyInterned<CRect> FwRect;'
    seam='#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL\n#include "xy_rust_rect_intern.h"\ntypedef XyRust::RectIntern FwRect;\n#else\n'+alias+'\n#endif'
    if current.count(seam)==1:
        current=current.replace(seam,alias)
    if pinned!=current:
        raise RuntimeError("Owner probe requires unchanged cleanup style dependency")


def run_owner(out, generate_only):
    """Real ATL records + exact experimental wrapper versus pinned storage tails."""
    out.mkdir(parents=True, exist_ok=True)
    report_path = out / "report.json"
    report = {"schema": 1, "complete": False, "reference_commit": PIN,
              "kind": "experimental-actual-ATL-Rust-owner-isolation", "rust_parity": False}
    def save():
        report_path.write_text(json.dumps(report, indent=2) + "\n")
    save()
    runner = Path(__file__).resolve()
    paths = ["src/subtitles/STS.h", "src/subtitles/STS.cpp", "src/subtitles/RustDocumentOwner.h",
             "include/xy_rust_document.h", "include/xy_rust_string_flyweight.h", "tools/compat/document_owner_probe.cpp",
             "include/xy_rust_rect_intern.h", "src/subtitles/RustRectIntern.inl",
             "src/subtitles/flyweight_base_types.h", "src/subtitles/mru_cache.h",
             "Cargo.toml", "Cargo.lock", "rust/compat/Cargo.toml", "rust/ffi/Cargo.toml"]
    paths += [str(p.relative_to(ROOT)) for parent in (ROOT / "rust/compat/src", ROOT / "rust/ffi/src")
              for p in parent.rglob("*.rs")]
    inputs = {path: (ROOT / path).read_bytes() for path in paths}
    pinned_paths = ["src/subtitles/STS.h", "src/subtitles/STS.cpp", "src/subtitles/RTS.cpp",
                    "src/subtitles/flyweight_base_types.h", "src/subtitles/mru_cache.h"]
    pinned_bytes = {path: source(path) for path in pinned_paths}
    report.update(runner_sha256=sha(runner.read_bytes()),
                  candidate_commit=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, timeout=60).decode().strip(),
                  candidate_source_sha256={p: sha(b) for p, b in inputs.items()},
                  reference_source_sha256={p: sha(b) for p, b in pinned_bytes.items()})
    def decode(value):
        return value.decode("utf-8-sig").replace("\r\n", "\n")
    pinned_header = decode(pinned_bytes[pinned_paths[0]])
    pinned = decode(pinned_bytes[pinned_paths[1]])
    current = decode(inputs["src/subtitles/STS.cpp"])
    entry_pattern = r"typedef struct\s*\{[^}]+\} STSEntry;"
    entry = re.search(entry_pattern, pinned_header).group(0)
    if re.search(entry_pattern, decode(inputs["src/subtitles/STS.h"])).group(0) != entry:
        raise RuntimeError("Experimental owner probe requires unchanged STSEntry value declaration")
    segment = function(pinned_header, "class STSSegment") + ";"
    if function(decode(inputs["src/subtitles/STS.h"]), "class STSSegment") + ";" != segment:
        raise RuntimeError("Experimental owner probe requires unchanged STSSegment value declaration")
    styles = owner_style_declarations(pinned_header, decode(inputs["src/subtitles/STS.h"]))
    report["style_dependency_scope"] = {
        "declarations": ["STSStyleBase", "STSStyle"],
        "declaration_sha256": sha(styles.encode()),
        "fwrect_header": "src/subtitles/flyweight_base_types.h",
        "cstring_type": "Actual MFC CString selected by afx.h before ATL headers; prior isolated ATL-alias evidence is not relabeled",
        "runtime": "Static MFC and static CRT (/MT, no _AFXDLL); actual included SDK/repository headers captured by /showIncludes",
        "limits": "Compile dependencies only; no new style lifecycle or full-class parity claim",
    }
    # Both storage algorithms use the current value adapters. Keep the native
    # template comparison separate from the changed rectangle owner, and do not
    # relabel historical results as current rectangle-interner parity.
    adapter_pin = json.loads((ROOT / "tools/compat/reference.json").read_text())["commit"]
    report["style_adapter_commit"] = report["candidate_commit"]
    report["style_adapter_source_sha256"] = {p:sha(inputs[p]) for p in ("src/subtitles/flyweight_base_types.h","src/subtitles/mru_cache.h","include/xy_rust_rect_intern.h","src/subtitles/RustRectIntern.inl")}
    report["native_template_reference_commit"] = adapter_pin
    report["native_template_reference_source_sha256"] = {}
    for dependency in ("src/subtitles/flyweight_base_types.h", "src/subtitles/mru_cache.h"):
        adapter = subprocess.check_output(["git", "show", f"{adapter_pin}:{dependency}"], cwd=ROOT, timeout=60)
        owner_style_dependency(decode(adapter),decode(inputs[dependency]))
        report["native_template_reference_source_sha256"][dependency] = sha(adapter)
    report["style_dependency_scope"]["excluded_alias_changes"] = "Exact XyFwStringW and FwRect selections only; remaining native template/cache code compared byte-for-byte after newline normalization"
    report["style_dependency_scope"]["rectangle_adapter"] = "Current Rust-backed FwRect on both storage-algorithm sides; not a rectangle comparison. Independent cleanup oracle: rect_intern_native.py"
    (out / "STS.h").write_bytes(("#pragma once\n#include \"flyweight_base_types.h\"\n" + styles + "\n" + entry + "\n" + segment + "\n").encode())
    shutil.copyfile(ROOT / "src/subtitles/RustDocumentOwner.h", out / "RustDocumentOwner.h")
    probe = decode(inputs["tools/compat/document_owner_probe.cpp"])
    if probe.count("#include <windows.h>") != 1:
        raise RuntimeError("Owner probe MFC include seam changed")
    probe = probe.replace("#include <windows.h>", "#include <afx.h>\n#include <afxwin.h>")
    seam='#include "RustDocumentOwner.h"'
    if probe.count(seam)!=1:raise RuntimeError("Owner rectangle adapter include seam changed")
    probe=probe.replace(seam,seam+'\n#include "RustRectIntern.inl"')
    (out / "document_owner_probe.cpp").write_bytes(probe.encode())
    reference = ["""class PinnedOwner { public:
 enum {TIME,FRAME}; int m_mode=TIME,m_subtitleType=0; unsigned callbacks=0;
 CAtlArray<STSEntry> m_entries; CAtlArray<STSSegment> m_segments;
 void OnChanged(){++callbacks;} void LogSegments(const CAtlArray<STSSegment>&){}
 void AddPrepared(STSEntry sub,uint32_t mode);void EntryOnly(STSEntry sub);
 void CreateSegments();void RemoveAllEntries();void ClearStorage();void CopyStorage(PinnedOwner& sts);
 void SetText(int i,const CStringW& str,int flag);void RenameStyle(const CString& name,const CString& name3);
 void AnimationOr(int i,bool animated);void CorrectLastEnd(REFERENCE_TIME bound);
 void ReplaceTimes(const std::vector<std::pair<int64_t,int64_t>>& times);void Sort(bool fRestoreReadorder);
 int SearchSub(REFERENCE_TIME,double);const STSSegment* SearchSubs(REFERENCE_TIME,double,int*,int*);
 STSSegment* SearchSubs2(REFERENCE_TIME,double,int*,int*);
 REFERENCE_TIME TranslateStart(int,double);REFERENCE_TIME TranslateSegmentStart(int,double);REFERENCE_TIME TranslateSegmentEnd(int,double);
};""", function(pinned, "static bool SegmentCompStart(")]
    add = function(pinned, "void CSimpleTextSubtitle::Add(")
    order_start = add.index("    sub.readorder  =")
    reference += ["void PinnedOwner::AddPrepared(STSEntry sub,uint32_t mode){m_subtitleType=mode==2?EXTVTT:0;if(mode==1){EntryOnly(sub);return;}auto start=sub.start,end=sub.end;int readorder=sub.readorder;\n"
                  + add[order_start:add.index(";", order_start)+1] + "\n"
                  + add[add.index("    int n = (int)m_entries.GetCount();"):]]
    append = function(pinned, "void CSimpleTextSubtitle::AddSTSEntryOnly(")
    reference += ["void PinnedOwner::EntryOnly(STSEntry sub){int readorder=sub.readorder;\n" + append[append.index("    sub.readorder  ="):]]
    complete_signatures = ["void CSimpleTextSubtitle::CreateSegments()", "void CSimpleTextSubtitle::RemoveAllEntries()",
                          "int CSimpleTextSubtitle::SearchSub(", "const STSSegment* CSimpleTextSubtitle::SearchSubs(",
                          "STSSegment* CSimpleTextSubtitle::SearchSubs2(", "REFERENCE_TIME CSimpleTextSubtitle::TranslateStart(",
                          "REFERENCE_TIME CSimpleTextSubtitle::TranslateSegmentStart(", "REFERENCE_TIME CSimpleTextSubtitle::TranslateSegmentEnd(",
                          "template <typename T> int SGN", "static int comp1(", "static int comp2(", "void CSimpleTextSubtitle::Sort("]
    reference += [function(pinned, sig).replace("CSimpleTextSubtitle::", "PinnedOwner::") for sig in complete_signatures]
    empty = function(pinned, "void CSimpleTextSubtitle::Empty()")
    copy = function(pinned, "void CSimpleTextSubtitle::Copy(")
    reference += ["void PinnedOwner::ClearStorage(){\n" + empty[empty.index("    m_segments.RemoveAll();"):],
                  "void PinnedOwner::CopyStorage(PinnedOwner& sts){\n" + copy[copy.index("    m_segments.Copy(sts.m_segments);"):]]
    prepared_assignment = "stse.str = str;"
    flag_assignment = "stse.fUnicode = fUnicode;"
    if prepared_assignment not in function(pinned, "void CSimpleTextSubtitle::SetStr(") or flag_assignment not in function(pinned, "void CSimpleTextSubtitle::ConvertUnicode("):
        raise RuntimeError("Pinned prepared-text assignment extraction failed")
    reference += ["void PinnedOwner::SetText(int i,const CStringW& str,int flag){STSEntry& stse=m_entries.GetAt(i);" + prepared_assignment
                  + "if(flag>=0){bool fUnicode=flag!=0;" + flag_assignment + "}}"]
    style = function(pinned, "void CSimpleTextSubtitle::AddStyle(")
    rename_start = style.index("        for (size_t j = 0, count = m_entries.GetCount();")
    reference += ["void PinnedOwner::RenameStyle(const CString& name,const CString& name3){\n" + function(style[rename_start:], "for (size_t j = 0, count = m_entries.GetCount();") + "\n}"]
    animation = "stss->animated |= s->m_fAnimated2;"
    correction = "stse.end = min(stse.end, MS2RT(start));"
    if animation not in decode(pinned_bytes[pinned_paths[2]]) or correction not in pinned:
        raise RuntimeError("Pinned animation/correction extraction failed")
    reference += ["void PinnedOwner::AnimationOr(int i,bool animated){STSSegment* stss=&m_segments.GetAt(i);struct Value{bool m_fAnimated2;} value{animated};Value* s=&value;" + animation + "}",
                  "void PinnedOwner::CorrectLastEnd(REFERENCE_TIME bound){if(m_entries.GetCount()){STSEntry& stse=m_entries[m_entries.GetCount()-1];" + correction.replace("MS2RT(start)", "bound") + "}}"]
    converted = function(pinned, "void CSimpleTextSubtitle::ConvertToTimeBased(")
    loop = function(converted, "for(int i = 0, j = m_entries.GetCount();")
    loop = loop.replace("std::llround(stse.start * UNITS_FLOAT / fps)", "times[i].first").replace("std::llround(stse.end * UNITS_FLOAT / fps)", "times[i].second")
    if "llround" in loop:
        raise RuntimeError("Pinned conversion-assignment extraction failed")
    reference += ["void PinnedOwner::ReplaceTimes(const std::vector<std::pair<int64_t,int64_t>>& times){if(times.size()!=m_entries.GetCount())throw std::runtime_error(\"time count\");" + loop + "CreateSegments();}"]
    facade_signatures = ["size_t CSimpleTextSubtitle::GetSegmentCount(", "REFERENCE_TIME CSimpleTextSubtitle::TranslateSegmentStart(",
                         "REFERENCE_TIME CSimpleTextSubtitle::TranslateSegmentEnd(", "bool CSimpleTextSubtitle::FindNextSegment(",
                         "bool CSimpleTextSubtitle::SearchSubs2Snapshot("]
    bodies = [function(current, sig).replace("CSimpleTextSubtitle::", "ExperimentalFacade::") for sig in facade_signatures]
    declarations = [body[:body.index("{")].replace("ExperimentalFacade::", "").strip() + ";" for body in bodies]
    reference += ["class ExperimentalFacade { public:enum{TIME,FRAME};int m_mode=TIME;int GetDocumentMode() const{return m_mode;}XyRust::DocumentOwner* m_document;explicit ExperimentalFacade(XyRust::DocumentOwner& owner):m_document(&owner){}\n" + "\n".join(declarations) + "\n};\n" + "\n".join(bodies)]
    generated = out / "document_owner_reference.inc"
    generated.write_bytes(("\n\n".join(reference) + "\n").encode())
    report.update(generated_sha256={p: sha((out / p).read_bytes()) for p in
                                   ["STS.h", "RustDocumentOwner.h", "document_owner_probe.cpp", generated.name]},
                  complete_reference_methods=complete_signatures,
                  extracted_experimental_facade_methods=facade_signatures,
                  seams=["Add/AddSTSEntryOnly from initialized record/readorder assignment; native preparation bypassed",
                         "Copy storage tail and Empty arrays tail only; unchanged style declarations and current Rust FwRect adapter on both sides; native style/metadata lifecycle and rectangle interner comparison excluded",
                         "SetStr prepared assignment and optional ConvertUnicode flag assignment only",
                         "Actual AddStyle entry-rename loop; style map/duplicate naming excluded",
                         "RTS animation OR assignment and MicroDVD correction with precomputed bound",
                         "Native conversion assignment loop with caller-precomputed times, then rebuild",
                         "Full native Sort selects a permutation; owner applies indexes and rebuilds"],
                  limits=["Actual wrapper and ATL values, minimal pinned-owner/current-facade shells; not whole class ABI or renderer integration",
                          "TIME queries and bounded FRAME translation remain native CRT on both sides; no numeric port claim",
                          "No callback/reentry/concurrency/allocation-failure parity; destructor failure remains experimental unresolved contract",
                          "Known unresolved WebVTT end and stale-index Add histories are not executed"],
                  adapter_constants={"UNITS_FLOAT": 10000000.0},
                  toolchain_status="Recorded Windows headers/static CRT/compiler environment, not a frozen reference build")
    save()
    if generate_only:
        report.update(generated=True, generation_only=True)
        save()
        return
    if os.name != "nt":
        raise SystemExit("Actual document owner execution requires Windows ATL")
    target = {"x86": "i686-pc-windows-msvc", "x64": "x86_64-pc-windows-msvc"}.get(os.environ.get("VSCMD_ARG_TGT_ARCH"))
    compiler = shutil.which("cl")
    if not target or not compiler:
        raise SystemExit("Owner probe requires an x86/x64 VS developer prompt")
    environment = os.environ.copy()
    environment.pop("CARGO_ENCODED_RUSTFLAGS", None)
    environment["RUSTFLAGS"] = "-C target-feature=+crt-static"
    cargo = ["rustup", "run", "1.85.0", "cargo", "rustc", "--locked", "--offline", "--release",
             "-p", "xy-vsfilter-ffi", "--features", "native-runtime", "--target", target,
             "--target-dir", str(ROOT / "target"), "--", "--print", "native-static-libs"]
    report.update(rust_target=target, cargo_command=cargo, rustflags=environment["RUSTFLAGS"],
                  compiler_sha256=sha(Path(compiler).read_bytes()),
                  environment={k: os.environ.get(k) for k in ["VCToolsVersion", "VCToolsInstallDir", "WindowsSDKVersion", "WindowsSdkDir",
                                                              "VSCMD_ARG_TGT_ARCH", "VSCMD_ARG_HOST_ARCH", "INCLUDE", "LIB"]})
    save()
    built_rust = subprocess.run(cargo, cwd=ROOT, env=environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
    (out / "rust-build.log").write_bytes(built_rust.stdout + built_rust.stderr)
    match = re.search(r"native-static-libs: (.+)", built_rust.stderr.decode(errors="replace"))
    if built_rust.returncode or not match:
        report["unresolved_build"] = {"kind": "Rust static library", "exit_code": built_rust.returncode}
        save()
        raise SystemExit("Owner Rust build failed or link dependencies missing")
    library = ROOT / "target" / target / "release/xy_vsfilter_ffi.lib"
    report["static_library_sha256"] = sha(library.read_bytes())
    exe = out / "document_owner_probe.exe"
    command = [compiler, "/nologo", "/Bv", "/showIncludes", "/std:c++17", "/EHsc", "/O2", "/Oi", "/Ob2", "/MT", "/fp:precise",
               "/DNDEBUG", "/DUNICODE", "/D_UNICODE", "/DNOMINMAX", "/DWINVER=0x0601", "/D_WIN32_WINNT=0x0601",
               "/D_ATL_CSTRING_EXPLICIT_CONSTRUCTORS",
               "/DXY_VSFILTER_RUST_TEXT", "/DXY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL", "/D_CRT_SECURE_NO_WARNINGS",
               "/I" + str(ROOT / "include"), "/I" + str(ROOT / "src/subtitles"),
               "/I" + str(ROOT / "src/thirdparty/boost_lib"), str(out / "document_owner_probe.cpp"), str(library), *shlex.split(match.group(1)),
               "/Fe:" + str(exe), "/Fo:" + str(out / "document_owner_probe.obj"), "/link", "/VERBOSE:LIB"]
    report["build_command"] = command
    save()
    compiled = subprocess.run(command, cwd=out, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
    (out / "build.log").write_bytes(compiled.stdout)
    report["build_exit_code"] = compiled.returncode
    headers, libraries = {}, {}
    text = compiled.stdout.decode(errors="replace")
    for line in text.splitlines():
        match_header = re.search(r"([A-Za-z]:\\.+?)\s*$", line)
        if match_header and Path(match_header.group(1)).is_file():
            headers[match_header.group(1)] = sha(Path(match_header.group(1)).read_bytes())
    for match_library in re.finditer(r"([A-Za-z]:\\[^\r\n]*?\.lib)(?=[:\s(]|$)", text, re.I):
        if Path(match_library.group(1)).is_file():
            libraries[match_library.group(1)] = sha(Path(match_library.group(1)).read_bytes())
    report.update(included_header_sha256=headers, linker_observed_library_sha256=libraries,
                  library_scope="Paths observed in /VERBOSE:LIB, including searched libraries; not proof all contributed code")
    save()
    if compiled.returncode:
        raise SystemExit("Actual owner compile failed; see owner/build.log")
    report["binary_sha256"] = sha(exe.read_bytes())
    traces = []
    for run in (1, 2):
        try:
            result = subprocess.run([str(exe)], cwd=out, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
        except subprocess.TimeoutExpired as error:
            (out / f"trace-{run}.txt").write_bytes(error.stdout or b"")
            (out / f"stderr-{run}.txt").write_bytes(error.stderr or b"")
            report["unresolved_execution"] = {"run": run, "reason": "timeout", "seconds": 30}
            save()
            raise SystemExit("Actual owner probe timeout; unresolved")
        (out / f"trace-{run}.txt").write_bytes(result.stdout)
        (out / f"stderr-{run}.txt").write_bytes(result.stderr)
        lines = result.stdout.splitlines()
        expected_pointer = b"pointer=4" if target.startswith("i686") else b"pointer=8"
        if (result.returncode or result.stderr or not lines or not lines[0].startswith(b"provenance ")
                or expected_pointer not in lines[0] or lines[-1] != b"document-owner-v1 complete"
                or b"owner-snapshot-isolation complete" not in lines
                or not any(line.startswith(b"checks ") and b"negative-control=passed" in line for line in lines)
                or any(not any(line.startswith(prefix) for line in lines) for prefix in
                       (b"entry ", b"segment ", b"query ", b"facade-alias ", b"facade-frame-alias "))):
            report["unresolved_execution"] = {"run": run, "exit_code": result.returncode, "reason": "assertion/stderr/incomplete/wrong architecture"}
            save()
            raise SystemExit("Actual owner observation failed; unresolved")
        traces.append(result.stdout)
    report.update(trace_sha256=[sha(t) for t in traces], repeat_equal=traces[0] == traces[1],
                  trace_lines=len(traces[0].splitlines()), stderr_empty=True)
    stable = (sha(runner.read_bytes()) == report["runner_sha256"]
              and all(sha((ROOT / p).read_bytes()) == value for p, value in report["candidate_source_sha256"].items())
              and all(sha((out / p).read_bytes()) == value for p, value in report["generated_sha256"].items())
              and all(sha(Path(p).read_bytes()) == value for p, value in headers.items())
              and sha(exe.read_bytes()) == report["binary_sha256"]
              and sha(library.read_bytes()) == report["static_library_sha256"]
              and subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, timeout=60).decode().strip() == report["candidate_commit"])
    report["candidate_stable"] = stable
    if not stable or not report["repeat_equal"]:
        save()
        raise SystemExit("Owner source/binary changed or repeat differs; unresolved")
    report.update(complete=True, rust_parity=True)
    save()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--generate-only", action="store_true")
    args = parser.parse_args()
    out = args.output_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)
    # Invalidate any previous successful report before git/compiler execution.
    (out / "report.json").write_text(json.dumps({"schema": 1, "complete": False, "reference_commit": PIN}, indent=2) + "\n")
    for child in ("facade", "owner"):
        (out / child).mkdir(parents=True, exist_ok=True)
        (out / child / "report.json").write_text(json.dumps({"schema": 1, "complete": False, "reference_commit": PIN}, indent=2) + "\n")
    runner_path = Path(__file__).resolve()
    runner_hash = sha(runner_path.read_bytes())
    candidate_commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, timeout=60).decode().strip()
    paths = ["src/subtitles/STS.cpp", "src/subtitles/RTS.cpp", "src/subtitles/STS.h",
             "src/dsutil/DSUtil.h", "src/common.props"]
    sources = {p: source(p) for p in paths}
    sts = sources[paths[0]].decode("utf-8-sig").replace("\r\n", "\n")
    rts = sources[paths[1]].decode("utf-8-sig").replace("\r\n", "\n")
    macros = sources[paths[3]].decode("utf-8-sig").replace("\r\n", "\n")
    entry_match = re.search(r"typedef struct\s*\{[^}]+\} STSEntry;", sources[paths[2]].decode("utf-8-sig").replace("\r\n", "\n"))
    if not entry_match:
        raise RuntimeError("Pinned STSEntry extraction failed")
    entry_declaration = entry_match.group(0)
    fragments = [entry_declaration, function(sts, "template <typename T> int SGN"),
                 function(sts, "static int comp1("),
                 function(sts, "static int comp2("),
                 function(rts, "static int lscomp(")]
    expressions = []
    for name in ("ConvertToTimeBased", "ConvertToFrameBased"):
        body = function(sts, f"void CSimpleTextSubtitle::{name}(")
        match = re.search(r"stse\.start = std::llround\(([^;]+)\);", body)
        if not match:
            raise RuntimeError(f"Pinned extraction failed: {name}")
        expressions.append(match.group(1).replace("stse.start", "input"))
    macro = re.search(r"^#define UNITS_FLOAT\s+[^\r\n]+", macros, re.M).group(0)
    # UNITS is DirectShow's 10^7 ticks/second constant, external to this git
    # tree; identify the adapter explicitly instead of claiming an extraction.
    include = "// Exact pinned comparator bodies and conversion expressions.\n"
    include += macro + "\nstatic constexpr long long UNITS = 10000000; // DirectShow adapter constant\n"
    include += "\n\n".join(fragments)
    include += "\nstatic double extracted_value(int mode, long long input, double fps) {\n"
    include += f"return mode == 0 ? ({expressions[0]}) : ({expressions[1]});\n}}\n"
    include += "static long long extracted_round(int mode, long long input, double fps) {\n"
    include += f"return mode == 0 ? std::llround({expressions[0]}) : std::llround({expressions[1]});\n}}\n"
    generated = out / "document_windows_extracted.inc"
    generated.write_text(include, encoding="utf-8")
    local = ROOT / "tools/compat/document_windows_probe.cpp"
    cpp = out / local.name
    shutil.copyfile(local, cpp)
    report = {
        "schema": 1, "complete": False, "candidate_commit": candidate_commit,
        "runner_sha256": runner_hash, "reference_commit": PIN, "kind": "reference-primitive-observations-only",
        "rust_parity": False, "source_sha256": {p: sha(v) for p, v in sources.items()},
        "probe_sha256": sha(cpp.read_bytes()), "generated_sha256": sha(generated.read_bytes()),
        "extracted_functions": ["SGN", "comp1", "comp2", "lscomp"],
        "extracted_types": ["STSEntry"],
        "extracted_conversion_expressions": expressions,
        "adapter_constants": {"UNITS": 10000000},
        "toolchain_status": "unfixed-environment observation baseline; compiler/SDK/ATL/static CRT are recorded, not pinned",
        "build_basis": "pinned common.props release /O2 /Oi /Ob2 /MT; /fp:precise explicit MSVC default (no pinned override found)",
        "limits": ["native nontrivial CString-bearing qsort is observed, not ported to Rust",
                   "full document Copy/style-map pointer ownership not probed; STSStyle pointees are identity-only surrogate objects", "comparator overflow unresolved",
                   "invalid fps and out-of-range llround unresolved", "allocation failure unresolved",
                   "no Windows execution established by generation-only mode"],
        "host": platform.platform(), "python": sys.version,
    }
    if not args.generate_only:
        if os.name != "nt":
            parser.error("Execution requires Windows; use --generate-only elsewhere")
        compiler = shutil.which("cl")
        if not compiler:
            parser.error("cl missing; use a VS developer prompt with ATL installed")
        exe = out / "document_windows_probe.exe"
        cmd = [compiler, "/nologo", "/Bv", "/showIncludes", "/std:c++17", "/EHsc", "/O2", "/Oi", "/Ob2",
               "/MT", "/fp:precise", "/DNDEBUG", "/DUNICODE", "/D_UNICODE", "/D_CRT_SECURE_NO_WARNINGS",
               str(cpp), f"/Fe:{exe}", f"/Fo:{out / 'document_windows_probe.obj'}", "/link", "/VERBOSE:LIB"]
        report["build_command"] = cmd
        try:
            built = subprocess.run(cmd, cwd=out, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
        except subprocess.TimeoutExpired as error:
            (out / "build.log").write_bytes(error.stdout or b"")
            report["unresolved_build"] = "timeout after 60 seconds"
            (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
            raise SystemExit("Reference probe build timeout; unresolved")
        (out / "build.log").write_bytes(built.stdout)
        report["build_command"] = cmd
        report["compiler_sha256"] = sha(Path(compiler).read_bytes())
        report["environment"] = {k: os.environ.get(k) for k in
                                 ["VCToolsVersion", "VCToolsInstallDir", "WindowsSDKVersion", "WindowsSdkDir",
                                  "VSCMD_ARG_TGT_ARCH", "VSCMD_ARG_HOST_ARCH", "INCLUDE", "LIB"]}
        # /showIncludes supplies the actual selected ATL/SDK/CRT headers, not an
        # assumed install version. Keep full paths and content hashes.
        headers = {}
        for line in built.stdout.decode(errors="replace").splitlines():
            match = re.search(r"([A-Za-z]:\\.+?)\s*$", line)
            if match:
                path = Path(match.group(1))
                if path.is_file():
                    headers[str(path)] = sha(path.read_bytes())
        report["included_header_sha256"] = headers
        libraries = {}
        for match in re.finditer(r"([A-Za-z]:\\[^\r\n]*?\.lib)(?=[:\s(]|$)", built.stdout.decode(errors="replace"), re.I):
            path = Path(match.group(1))
            if path.is_file():
                libraries[str(path)] = sha(path.read_bytes())
        report["linker_observed_library_sha256"] = libraries
        report["library_scope"] = "Library paths observed in /VERBOSE:LIB output, including searched libraries; not proof every archived library contributed code."
        report["build_exit_code"] = built.returncode
        if built.returncode:
            (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
            raise SystemExit("Windows probe build failed; see build.log")
        report["binary_sha256"] = sha(exe.read_bytes())
        traces = []
        errors = []
        for run in (1, 2):
            try:
                result = subprocess.run([str(exe)], cwd=out, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
            except subprocess.TimeoutExpired as error:
                (out / f"trace-{run}.txt").write_bytes(error.stdout or b"")
                (out / f"stderr-{run}.txt").write_bytes(error.stderr or b"")
                report["unresolved_execution"] = {"run": run, "reason": "timeout", "seconds": 60}
                (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
                raise SystemExit("Reference probe timeout; unresolved, no parity claim")
            (out / f"trace-{run}.txt").write_bytes(result.stdout)
            (out / f"stderr-{run}.txt").write_bytes(result.stderr)
            if result.returncode:
                report["unresolved_execution"] = {"run": run, "exit_code": result.returncode}
                (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
                raise SystemExit(f"Reference probe unresolved: exit {result.returncode}; no parity claim")
            lines = result.stdout.splitlines()
            required = (b"map ", b"cstring ", b"cstring-assignment ", b"cstring-matrix ",
                        b"sort ", b"lsort ", b"numeric ", b"round ")
            if (not lines or not lines[0].startswith(b"provenance ")
                    or lines[-1] != b"document-primitives-v1 complete"
                    or lines.count(b"cstring-matrix-summary 256 16") != 1
                    or sum(line.startswith(b"cstring-matrix ") for line in lines) != 256
                    or sum(line.startswith(b"cstring-assignment ") for line in lines) != 16
                    or any(not any(line.startswith(prefix) for line in lines) for prefix in required)):
                report["unresolved_execution"] = {"run": run, "reason": "missing required trace section or completion marker"}
                (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
                raise SystemExit("Reference probe trace incomplete; unresolved")
            traces.append(result.stdout)
            errors.append(result.stderr)
        report["stderr_sha256"] = [sha(e) for e in errors]
        report["stderr_repeat_equal"] = errors[0] == errors[1]
        report["trace_sha256"] = [sha(t) for t in traces]
        report["repeat_equal"] = traces[0] == traces[1]
        report["trace_lines"] = len(traces[0].splitlines())
        if not report["repeat_equal"] or not report["stderr_repeat_equal"]:
            (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
            raise SystemExit("Reference traces nondeterministic; unresolved")
        stable = (sha(local.read_bytes()) == report["probe_sha256"]
                  and sha(cpp.read_bytes()) == report["probe_sha256"]
                  and sha(runner_path.read_bytes()) == runner_hash
                  and sha(generated.read_bytes()) == report["generated_sha256"]
                  and sha(exe.read_bytes()) == report["binary_sha256"]
                  and subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, timeout=60).decode().strip() == candidate_commit)
        report["candidate_stable"] = stable
        if not stable:
            (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
            raise SystemExit("Probe source/binary/candidate changed during observation; unresolved")
        report["complete"] = True
    else:
        report["generation_only"] = True
        report["generated"] = True
    (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    run_facade(out / "facade", args.generate_only)
    run_owner(out / "owner", args.generate_only)
    print(json.dumps({"output": str(out), "generation_only": args.generate_only,
                      "reference_commit": PIN, "rust_parity": False}))


if __name__ == "__main__":
    main()
