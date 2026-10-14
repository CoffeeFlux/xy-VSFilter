#!/usr/bin/env python3
"""Compare fixed pinned native STSStyle fixtures with the safe Rust value codec."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

import com_contract_compare as com
from document_native_lifecycle import ROOT, PIN, LIBRARIES, SYSTEM_LIBRARIES, find_library

NATIVE_COMPLETE = 'document-style-value-v1 complete'
RUST_COMPLETE = 'document-style-codec-v1 complete'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def commit_id(root):
    return com.git(root, 'rev-parse', 'HEAD').decode('ascii').strip()


def validate_wire(hex_text, bits):
    if not re.fullmatch(r'(?:[0-9a-fA-F]{2})+', hex_text):
        raise ValueError('invalid wire hex')
    data = bytes.fromhex(hex_text)
    if len(data) > 1024 * 1024:
        raise ValueError('wire exceeds fixture limit')
    if len(data) < 192:
        raise ValueError('truncated wire header')
    count = int.from_bytes(data[64:68], 'little')
    if count > 0x7fffffff or 192 + 2 * count > (1 << (bits - 1)) - 1 or len(data) != 192 + 2 * count:
        raise ValueError('wire count/length mismatch')
    if any(data[59:64] + data[68:72]) or any(v > 1 for v in data[56:59]):
        raise ValueError('invalid reserved/bool wire fields')
    return data


def parse_trace(data, bits, native):
    lines = data.decode('ascii').splitlines()
    marker = NATIVE_COMPLETE if native else RUST_COMPLETE
    if native:
        if not lines or lines.pop(0) != f'provenance pointer_bits={bits}':
            raise ValueError('native architecture provenance missing')
    if not lines or lines.pop() != marker:
        raise ValueError('missing completion')
    values = []
    while lines and lines[0].startswith('value '):
        fields = lines.pop(0).split(' ')
        if len(fields) != 3 or fields[1] != str(len(values)) or len(values) >= 256:
            raise ValueError('invalid value index/count')
        values.append(validate_wire(fields[2], bits))
    if not values:
        raise ValueError('empty value corpus')
    equal = []
    for i in range(len(values)):
        for j in range(len(values)):
            if not lines:
                raise ValueError('truncated equality matrix')
            fields = lines.pop(0).split(' ')
            if len(fields) != 4 or fields[:3] != ['equal', str(i), str(j)] or fields[3] not in ('0', '1'):
                raise ValueError('invalid equality matrix cell/order')
            equal.append(fields[3] == '1')
    if native:
        expected = f'summary values={len(values)} equalities={len(values) * len(values)}'
        if not lines or lines.pop(0) != expected:
            raise ValueError('missing/incorrect native summary')
    if lines:
        raise ValueError('unexpected extra trace')
    return values, equal


def compare_traces(native, rust, bits):
    if len(native) != 2 or len(rust) != 2:
        raise ValueError('two repeats per implementation required')
    if native[0] != native[1] or rust[0] != rust[1]:
        raise ValueError('nondeterministic repeated trace')
    n = parse_trace(native[0], bits, True)
    r = parse_trace(rust[0], bits, False)
    if n != r:
        raise ValueError('wire bits or native equality differ')
    return len(n[0])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference-root', type=Path, required=True)
    parser.add_argument('--candidate-root', type=Path, default=ROOT)
    parser.add_argument('--platform', choices=('Win32', 'x64'), required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('actual Windows ATL/MFC and Release libraries required')
    out = args.output.resolve(); out.mkdir(parents=True, exist_ok=False)
    reference, candidate = args.reference_root.resolve(), args.candidate_root.resolve()
    bits = 32 if args.platform == 'Win32' else 64
    target = 'i686-pc-windows-msvc' if bits == 32 else 'x86_64-pc-windows-msvc'
    report = {'schema': 'document-style-value-v1', 'status': 'incomplete', 'complete': False,
              'equal': False, 'reference_commit': PIN, 'platform': args.platform,
              'executions': [], 'artifacts': {}, 'production_artifacts': {},
              'environment': {key: os.environ.get(key) for key in ('VCToolsVersion', 'VisualStudioVersion', 'WindowsSDKVersion', 'VSCMD_ARG_TGT_ARCH', 'INCLUDE', 'LIB', 'PATH')},
              'tools': {key: shutil.which(key) for key in ('cl', 'link', 'cargo', 'rustc')},
              'scope': 'Fixed initialized pinned-native fixtures only; no native arbitrary-input injection or style owner routing'}
    protected = report['artifacts']
    def protect(path):
        path = Path(path).resolve(); protected[str(path)] = sha(path.read_bytes())
    def save():
        (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    def run(command, cwd, name, timeout, stdin=None, runtime=False):
        record = {'name': name, 'command': [str(v) for v in command], 'cwd': str(cwd)}
        report['executions'].append(record); save()
        try:
            result = subprocess.run(command, cwd=cwd, input=stdin, capture_output=True, timeout=timeout)
        except subprocess.TimeoutExpired as error:
            stdout, stderr = error.stdout or b'', error.stderr or b''
            record.update(status='timeout', returncode=None)
        except OSError as error:
            stdout, stderr = b'', str(error).encode()
            record.update(status='launch_failure', returncode=None)
        else:
            stdout, stderr = result.stdout, result.stderr
            record.update(status='complete' if result.returncode == 0 else 'process_failure', returncode=result.returncode)
        for suffix, data in [('stdout', stdout), ('stderr', stderr)]:
            path = out / (name + '.' + suffix); path.write_bytes(data); protect(path)
            record[suffix + '_sha256'] = sha(data)
        save()
        if record['status'] != 'complete' or (runtime and stderr):
            raise RuntimeError(name + ': unresolved execution/nonempty runtime stderr')
        return stdout
    save()
    try:
        commits = [commit_id(root) for root in (reference, candidate)]
        if commits[0] != PIN:
            raise ValueError('reference is not pinned')
        report['candidate_commit'] = commits[1]
        for label, root in [('reference', reference), ('candidate', candidate)]:
            if com.git(root, 'status', '--porcelain', '--untracked-files=no'):
                raise ValueError('tracked checkout is not clean: ' + label)
            paths = [find_library(root, args.platform, name) for name in LIBRARIES]
            paths += [com.find_dll(root, dll, args.platform) for dll in ('VSFilter.dll', 'XySubFilter.dll')]
            report['production_artifacts'][label] = {str(path): sha(path.read_bytes()) for path in paths}
        source = candidate / 'tools/compat/document_style_value_probe.cpp'
        example = candidate / 'rust/compat/examples/document_style_codec.rs'
        sources = (source, example, candidate / 'rust/compat/src/document_style.rs',
                   candidate / 'rust/compat/src/lib.rs', candidate / 'rust/compat/Cargo.toml',
                   candidate / 'Cargo.toml', candidate / 'Cargo.lock',
                   candidate / 'tools/compat/document_style_value_check.py',
                   candidate / 'tools/compat/document_native_lifecycle.py',
                   candidate / 'tools/compat/com_contract_compare.py')
        for path in sources:
            com.git(candidate, 'ls-files', '--error-unmatch', path.relative_to(candidate).as_posix())
            protect(path)
        protect(Path(__file__))
        for tool in ('cl', 'link', 'cargo', 'rustc'):
            path = shutil.which(tool)
            if path: protect(path)
        run(['rustc', '-vV'], candidate, 'rustc-version', 30)
        run(['cargo', '--version'], candidate, 'cargo-version', 30)
        libraries = [find_library(reference, args.platform, name) for name in LIBRARIES]
        defines = ['UNICODE', '_UNICODE', 'NDEBUG', 'WINVER=0x0601', '_WIN32_WINNT=0x0601',
                   'PSAPI_VERSION=1', '_USE_MATH_DEFINES', '_CRT_SECURE_NO_WARNINGS', '_SCL_SECURE_NO_WARNINGS',
                   '_AFX_NO_MFC_CONTROLS_IN_DIALOGS', 'WINDOWS', '_WINDOWS', 'WIN32' if bits == 32 else 'WIN64', 'XY_DOCUMENT_PINNED_REFERENCE']
        includes = [reference / path for path in ('src/subtitles', 'src/filters/transform/vsfilter', 'src/filters/BaseClasses',
                    'src/thirdparty/boost_lib', 'src/thirdparty/log4cplus/include', 'src/thirdparty/VirtualDub/h', 'src/thirdparty', 'include', 'include/unrar')]
        exe = out / 'document-style-value-native.exe'
        command = ['cl', '/nologo', '/Bv', '/O2', '/Oi', '/Ob2', '/fp:precise', '/EHsc', '/std:c++14', '/MT', '/Gy', '/showIncludes']
        command += ['/D' + d for d in defines] + ['/I' + str(p) for p in includes]
        command += [str(source), '/Fo:' + str(out / 'probe.obj'), '/Fe:' + str(exe), '/link', '/SUBSYSTEM:CONSOLE', '/OPT:REF', '/OPT:ICF', '/VERBOSE:LIB', '/LIBPATH:' + str(reference / 'lib')]
        command += [str(p) for p in libraries] + list(SYSTEM_LIBRARIES)
        compiled = run(command, out, 'native-build', 180)
        for line in compiled.decode(errors='replace').splitlines():
            match = re.search(r'([A-Za-z]:\\.+?)\s*$', line)
            if match and Path(match[1]).is_file(): protect(Path(match[1]))
        protect(reference / 'src/subtitles/STS.h'); protect(exe)
        cargo_dir = out / 'cargo-target'
        run(['cargo', 'build', '-p', 'xy-vsfilter-compat', '--example', 'document_style_codec', '--release', '--target', target, '--target-dir', str(cargo_dir), '--locked'], candidate, 'rust-build', 300)
        rust_exe = cargo_dir / target / 'release/examples/document_style_codec.exe'; protect(rust_exe)
        native = [run([str(exe)], out, 'native-' + str(i), 30, runtime=True) for i in range(2)]
        values, _ = parse_trace(native[0], bits, True)
        stdin = b''.join(value.hex().encode() + b'\n' for value in values)
        input_path = out / 'codec-input.hex'; input_path.write_bytes(stdin); protect(input_path)
        rust = [run([str(rust_exe)], out, 'rust-' + str(i), 30, stdin=stdin, runtime=True) for i in range(2)]
        count = compare_traces(native, rust, bits)
        for group in report['production_artifacts'].values():
            for path, expected in group.items():
                if sha(Path(path).read_bytes()) != expected: raise ValueError('production artifact changed: ' + path)
        for path, expected in protected.items():
            if sha(Path(path).read_bytes()) != expected: raise ValueError('evidence changed: ' + path)
        if [commit_id(root) for root in (reference, candidate)] != commits or any(com.git(root, 'status', '--porcelain', '--untracked-files=no') for root in (reference, candidate)):
            raise ValueError('checkout changed during observation')
        report.update(status='complete', complete=True, equal=True, repeat_equal=True, stderr_empty=True, inputs_unchanged=True, value_count=count, equality_cells=count * count)
    except Exception as error:
        report['unresolved'] = str(error)
        raise
    finally:
        save()


if __name__ == '__main__':
    main()
