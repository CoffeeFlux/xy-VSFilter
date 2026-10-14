#!/usr/bin/env python3
"""Generate/run Windows ATL/CRT reference primitive observations (no Rust parity).

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
import subprocess
import sys

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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--generate-only", action="store_true")
    args = parser.parse_args()
    out = args.output_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)
    # Invalidate any previous successful report before git/compiler execution.
    (out / "report.json").write_text(json.dumps({"schema": 1, "complete": False, "reference_commit": PIN}, indent=2) + "\n")
    runner_path = Path(__file__).resolve()
    runner_hash = sha(runner_path.read_bytes())
    candidate_commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, timeout=60).decode().strip()
    paths = ["src/subtitles/STS.cpp", "src/subtitles/RTS.cpp", "src/subtitles/STS.h",
             "src/dsutil/DSUtil.h", "src/common.props"]
    sources = {p: source(p) for p in paths}
    sts = sources[paths[0]].decode("utf-8-sig")
    rts = sources[paths[1]].decode("utf-8-sig")
    macros = sources[paths[3]].decode("utf-8-sig")
    entry_match = re.search(r"typedef struct\s*\{[^}]+\} STSEntry;", sources[paths[2]].decode("utf-8-sig"))
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
            required = (b"map ", b"cstring ", b"sort ", b"lsort ", b"numeric ", b"round ")
            if (not lines or not lines[0].startswith(b"provenance ")
                    or lines[-1] != b"document-primitives-v1 complete"
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
    print(json.dumps({"output": str(out), "generation_only": args.generate_only,
                      "reference_commit": PIN, "rust_parity": False}))


if __name__ == "__main__":
    main()
