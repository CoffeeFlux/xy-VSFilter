#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare disconnected COM filter contracts in fresh Windows processes."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import uuid

ROOT = Path(__file__).resolve().parents[2]
CLASSES = [
    ("VSFilter.dll", "DirectVobSubFilter.h", "CDirectVobSubFilter"),
    ("VSFilter.dll", "DirectVobSubFilter.h", "CDirectVobSubFilter2"),
    ("XySubFilter.dll", "xy_sub_filter.h", "XySubFilter"),
    ("XySubFilter.dll", "auto_load_helper.h", "XySubFilterAutoLoader"),
    ("xy_sub_filter_consumer.dll", "xy_sub_filter_consumer.h", "XySubFilterConsumer"),
]
CUSTOM_IIDS = {
    "IDirectVobSub.h": ["EBE1FB08-3957-47ca-AF13-5827E5442E56", "FE6EC6A0-21CA-4970-9EF0-B296F7F38AF0"],
    "XyOptions.h": ["85E5D6F9-BEFB-4E01-B047-758359CDF9AB"],
    "xy_sub_filter.h": ["8871ac83-89c8-44e9-913f-a9a2a322440a"],
}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def git(root, *arguments):
    return subprocess.run(["git", "-C", str(root), *arguments], check=True, capture_output=True).stdout


def class_manifest(reference_commit, candidate_root):
    records, sources = [], {}
    for dll, header, name in CLASSES:
        path = "src/filters/transform/vsfilter/" + header
        original = git(candidate_root, "show", f"{reference_commit}:{path}")
        current = (candidate_root / path).read_bytes()
        pattern = r'\[uuid\("([^"]+)"\)\]\s*class\s+' + re.escape(name) + r'\b'
        reference_match = re.search(pattern, original.decode("utf-8-sig"))
        candidate_match = re.search(pattern, current.decode("utf-8-sig"))
        if not reference_match or not candidate_match or reference_match[1].lower() != candidate_match[1].lower():
            raise ValueError(f"Missing or changed public class UUID: {name}")
        records.append({"dll": dll, "class": name, "clsid": "{" + reference_match[1] + "}"})
        sources[path] = {"reference_sha256": digest(original), "candidate_sha256": digest(current)}
    for header, values in CUSTOM_IIDS.items():
        path = "src/filters/transform/vsfilter/" + header
        original = git(candidate_root, "show", f"{reference_commit}:{path}")
        current = (candidate_root / path).read_bytes()
        for value in values:
            if value.lower().encode() not in original.lower() or value.lower().encode() not in current.lower():
                raise ValueError(f"Missing or changed interface UUID: {value}")
        sources[path] = {"reference_sha256": digest(original), "candidate_sha256": digest(current)}
    return records, sources


def capture(command, directory, timeout):
    directory.mkdir()
    status, code = "complete", None
    try:
        result = subprocess.run(command, capture_output=True, timeout=timeout)
        stdout, stderr, code = result.stdout, result.stderr, result.returncode
        if code:
            status = "process_failure"
    except subprocess.TimeoutExpired as error:
        stdout, stderr = error.stdout or b"", error.stderr or b""
        status = "timeout"
    except OSError as error:
        stdout, stderr = b"", str(error).encode()
        status = "launch_failure"
    (directory / "stdout.txt").write_bytes(stdout)
    (directory / "stderr.txt").write_bytes(stderr)
    lines = stdout.replace(b"\r\n", b"\n").splitlines()
    if status == "complete" and (not lines or lines[-1] != b"COM_TRACE_COMPLETE" or b"control.created value 1" not in lines):
        status = "incomplete_trace"
    return {"status": status, "returncode": code, "stdout_sha256": digest(stdout),
            "stderr_sha256": digest(stderr), "line_count": len(lines)}


def isolated_capture(probe, dll, clsid, apartment, profile, directory, timeout):
    import winreg
    prefix = r"Software\xyVSFilterCompatibility"
    key_name = prefix + "\\" + str(uuid.uuid4())

    def remove_tree(path):
        try:
            key = winreg.OpenKey(winreg.HKEY_CURRENT_USER, path, 0, winreg.KEY_READ | winreg.KEY_WRITE)
        except FileNotFoundError:
            return
        with key:
            while True:
                try:
                    child = winreg.EnumKey(key, 0)
                except OSError as error:
                    if error.winerror == 259:  # ERROR_NO_MORE_ITEMS
                        break
                    raise
                remove_tree(path + "\\" + child)
        winreg.DeleteKey(winreg.HKEY_CURRENT_USER, path)

    try:
        return capture([str(probe.resolve()), str(dll), clsid, apartment, profile, key_name], directory, timeout)
    finally:
        remove_tree(key_name)
        try:
            winreg.DeleteKey(winreg.HKEY_CURRENT_USER, prefix)
        except (FileNotFoundError, PermissionError):
            pass  # Another worker may still own a private child under this key.


def candidate_pin_id_control(captures, directories):
    if any(result["status"] != "complete" for result in captures.values()):
        return False
    traces = [(directory / "stdout.txt").read_bytes() for directory in directories.values()]
    if len(traces) != 2 or traces[0] != traces[1]:
        return False
    lines = traces[0].splitlines()
    return all(line in lines for line in (b"pin.0.id hr 0", b"pin.0.id utf16",
                                         b"pin.0.find hr 0", b"pin.0.find.identity value 1"))


def compare(captures, directories):
    for name, result in captures.items():
        if result["status"] != "complete":
            return {"kind": result["status"], "capture": name}
    traces = {name: (directories[name] / "stdout.txt").read_bytes() for name in captures}
    if traces["reference-a"] != traces["reference-b"]:
        return {"kind": "reference_unstable"}
    if traces["candidate-a"] != traces["candidate-b"]:
        return {"kind": "candidate_unstable"}
    if traces["reference-a"] != traces["candidate-a"]:
        left, right = traces["reference-a"].splitlines(), traces["candidate-a"].splitlines()
        for index in range(max(len(left), len(right))):
            a, b = left[index] if index < len(left) else None, right[index] if index < len(right) else None
            if a != b:
                return {"kind": "trace_mismatch", "line": index + 1,
                        "reference": None if a is None else a.decode("ascii", errors="backslashreplace"),
                        "candidate": None if b is None else b.decode("ascii", errors="backslashreplace")}
    return {"kind": "equal"}


def find_dll(root, name, platform):
    matches = [p for p in (root / "bin").rglob(name) if p.parent.name == "Release" and p.parent.parent.name == platform]
    if len(matches) != 1:
        raise ValueError(f"Expected one {platform} Release {name} under {root}, found {len(matches)}")
    return matches[0].resolve()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-root", required=True, type=Path)
    parser.add_argument("--candidate-root", required=True, type=Path)
    parser.add_argument("--probe", required=True, type=Path)
    parser.add_argument("--platform", required=True, choices=["Win32", "x64"])
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--timeout", type=float, default=15)
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("timeout must be positive")
    args.output.mkdir(parents=True, exist_ok=False)
    reference = json.loads((ROOT / "tools/compat/reference.json").read_text())["commit"]
    if git(args.reference_root, "rev-parse", "HEAD").decode().strip() != reference or git(args.reference_root, "status", "--porcelain").strip():
        raise ValueError("Reference checkout must be the clean pinned commit")
    classes, headers = class_manifest(reference, args.candidate_root)
    binaries = {name: {side: find_dll(root, name, args.platform) for side, root in
                       [("reference", args.reference_root), ("candidate", args.candidate_root)]} for name, _, _ in CLASSES}
    hashes = {name: {side: digest(path.read_bytes()) for side, path in paths.items()} for name, paths in binaries.items()}
    probe_hash = digest(args.probe.read_bytes())
    report = {"schema": 1, "complete": False, "passed": False, "reference_commit": reference,
              "candidate_commit": git(args.candidate_root, "rev-parse", "HEAD").decode().strip(),
              "platform": args.platform, "probe_sha256": probe_hash, "binaries": hashes,
              "settings_environment": "Private volatile HKCU per worker, removed by parent after termination; native constructor writes are isolated.",
              "reference_exclusions": ["XySubFilterAutoLoader QueryId/FindPin; see docs/autoloader-pin-id-scope.json"],
              "candidate_only_controls": [],
              "harness_sources": {name: digest((ROOT / name).read_bytes()) for name in
                                  ("tools/compat/com_contract_probe.cpp", "tools/compat/com_contract_compare.py")},
              "headers": headers, "cases": [],
              "scope": "Fresh disconnected filters, STA and MTA, class factory and IUnknown identity, standard interface availability, selected custom IIDs through IUnknown only, initial state/name/class/clock, property-page UUIDs, stream count, pin names/directions/IDs/parent identity, FindPin and enumerator reset/clone/skip. No registration, custom interface methods, media negotiation, graph playback or UI. Constructor settings writes are confined to each private volatile HKCU key. Autoloader pin-ID operations are excluded from reference parity and checked separately on the candidate. Two reference and two candidate processes per case; unstable, crashed, timed-out or incomplete runs are failures."}
    report_path = args.output / "report.json"
    def save():
        report_path.write_text(json.dumps(report, indent=2) + "\n")
    save()
    for item in classes:
        for apartment in ("sta", "mta"):
            name = item["class"] + "-" + apartment
            directory = args.output / name
            directory.mkdir()
            observations, directories = {}, {}
            for side in ("reference-a", "reference-b", "candidate-a", "candidate-b"):
                directories[side] = directory / side
                dll = binaries[item["dll"]][side.split("-")[0]]
                profile = "autoload_without_ids" if item["class"] == "XySubFilterAutoLoader" else "full"
                observations[side] = isolated_capture(args.probe, dll, item["clsid"], apartment, profile, directories[side], args.timeout)
            exclusions = ["QueryId", "FindPin"] if item["class"] == "XySubFilterAutoLoader" else []
            report["cases"].append({**item, "id": name, "apartment": apartment, "scope_exclusions": exclusions,
                                    "captures": observations, "verdict": compare(observations, directories)})
            save()
            if exclusions:
                controls, control_directories = {}, {}
                for side in ("candidate-id-a", "candidate-id-b"):
                    control_directories[side] = directory / side
                    controls[side] = isolated_capture(args.probe, binaries[item["dll"]]["candidate"], item["clsid"], apartment, "full", control_directories[side], args.timeout)
                report["candidate_only_controls"].append({"id": name, "captures": controls,
                    "passed": candidate_pin_id_control(controls, control_directories),
                    "scope": "Owned empty pin ID and FindPin identity, stable across two candidate processes; NOT reference parity."})
                save()
    report["inputs_unchanged"] = probe_hash == digest(args.probe.read_bytes()) and all(hashes[name][side] == digest(path.read_bytes()) for name, paths in binaries.items() for side, path in paths.items())
    report["complete"] = True
    report["passed"] = report["inputs_unchanged"] and all(case["verdict"]["kind"] == "equal" for case in report["cases"]) and all(control["passed"] for control in report["candidate_only_controls"])
    save()
    print(f"COM contracts: {sum(case['verdict']['kind'] == 'equal' for case in report['cases'])}/{len(report['cases'])} exact stable scoped cases; {len(report['candidate_only_controls'])} candidate-only pin-ID controls")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
