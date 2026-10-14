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

ROOT = Path(__file__).resolve().parents[2]
PIN = json.loads((ROOT / "tools/compat/reference.json").read_text())["commit"]
SOURCE = ROOT / "tools/compat/document_native_lifecycle.cpp"
LIBRARIES = (
    "strmbaseR.lib", "dsutilR.lib", "subtitlesR.lib", "subpicR.lib",
    "basevideofilterR.lib", "libssfR.lib", "log4cplus_staticRU.lib",
    "KasumiR.lib", "systemR.lib", "unrarR.lib",
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


def validate_trace(data, pointer_bits):
    lines = data.splitlines()
    if not lines or lines[0] != f"provenance pointer_bits={pointer_bits}".encode():
        raise ValueError("missing or incorrect native architecture provenance")
    if lines[-1] != b"document-native-lifecycle-v1 complete":
        raise ValueError("missing native lifecycle completion marker")
    for prefix in (b"state ", b"entry ", b"segment ", b"callback ", b"text "):
        if not any(line.startswith(prefix) for line in lines):
            raise ValueError(f"missing native lifecycle trace section {prefix!r}")


def compare_traces(traces, pointer_bits):
    if len(traces) != 4:
        raise ValueError("two reference and two candidate executions required")
    for trace in traces:
        validate_trace(trace, pointer_bits)
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
              "runner_sha256": sha(Path(__file__).read_bytes()), "builds": {},
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
                       "/std:c++17", "/MT", "/Gy", "/showIncludes"]
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
                result = run([str(exe)], work, f"{label}-trace-{repeat}", 30)
                if result.stderr:
                    raise ValueError(f"{label} execution produced unexpected stderr")
                validate_trace(result.stdout, bits)
                traces.append(result.stdout)
            if sha(exe.read_bytes()) != build["binary_sha256"]:
                raise ValueError("native probe executable changed during execution")
            for path, expected in build["library_sha256"].items():
                if sha(Path(path).read_bytes()) != expected:
                    raise ValueError("native library changed during execution")
            for path, expected in headers.items():
                if sha(Path(path).read_bytes()) != expected:
                    raise ValueError("included header changed during execution")
        compare_traces(traces, bits)
        if [git(root, "rev-parse", "HEAD") for root in roots] != commits:
            raise ValueError("checkout commit changed during observation")
        if any(git(root, "status", "--porcelain", "--untracked-files=no") for root in roots):
            raise ValueError("tracked native source changed during observation")
        if sha(SOURCE.read_bytes()) != report["probe_sha256"] or sha(Path(__file__).read_bytes()) != report["runner_sha256"]:
            raise ValueError("probe source changed during observation")
        report.update(complete=True, equal=True, repeat_equal=True, stderr_empty=True, inputs_unchanged=True,
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
