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


def validate_trace(data, pointer_bits, *, require_named_stages=True, require_identity_stages=True, require_interpretation_stages=True, require_layout_stages=True, require_metadata_stages=True, require_selected_copy_stages=True, require_style_rename_stages=True, require_sort_stages=True, require_base_empty_stages=True):
    lines = data.splitlines()
    if not lines or lines[0] != f"provenance pointer_bits={pointer_bits}".encode():
        raise ValueError("missing or incorrect native architecture provenance")
    if lines[-1] != b"document-native-lifecycle-v1 complete":
        raise ValueError("missing native lifecycle completion marker")
    for prefix in (b"state ", b"entry ", b"segment ", b"callback ", b"text "):
        if not any(line.startswith(prefix) for line in lines):
            raise ValueError(f"missing native lifecycle trace section {prefix!r}")

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
        for observation in (b"saved-file ssa exists=1 bytes=", b"saved-file ass exists=1 bytes=",
                            b"saved-file srt exists=1 bytes=", b"saved-file srt-sidecar exists=1 bytes=",
                            b"saved-file ssa-sidecar exists=0", b"saved-file ass-sidecar exists=0",
                            b"saved-file srt-no-map exists=1 bytes=", b"saved-file srt-no-map-sidecar exists=0"):
            if not any(line.startswith(observation) for line in lines):
                raise ValueError(f"missing named style export observation {observation!r}")



def compare_traces(traces, pointer_bits, *, require_named_stages=True, require_identity_stages=True, require_interpretation_stages=True, require_layout_stages=True, require_metadata_stages=True, require_selected_copy_stages=True, require_style_rename_stages=True, require_sort_stages=True, require_base_empty_stages=True):
    if len(traces) != 4:
        raise ValueError("two reference and two candidate executions required")
    for trace in traces:
        validate_trace(trace, pointer_bits, require_named_stages=require_named_stages, require_identity_stages=require_identity_stages, require_interpretation_stages=require_interpretation_stages, require_layout_stages=require_layout_stages, require_metadata_stages=require_metadata_stages, require_selected_copy_stages=require_selected_copy_stages, require_style_rename_stages=require_style_rename_stages, require_sort_stages=require_sort_stages, require_base_empty_stages=require_base_empty_stages)
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
