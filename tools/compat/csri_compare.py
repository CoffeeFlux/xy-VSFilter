#!/usr/bin/env python3
"""Compare real CSRI libraries in isolated processes, without normalizing pixels.

Windows x86/x64 Python must match the DLL architecture. The driver and tests also
run on macOS/Linux with a test library; that does not validate Windows VSFilter.
This is a crash-isolation harness, not a security sandbox for hostile binaries.
"""
import argparse
import ctypes as C
import hashlib
import json
import locale
import math
import os
from pathlib import Path
import platform
import struct
import subprocess
import sys

from pe_exports import exports as pe_exports

ROOT = Path(__file__).resolve().parents[2]
BGRX = 0x102
GUARD = 64
SCHEMA = 1


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


class Format(C.Structure):
    _fields_ = [("pixfmt", C.c_int), ("width", C.c_uint), ("height", C.c_uint)]


class Frame(C.Structure):
    _fields_ = [("pixfmt", C.c_int), ("planes", C.POINTER(C.c_ubyte) * 4),
                ("strides", C.c_ssize_t * 4)]


class Info(C.Structure):
    _fields_ = [(name, C.c_char_p) for name in ("name", "specific", "longname", "author", "copyright")]


def load_library(path):
    # CSRI is cdecl, including 32-bit Windows. Do not use WinDLL for CSRI calls.
    lib = C.CDLL(str(Path(path).resolve()))
    signatures = {
        "csri_renderer_default": ([], C.c_void_p),
        "csri_renderer_info": ([C.c_void_p], C.POINTER(Info)),
        "csri_renderer_next": ([C.c_void_p], C.c_void_p),
        "csri_renderer_byname": ([C.c_char_p, C.c_char_p], C.c_void_p),
        "csri_query_ext": ([C.c_void_p, C.c_char_p], C.c_void_p),
        "csri_open_file": ([C.c_void_p, C.c_char_p, C.c_void_p], C.c_void_p),
        "csri_open_mem": ([C.c_void_p, C.c_void_p, C.c_size_t, C.c_void_p], C.c_void_p),
        "csri_close": ([C.c_void_p], None),
        "csri_request_fmt": ([C.c_void_p, C.POINTER(Format)], C.c_int),
        "csri_render": ([C.c_void_p, C.POINTER(Frame), C.c_double], None),
    }
    for name, (arguments, result) in signatures.items():
        function = getattr(lib, name)
        function.argtypes = arguments
        function.restype = result
    return lib


def validate_case(case):
    # Validate harness configuration, NEVER the subtitle contents.
    if not isinstance(case.get("id"), str) or not case["id"]:
        raise ValueError("each case needs a nonempty id")
    for field in ("width", "height"):
        if type(case[field]) is not int or not 1 <= case[field] <= 8192:
            raise ValueError(f"invalid {field}")
    padding = case.get("stride_padding", 0)
    if type(padding) is not int or not 0 <= padding <= 4096:
        raise ValueError("invalid stride_padding")
    if case["open"] not in ("file", "memory"):
        raise ValueError("open must be file or memory")
    if not isinstance(case["times"], list) or not case["times"]:
        raise ValueError("times must be nonempty")
    if any(not math.isfinite(float(t)) or abs(float(t)) > 1e10 for t in case["times"]):
        raise ValueError("timestamp outside harness range")
    if type(case.get("fill_seed", 0)) is not int:
        raise ValueError("fill_seed must be an integer")
    if type(case.get("bottom_up", False)) is not bool:
        raise ValueError("bottom_up must be a boolean")


def initial_bytes(size, seed):
    # Includes a varying fourth byte: alpha/unused-byte changes are observable.
    block = bytes(((i * 73 + seed) & 255) for i in range(256))
    return (block * ((size + 255) // 256))[:size]


def capture(dll, case, destination):
    """Worker only: one instance, ordered timestamps (including backward seeks)."""
    validate_case(case)
    script = ROOT / case["script"]
    data = script.read_bytes()
    if digest(data) != case["sha256"]:
        raise ValueError(f"fixture hash mismatch: {script}")
    if os.name == "nt":
        # Avoid OS crash dialogs; unsupported formats are queried, never rendered.
        C.windll.kernel32.SetErrorMode(0x0001 | 0x0002 | 0x8000)
    def stage(name, **details):
        # Survives a crash/timeout inside a native call. No worker output is
        # interpreted as a completed result until close also returns.
        write_json(destination / "progress.json", {"stage": name, **details})

    stage("load_library")
    lib = load_library(dll)
    stage("renderer_discovery")
    renderer = lib.csri_renderer_default()
    if not renderer:
        raise RuntimeError("no default renderer")
    info = lib.csri_renderer_info(renderer)
    if not info:
        raise RuntimeError("no renderer info")
    metadata = {name: (getattr(info.contents, name).hex() if getattr(info.contents, name) is not None else None)
                for name, _ in Info._fields_}
    result = {"status": "ok", "metadata": metadata, "frames": [],
              "next_renderer_present": bool(lib.csri_renderer_next(renderer)),
              "unknown_name_returns_default": lib.csri_renderer_byname(b"xy_compat_nonexistent", None) == renderer,
              "unknown_extension_present": bool(lib.csri_query_ext(renderer, b"xy.compat.nonexistent"))}
    if case["open"] == "file":
        stage("open_file")
        instance = lib.csri_open_file(renderer, str(script.resolve()).encode("utf-8"), None)
    else:
        buffer = C.create_string_buffer(data)
        stage("open_memory")
        instance = lib.csri_open_mem(renderer, buffer, len(data), None)
    if not instance:
        result["status"] = "open_rejected"
        return result
    try:
        stage("format_queries")
        # Capture the legacy advertised support separately from actual rendering.
        # These queries mutate the instance; set the final render format last.
        result["format_results"] = {}
        for pixel_format in (0, 1, 2, 3, 0x100, 0x101, BGRX, 0x103, 0x200, 0x201,
                             0x1000, 0x1001, 0x1002, 0x1100, 0x2011, 0x2111):
            fmt = Format(pixel_format, case["width"], case["height"])
            result["format_results"][str(pixel_format)] = lib.csri_request_fmt(instance, C.byref(fmt))
        fmt = Format(BGRX, case["width"], case["height"])
        result["render_format_result"] = lib.csri_request_fmt(instance, C.byref(fmt))
        if result["render_format_result"] != 0:
            result["status"] = "format_rejected"
            return result
        pitch = case["width"] * 4 + case.get("stride_padding", 0)
        size = pitch * case["height"]
        initial = initial_bytes(size, case.get("fill_seed", 0))
        # A fresh destination for each frame; renderer state survives every seek.
        for index, timestamp in enumerate(case["times"]):
            raw = (C.c_ubyte * (size + GUARD * 2 + 15))()
            address = (C.addressof(raw) + 15) & ~15
            C.memset(address, 0xa5, size + GUARD * 2)
            C.memmove(address + GUARD, initial, size)
            top = address + GUARD
            stride = pitch
            if case.get("bottom_up", False):
                top += (case["height"] - 1) * pitch
                stride = -pitch
            frame = Frame()
            frame.pixfmt = BGRX
            frame.planes[0] = C.cast(top, C.POINTER(C.c_ubyte))
            frame.strides[0] = stride
            stage("render", frame=index, timestamp=str(timestamp))
            lib.csri_render(instance, C.byref(frame), float(timestamp))
            stage("capture_frame", frame=index)
            before = C.string_at(address, GUARD)
            after = C.string_at(address + GUARD + size, GUARD)
            output = C.string_at(address + GUARD, size)
            filename = f"frame-{index:05d}.bin"
            (destination / filename).write_bytes(output)
            result["frames"].append({"file": filename, "time": str(timestamp),
                "time_f64_hex": struct.pack("<d", float(timestamp)).hex(), "sha256": digest(output),
                # Native byte comparison avoids billions of Python iterations
                # during a corpus run. Actual reference/candidate bytes are
                # still compared independently in compare_captures.
                "bytes": size, "changed": output != initial,
                "guards_intact": before == after == b"\xa5" * GUARD})
            if before != b"\xa5" * GUARD or after != b"\xa5" * GUARD:
                result["status"] = "guard_corruption"
                break
    finally:
        stage("close")
        lib.csri_close(instance)
    stage("complete")
    return result


def worker(dll, case_path, destination):
    destination = Path(destination)
    destination.mkdir(parents=True, exist_ok=True)
    try:
        result = capture(dll, read_json(case_path), destination)
    except Exception as error:
        result = {"status": "harness_error", "error": f"{type(error).__name__}: {error}"}
    write_json(destination / "result.json", result)


def run_worker(dll, case_path, destination, timeout):
    destination.mkdir(parents=True, exist_ok=True)
    command = [sys.executable, str(Path(__file__).resolve()), "_worker",
               str(dll), str(case_path), str(destination)]
    try:
        process = subprocess.run(command, capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired as error:
        result = {"status": "timeout"}
        (destination / "stdout.txt").write_bytes(error.stdout or b"")
        (destination / "stderr.txt").write_bytes(error.stderr or b"")
    else:
        (destination / "stdout.txt").write_bytes(process.stdout)
        (destination / "stderr.txt").write_bytes(process.stderr)
        if process.returncode:
            result = {"status": "crash", "returncode": process.returncode}
        else:
            try:
                result = read_json(destination / "result.json")
            except (OSError, ValueError) as error:
                result = {"status": "harness_error", "error": str(error)}
    if result["status"] in {"timeout", "crash", "harness_error"} and (destination / "progress.json").exists():
        try:
            result["last_progress"] = read_json(destination / "progress.json")
        except (OSError, ValueError) as error:
            result["progress_error"] = str(error)
    write_json(destination / "result.json", result)
    return result


def compare_captures(left, right, left_dir, right_dir):
    # Equal crashes/timeouts/errors can NEVER count as compatibility evidence.
    normal = {"ok", "open_rejected", "format_rejected"}
    if left.get("status") not in normal or right.get("status") not in normal:
        return {"kind": "unresolved", "left": left.get("status"), "right": right.get("status")}
    differences = []
    # Compare actual bytes too, not merely hashes. This also detects missing or
    # truncated artifact files and reports a concrete first differing byte.
    for a, b in zip(left.get("frames", []), right.get("frames", [])):
        aa, bb = (left_dir / a["file"]).read_bytes(), (right_dir / b["file"]).read_bytes()
        if digest(aa) != a["sha256"] or digest(bb) != b["sha256"]:
            return {"kind": "unresolved", "reason": "capture artifact hash mismatch"}
        if aa != bb:
            first = next((i for i, pair in enumerate(zip(aa, bb)) if pair[0] != pair[1]), min(len(aa), len(bb)))
            differences.append({"frame": a["file"], "first_byte": first,
                "left_size": len(aa), "right_size": len(bb)})
    if left != right or differences:
        return {"kind": "mismatch", "byte_differences": differences,
                "changed_fields": [key for key in sorted(left.keys() | right.keys()) if left.get(key) != right.get(key)]}
    return {"kind": "equal"}


def environment_manifest():
    result = {"platform": platform.platform(), "machine": platform.machine(),
        "python": sys.version, "pointer_bits": C.sizeof(C.c_void_p) * 8,
        "locale": locale.setlocale(locale.LC_ALL, None)}
    if os.name != "nt":
        result["windows_renderer_validation"] = False
        return result
    result["fonts"] = []
    roots = [Path(os.environ["WINDIR"]) / "Fonts",
             Path(os.environ["LOCALAPPDATA"]) / "Microsoft/Windows/Fonts"]
    for root in roots:
        if root.exists():
            for path in sorted(root.rglob("*")):
                if path.is_file():
                    result["fonts"].append({"path": str(path), "sha256": digest(path.read_bytes())})
    # Registry data stays in local artifacts. Never modify the user's settings.
    registry = subprocess.run(["reg", "query", r"HKCU\Software\Gabest", "/s"], capture_output=True)
    result["gabest_registry"] = {"returncode": registry.returncode,
        "stdout_hex": registry.stdout.hex(), "stderr_hex": registry.stderr.hex()}
    for name in ("GetACP", "GetOEMCP", "GetUserDefaultLCID"):
        result[name] = getattr(C.windll.kernel32, name)()
    return result


def compare(args):
    manifest = read_json(args.cases)
    if manifest.get("schema") != SCHEMA or not manifest.get("cases"):
        raise ValueError("unsupported/empty cases manifest")
    if args.repeat < 2:
        raise ValueError("at least two reference runs are required")
    ids = set()
    for case in manifest["cases"]:
        validate_case(case)
        if case["id"] in ids:
            raise ValueError("duplicate case id")
        ids.add(case["id"])
        if digest((ROOT / case["script"]).read_bytes()) != case["sha256"]:
            raise ValueError(f"fixture hash mismatch: {case['script']}")
    reference, candidate = Path(args.reference).resolve(), Path(args.candidate).resolve()
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=False)  # never mix runs or overwrite evidence
    environment = environment_manifest()
    report = {"schema": SCHEMA, "reference_source": read_json(ROOT / "tools/compat/reference.json"),
        "environment_label": args.environment_label, "environment": environment,
        "manifest_sha256": digest(Path(args.cases).read_bytes()),
        "reference_dll": {"path": str(reference), "sha256": digest(reference.read_bytes())},
        "candidate_dll": {"path": str(candidate), "sha256": digest(candidate.read_bytes())},
        "cases": [], "complete": False}
    if os.name == "nt":
        report["reference_exports"] = pe_exports(reference)
        report["candidate_exports"] = pe_exports(candidate)
        report["exports_equal"] = report["reference_exports"] == report["candidate_exports"]
    write_json(output / "report.json", report)
    for number, case in enumerate(manifest["cases"]):
        directory = output / f"case-{number:05d}"
        directory.mkdir()
        case_path = directory / "case.json"
        write_json(case_path, case)
        runs = []
        for repetition in range(args.repeat):
            capture_dir = directory / f"reference-{repetition}"
            runs.append((run_worker(reference, case_path, capture_dir, args.timeout), capture_dir))
        candidate_dir = directory / "candidate"
        actual = run_worker(candidate, case_path, candidate_dir, args.timeout)
        stable = [compare_captures(runs[0][0], result, runs[0][1], path) for result, path in runs[1:]]
        verdict = compare_captures(runs[0][0], actual, runs[0][1], candidate_dir)
        if any(result["kind"] != "equal" for result in stable):
            verdict = {"kind": "unresolved_reference", "repeat_results": stable}
        entry = {"id": case["id"], "directory": directory.name, "verdict": verdict,
                 "reference_status": runs[0][0]["status"], "candidate_status": actual["status"]}
        report["cases"].append(entry)
        if verdict["kind"] == "equal" and not args.keep_equal_frames:
            for path in directory.glob("*/frame-*.bin"):
                path.unlink()
        write_json(output / "report.json", report)
        print(f"{case['id']}: {verdict['kind']}", flush=True)
    after = environment_manifest()
    report["environment_unchanged"] = after == environment
    if after != environment:
        report["environment_after"] = after
    report["complete"] = True
    report["rendered_frames"] = sum(len(read_json(output / c["directory"] / "candidate/result.json").get("frames", [])) for c in report["cases"])
    report["changed_frames"] = sum(sum(frame["changed"] for frame in
        read_json(output / c["directory"] / "candidate/result.json").get("frames", [])) for c in report["cases"])
    report["passed"] = (report["environment_unchanged"] and report.get("exports_equal", True)
        and report["rendered_frames"] > 0 and all(c["verdict"]["kind"] == "equal" for c in report["cases"]))
    write_json(output / "report.json", report)
    print(f"Report: {output / 'report.json'}")
    return 0 if report["passed"] else 1


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "_worker":
        worker(*sys.argv[2:])
        return 0
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", required=True, help="original VSFilter.dll")
    parser.add_argument("--candidate", required=True, help="candidate VSFilter.dll")
    parser.add_argument("--cases", default=str(ROOT / "test/compat/cases.json"))
    parser.add_argument("--output", required=True, help="new artifact directory")
    parser.add_argument("--environment-label", required=True, help="Windows build, compiler/configuration, font setup")
    parser.add_argument("--repeat", type=int, default=2)
    parser.add_argument("--timeout", type=float, default=60)
    parser.add_argument("--keep-equal-frames", action="store_true")
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("timeout must be positive")
    return compare(args)


if __name__ == "__main__":
    sys.exit(main())
