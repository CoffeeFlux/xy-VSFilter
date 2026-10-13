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
    try:
        return subprocess.run(command, cwd=ROOT, check=True, **kwargs)
    except subprocess.CalledProcessError as error:
        for output in (error.stdout, error.stderr):
            if output:
                print(output.decode(errors="replace") if isinstance(output, bytes) else output, file=sys.stderr)
        raise


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
    # A failed rerun must never leave a previous run's successful report behind.
    (destination / "report.json").write_text(json.dumps({"complete": False, "reference": reference}) + "\n")
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
    candidate_source = (ROOT / "src/subtitles/TextFile.cpp").read_bytes()
    methods = ["bool CTextFile::Open(", "ULONGLONG CTextFile::GetPosition(",
               "ULONGLONG CTextFile::GetLength(", "ULONGLONG CTextFile::Seek(",
               "bool CTextFile::ReadLine(", "int CTextFile::NextChar("]
    reader = license_header + '''#include "text_reader_shell.h"
#include "Utf8.h"
#include "xy_rust_text.h"
#define __super MemoryFile
#define XY_LOG_ERROR(message) ((void)0)
'''
    for name, content in (("OriginalTextFile", source), ("CandidateTextFile", candidate_source)):
        reader += f"\nTEXT_READER_SHELL({name})\n#define CTextFile {name}\n"
        if name == "CandidateTextFile":
            reader += "#define XY_VSFILTER_RUST_TEXT\n"
        reader += "\n".join(extract_method(content.decode("utf-8-sig"), signature) for signature in methods)
        reader += "\n#undef CTextFile\n#undef XY_VSFILTER_RUST_TEXT\n"
    reader += "#undef __super\n#undef XY_LOG_ERROR\n"
    (destination / "original_text_reader.h").write_text(reader, encoding="utf-8")
    font_source = original("src/subtitles/STS.cpp", reference["commit"])
    candidate_font = (ROOT / "src/subtitles/STS.cpp").read_bytes()
    font = font_source.decode("utf-8-sig").split("#include", 1)[0]
    font += '#include "font_shell.h"\n#include "xy_rust_text.h"\n'
    for name, content in (("OriginalFont", font_source), ("CandidateFont", candidate_font)):
        text = content.decode("utf-8-sig")
        start = text.index("static bool LoadFont(CString& font)")
        end = text.index("    HANDLE hFont = INVALID_HANDLE_VALUE;", start)
        font += f"namespace {name} {{\nstd::vector<BYTE> decoded;\n"
        if name == "CandidateFont":
            font += "#define XY_VSFILTER_RUST_TEXT\n"
        # Stop exactly where Windows font registration starts; retain the entire
        # original decode/allocation/length-check block without rewriting it.
        font += text[start:end] + '\n decoded.assign((BYTE*)pData, (BYTE*)pData + datalen);\n return true;\n}\n}\n'
        font += "#undef XY_VSFILTER_RUST_TEXT\n"
    (destination / "original_font.h").write_text(font, encoding="utf-8")
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
            f"/I{destination}", f"/I{ROOT / 'include'}", f"/I{ROOT / 'tools/compat'}",
            str(ROOT / "tools/compat/decoder_oracle.cpp"), str(library_dir / "xy_vsfilter_ffi.lib"),
            *libraries, f"/Fe:{executable}", f"/Fo:{destination / 'decoder-oracle.obj'}"]
    else:
        compile_command = [args.cxx, "-std=c++11", "-O2", "-Wall", "-Wextra",
            "-I", str(destination), "-I", str(ROOT / "include"), "-I", str(ROOT / "tools/compat"),
            str(ROOT / "tools/compat/decoder_oracle.cpp"), str(library_dir / "libxy_vsfilter_ffi.a"),
            *libraries, "-o", str(executable)]
        if args.sanitize:
            compile_command += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
    run(compile_command)
    result = run([str(executable)], capture_output=True, text=True)
    print(result.stdout, end="")
    print(result.stderr, end="", file=sys.stderr)
    reader_executable = destination / ("text-reader-oracle.exe" if os.name == "nt" else "text-reader-oracle")
    reader_compile = [arg.replace("decoder_oracle.cpp", "text_reader_oracle.cpp")
                      .replace("decoder-oracle", "text-reader-oracle") for arg in compile_command]
    run(reader_compile)
    reader_result = run([str(reader_executable)], capture_output=True, text=True)
    print(reader_result.stdout, end="")
    print(reader_result.stderr, end="", file=sys.stderr)
    font_executable = destination / ("font-oracle.exe" if os.name == "nt" else "font-oracle")
    font_compile = [arg.replace("decoder_oracle.cpp", "font_oracle.cpp")
                    .replace("decoder-oracle", "font-oracle") for arg in compile_command]
    run(font_compile)
    font_result = run([str(font_executable)], capture_output=True, text=True)
    print(font_result.stdout, end="")
    print(font_result.stderr, end="", file=sys.stderr)
    compiler = subprocess.run([args.cxx] if os.name == "nt" else [args.cxx, "--version"],
                              capture_output=True, text=True)
    inputs = [ROOT / "Cargo.toml", ROOT / "Cargo.lock", ROOT / "include/xy_rust_text.h"]
    inputs += sorted((ROOT / "rust").rglob("*.rs")) + sorted((ROOT / "rust").rglob("Cargo.toml"))
    report = {"complete": True, "reference": reference, "source_sha256": hashlib.sha256(source).hexdigest(),
        "header_sha256": hashlib.sha256(header).hexdigest(), "target": args.target or "host",
        "sanitizers": args.sanitize, "compile_command": compile_command,
        "cpp_compiler": compiler.stdout + compiler.stderr,
        "candidate_commit": run(["git", "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip(),
        "candidate_inputs": {path.relative_to(ROOT).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs},
        "rustc": run(["rustc", "-Vv"], capture_output=True, text=True).stdout,
        "result": result.stdout, "stderr": result.stderr,
        "candidate_source_sha256": hashlib.sha256(candidate_source).hexdigest(),
        "text_reader_compile_command": reader_compile,
        "text_reader_result": reader_result.stdout, "text_reader_stderr": reader_result.stderr,
        "font_source_sha256": hashlib.sha256(font_source).hexdigest(),
        "candidate_font_source_sha256": hashlib.sha256(candidate_font).hexdigest(),
        "font_compile_command": font_compile,
        "font_result": font_result.stdout, "font_stderr": font_result.stderr}
    (destination / "report.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
