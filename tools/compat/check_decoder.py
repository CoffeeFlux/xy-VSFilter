#!/usr/bin/env python3
"""Compile the original C++ decoder and compare it with the real Rust C ABI.

No Python packages, Rust dependencies, Windows headers, or copied reference
algorithm are needed. Run in a VS Native Tools prompt on Windows (x86 or x64).
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def run(command, **kwargs):
    return subprocess.run(command, cwd=ROOT, check=True, **kwargs)


def original(path, commit):
    return run(["git", "show", f"{commit}:{path}"], capture_output=True).stdout


def extract_method(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", help="Rust target; must match the C++ compiler")
    parser.add_argument("--cxx", default="cl" if os.name == "nt" else "c++")
    parser.add_argument("--sanitize", action="store_true", help="Clang/GCC ASan + UBSan")
    args = parser.parse_args()
    reference = json.loads((ROOT / "tools/compat/reference.json").read_text())
    destination = ROOT / "artifacts/decoder" / (args.target or "host")
    destination.mkdir(parents=True, exist_ok=True)
    source = original("src/subtitles/TextFile.cpp", reference["commit"])
    header = original("include/Utf8.h", reference["commit"])
    (destination / "Utf8.h").write_bytes(header)
    method = extract_method(source.decode("utf-8-sig"), "int CTextFile::NextChar()")
    # Minimal platform-independent class shell; method body is verbatim.
    shell = '''#include <cstddef>
#include "Utf8.h"
class CTextFile {
public:
    enum enc { DEFAULT_ENCODING, UTF8, LE16, BE16 };
    enum charerror { CHARERR_NEED_MORE=-1, CHARERR_REOPEN=-2 };
    enc m_encoding;
    unsigned char m_readbuffer[16];
    size_t m_bufferPos, m_bufferCount;
    int NextChar();
};
'''
    license_header = source.decode("utf-8-sig").split("#include", 1)[0]
    (destination / "original_next_char.h").write_text(license_header + shell + method, encoding="utf-8")
    cargo = ["cargo", "rustc", "--locked", "--offline", "--release", "-p", "xy-vsfilter-ffi"]
    if args.target:
        cargo += ["--target", args.target]
    cargo += ["--", "--print", "native-static-libs"]
    build = run(cargo, capture_output=True, text=True)
    print(build.stderr, end="")
    match = re.search(r"native-static-libs: (.+)", build.stderr)
    if not match:
        raise RuntimeError("rustc did not report native link dependencies")
    libraries = shlex.split(match[1])
    library_dir = ROOT / "target"
    if args.target:
        library_dir /= args.target
    library_dir /= "release"
    executable = destination / ("decoder-oracle.exe" if os.name == "nt" else "decoder-oracle")
    if os.name == "nt":
        if args.sanitize:
            parser.error("--sanitize is currently supported with Clang/GCC only")
        compile_command = [args.cxx, "/nologo", "/O2", "/EHsc", "/std:c++14", "/MD",
            f"/I{destination}", f"/I{ROOT / 'include'}",
            str(ROOT / "tools/compat/decoder_oracle.cpp"), str(library_dir / "xy_vsfilter_ffi.lib"),
            *libraries, f"/Fe:{executable}", f"/Fo:{destination / 'decoder-oracle.obj'}"]
    else:
        compile_command = [args.cxx, "-std=c++11", "-O2", "-Wall", "-Wextra",
            "-I", str(destination), "-I", str(ROOT / "include"),
            str(ROOT / "tools/compat/decoder_oracle.cpp"), str(library_dir / "libxy_vsfilter_ffi.a"),
            *libraries, "-o", str(executable)]
        if args.sanitize:
            compile_command += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
    run(compile_command)
    result = run([str(executable)], capture_output=True, text=True)
    print(result.stdout, end="")
    print(result.stderr, end="", file=sys.stderr)
    report = {"reference": reference, "source_sha256": hashlib.sha256(source).hexdigest(),
        "header_sha256": hashlib.sha256(header).hexdigest(), "target": args.target or "host",
        "sanitizers": args.sanitize, "compile_command": compile_command,
        "rustc": run(["rustc", "-Vv"], capture_output=True, text=True).stdout,
        "result": result.stdout, "stderr": result.stderr}
    (destination / "report.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
