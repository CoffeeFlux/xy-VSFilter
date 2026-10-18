#!/usr/bin/env python3
"""Record uninitialized SubViewer reference outcomes; never a parity gate."""
import argparse
import json
from pathlib import Path
import re
import subprocess

from document_native_lifecycle import ROOT, PIN, LIBRARIES, SYSTEM_LIBRARIES, find_library, git, sha

SOURCE = ROOT / "tools/compat/subviewer_reference_outcomes.cpp"
OBSERVER = ROOT / "tools/compat/document_native_lifecycle.cpp"


def observations(trace, bits):
    lines = trace.splitlines()
    if not lines or lines[0] != f"subviewer-reference pointer_bits={bits}".encode():
        raise ValueError("missing architecture")
    rows = [line for line in lines if line.startswith(b"case=")]
    if len(rows) != 32:
        raise ValueError("missing reference observations")
    result = []
    for i, row in enumerate(rows):
        match = re.fullmatch(f"case={i} accepted=1 entries=1 ".encode() + rb"(\d+)((?: [0-9a-f]{4})+)", row)
        if not match:
            raise ValueError("unexpected case order or outcome")
        units = [int(word, 16) for word in match[2].split()]
        if len(units) != int(match[1]):
            raise ValueError("incorrect text length")
        result.append({"case": i, "explicit_style": i % 2 == 0, "text_units": units})
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-root", type=Path, required=True)
    parser.add_argument("--platform", choices=("Win32", "x64"), required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = args.reference_root.resolve()
    out = args.output.resolve(); out.mkdir(parents=True, exist_ok=False)
    bits = 32 if args.platform == "Win32" else 64
    report = {"complete": False, "compatibility_established": False, "reference_commit": PIN,
              "platform": args.platform, "classification": "unresolved-reference-uninitialized-style-flags",
              "probe_sha256": sha(SOURCE.read_bytes()), "observer_sha256": sha(OBSERVER.read_bytes()),
              "runner_sha256": sha(Path(__file__).read_bytes()),
              "limits": ["Direct actual-source probe, not pinned DLL host behavior",
                         "Repeat stability in this harness cannot establish stable unsafe-code emulation",
                         "No candidate comparison or memory corruption emulation"]}
    def save():
        (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    def run(command, label, timeout):
        (out / f"{label}.command.json").write_text(json.dumps(command, indent=2))
        result = subprocess.run(command, cwd=out, capture_output=True, timeout=timeout)
        (out / f"{label}.stdout").write_bytes(result.stdout)
        (out / f"{label}.stderr").write_bytes(result.stderr)
        if result.returncode:
            raise RuntimeError(f"{label} exited {result.returncode}")
        return result
    save()
    try:
        if git(root, "rev-parse", "HEAD") != PIN or git(root, "status", "--porcelain", "--untracked-files=no"):
            raise ValueError("clean pinned reference required")
        libraries = [find_library(root, args.platform, name) for name in LIBRARIES]
        defines = ["UNICODE", "_UNICODE", "NDEBUG", "WINVER=0x0601", "_WIN32_WINNT=0x0601", "PSAPI_VERSION=1",
                   "_USE_MATH_DEFINES", "_CRT_SECURE_NO_WARNINGS", "_SCL_SECURE_NO_WARNINGS", "_AFX_NO_MFC_CONTROLS_IN_DIALOGS",
                   "WINDOWS", "_WINDOWS", "WIN32" if bits == 32 else "WIN64", "XY_DOCUMENT_PINNED_REFERENCE"]
        includes = [root / path for path in ("src/subtitles", "src/filters/transform/vsfilter", "src/filters/BaseClasses",
                    "src/thirdparty/boost_lib", "src/thirdparty/log4cplus/include", "src/thirdparty/VirtualDub/h",
                    "src/thirdparty", "include", "include/unrar")]
        exe = out / "subviewer-reference.exe"
        command = ["cl", "/nologo", "/Bv", "/O2", "/Oi", "/Ob2", "/fp:precise", "/EHsc", "/std:c++14", "/MT", "/Gy", "/showIncludes", "/FAs", "/Fa" + str(out / "probe.asm")]
        command += ["/D" + d for d in defines] + ["/I" + str(p) for p in includes]
        command += [str(SOURCE), "/Fo:" + str(out / "probe.obj"), "/Fe:" + str(exe), "/link", "/SUBSYSTEM:CONSOLE", "/OPT:REF", "/OPT:ICF", "/LIBPATH:" + str(root / "lib")]
        command += list(map(str, libraries)) + list(SYSTEM_LIBRARIES)
        hashes = {str(p): sha(p.read_bytes()) for p in libraries + [SOURCE, OBSERVER]}
        built = run(command, "build", 240)
        for line in built.stdout.decode(errors="replace").splitlines():
            match = re.search(r"([A-Za-z]:\\.+?)\s*$", line)
            if match and Path(match[1]).is_file():
                hashes[match[1]] = sha(Path(match[1]).read_bytes())
        traces = [run([str(exe)], f"trace-{i}", 30).stdout for i in range(4)]
        report["observations"] = [observations(trace, bits) for trace in traces]
        report["repeat_equal"] = all(trace == traces[0] for trace in traces)
        report["trace_sha256"] = [sha(trace) for trace in traces]
        report["binary_sha256"] = sha(exe.read_bytes()); report["source_library_sha256"] = hashes
        report["assembly_sha256"] = sha((out / "probe.asm").read_bytes())
        if any(sha(Path(p).read_bytes()) != digest for p, digest in hashes.items()):
            raise ValueError("inputs changed during investigation")
        if git(root, "rev-parse", "HEAD") != PIN or git(root, "status", "--porcelain", "--untracked-files=no"):
            raise ValueError("reference changed")
        report.update(complete=True, inputs_unchanged=True)
    except Exception as error:
        report["unresolved"] = str(error); save(); raise
    save()
    print(json.dumps({key: report[key] for key in ("complete", "classification", "repeat_equal", "compatibility_established")}))


if __name__ == "__main__":
    main()
