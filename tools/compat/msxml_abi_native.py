#!/usr/bin/env python3
"""Capture Windows SDK MSXML ABI facts before using Rust COM bindings."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--bits", type=int, choices=(32, 64), required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    source = ROOT / "tools/compat/msxml_abi_native.cpp"
    exe = out / "msxml-abi.exe"
    command = ["cl", "/nologo", "/Bv", "/showIncludes", "/O2", "/EHsc", "/std:c++14", "/MT", "/DUNICODE", "/D_UNICODE", str(source), "/Fo:" + str(out / "probe.obj"), "/Fe:" + str(exe)]
    report = {"complete": False, "source_sha256": sha(source), "runner_sha256": sha(__file__), "command": command,
              "environment": {k: os.environ.get(k) for k in ("VCToolsVersion", "WindowsSDKVersion", "VSCMD_ARG_TGT_ARCH")}}
    def save():
        (out / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    save()
    try:
        compiled = subprocess.run(command, cwd=out, capture_output=True, timeout=120)
        (out / "build.stdout").write_bytes(compiled.stdout)
        (out / "build.stderr").write_bytes(compiled.stderr)
        if compiled.returncode:
            raise RuntimeError(f"ABI probe compile exited {compiled.returncode}")
        headers = {}
        for line in compiled.stdout.decode(errors="replace").splitlines():
            match = re.search(r"([A-Za-z]:\\.+?)\s*$", line)
            if match and Path(match[1]).is_file():
                headers[match[1]] = sha(match[1])
        report["included_source_sha256"] = headers
        report["binary_sha256"] = sha(exe)
        runs = []
        for n in range(2):
            result = subprocess.run([str(exe)], capture_output=True, timeout=20)
            (out / f"trace-{n}.stdout").write_bytes(result.stdout)
            (out / f"trace-{n}.stderr").write_bytes(result.stderr)
            if result.returncode or result.stderr:
                raise RuntimeError("ABI probe did not finish normally")
            value = json.loads(result.stdout)
            if value.get("pointer_bits") != args.bits or value.get("complete") is not True:
                raise ValueError("wrong architecture or incomplete ABI probe")
            runs.append(result.stdout)
        if runs[0] != runs[1]:
            raise ValueError("ABI probe is nondeterministic")
        if any(sha(p) != digest for p, digest in headers.items()) or sha(exe) != report["binary_sha256"]:
            raise ValueError("ABI probe input changed")
        report.update(complete=True, repeat_equal=True, facts=json.loads(runs[0]))
    except Exception as error:
        report["unresolved"] = str(error)
        save()
        raise
    save()
    print(json.dumps({"output": str(out), "complete": True, "bits": args.bits}))


if __name__ == "__main__":
    main()
