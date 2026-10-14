#!/usr/bin/env python3
"""Compare safe preparation with retained, bounded Windows MFC observations."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess

import document_prepare_check as native


def verify_pe(binary, bits):
    if binary[:2] != b'MZ' or len(binary) < 64:
        raise ValueError('executable is not PE')
    offset = struct.unpack_from('<I', binary, 60)[0]
    machine = {32:0x14c,64:0x8664}[bits]
    if binary[offset:offset+4] != b'PE\0\0' or struct.unpack_from('<H',binary,offset+4)[0] != machine:
        raise ValueError('executable architecture mismatch')
    return machine


def native_capture(directory, reference_root=native.ROOT):
    """Verify retained evidence, without executing a native binary again."""
    directory = Path(directory)
    report_bytes = (directory / 'report.json').read_bytes()
    report = json.loads(report_bytes)
    required = ('complete', 'candidate_tracked_clean', 'candidate_source_stable',
                'repeat_equal', 'stderr_empty')
    if report.get('schema') != 1 or any(report.get(k) is not True for k in required):
        raise ValueError('native preparation capture incomplete')
    if report.get('reference_commit') != native.PIN:
        raise ValueError('reference commit mismatch')
    bits = report.get('pointer_bits')
    if bits not in (32, 64) or report.get('requested_platform') != {32:'Win32',64:'x64'}[bits]:
        raise ValueError('native architecture missing or inconsistent')
    expected = {'document_prepare_probe.cpp':report['generated_sha256'],
                'document_prepare_probe.exe':report['exe_sha256'],
                'build.log':report['build_log_sha256'],
                'trace-0.txt':report['trace_sha256'][0],
                'trace-1.txt':report['trace_sha256'][1],
                'stderr-0.txt':native.sha(b''), 'stderr-1.txt':native.sha(b'')}
    verified = {'report.json':native.sha(report_bytes)}
    for name, digest in expected.items():
        if native.sha((directory / name).read_bytes()) != digest:
            raise ValueError('native artifact hash mismatch: ' + name)
        verified[name] = digest
    binary = (directory / 'document_prepare_probe.exe').read_bytes()
    machine = verify_pe(binary, bits)
    if report.get('pe_machine') != machine:
        raise ValueError('native report architecture mismatch')
    traces = [(directory / f'trace-{i}.txt').read_bytes() for i in range(2)]
    count = native.compare_traces(*traces, bits)
    if count != report.get('observations') or count != report.get('fixture_count'):
        raise ValueError('native observation count mismatch')
    if native.sha(json.dumps(native.fixtures(),separators=(',',':')).encode()) != report['fixture_sha256']:
        raise ValueError('native fixtures changed')
    raw = subprocess.check_output(['git','show',f'{native.PIN}:src/subtitles/STS.cpp'],cwd=reference_root,timeout=30)
    extracted = native.extract(raw.decode('utf-8-sig').replace('\r\n','\n'))
    if native.sha(raw) != report['reference_source_sha256'] or extracted != report['extracted_suffixes']:
        raise ValueError('pinned native suffix provenance mismatch')
    if native.generate(extracted).encode() != (directory/'document_prepare_probe.cpp').read_bytes():
        raise ValueError('generated native probe differs from bounded source')
    # Source hashes from the Windows capture are checked against that commit,
    # allowing Git's Windows line-ending conversion, not arbitrary source edits.
    for path, digest in report['candidate_source_sha256'].items():
        path = path.replace('\\','/')
        blob = subprocess.check_output(['git','show',report['candidate_commit']+':'+path],cwd=native.ROOT,timeout=30)
        windows = blob.replace(b'\r\n',b'\n').replace(b'\n',b'\r\n')
        if digest not in (native.sha(blob),native.sha(windows)):
            raise ValueError('native capture runner source mismatch: ' + path)
    return report, traces[0], verified


def compare_values(native_trace, rust_traces, bits):
    count = native.validate_trace(native_trace, bits)
    if len(rust_traces) != 2 or rust_traces[0] != rust_traces[1]:
        raise ValueError('Rust observations incomplete or not repeatable')
    expected = [line for line in native_trace.splitlines() if line.startswith(b'value ')]
    if rust_traces[0].splitlines() != expected:
        raise ValueError('safe preparation output differs from native raw UTF16 observations')
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native-capture', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--reference-root', type=Path, default=native.ROOT,
                        help='Repository containing the pinned reference object; needed with shallow candidate checkouts')
    parser.add_argument('--target', choices=('i686-pc-windows-msvc','x86_64-pc-windows-msvc'))
    parser.add_argument('--allow-working-tree', action='store_true')
    args = parser.parse_args()
    out = args.output.resolve(); out.mkdir(parents=True, exist_ok=True)
    report = {'schema':'document-prepare-rust-v1', 'complete':False, 'equal':False,
              'scope':'Bounded final Add/EntryOnly suffix and SetStr LF-only; excludes preceding trimming, WebVTT, encoding, append, callbacks, unassigned CString tail units and full host parity',
              'executions':[], 'artifact_sha256':{}}
    protected = report['artifact_sha256']
    def save(): (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    def protect(path):
        path = Path(path).resolve(); protected[str(path)] = native.sha(path.read_bytes())
    def git(*args):
        return subprocess.check_output(['git',*args],cwd=native.ROOT,timeout=30).decode().strip()
    def run(command, name, timeout, input_bytes=None):
        row = {'name':name, 'command':command}; report['executions'].append(row); save()
        try:
            result = subprocess.run(command,cwd=native.ROOT,input=input_bytes,capture_output=True,timeout=timeout)
        except subprocess.TimeoutExpired as error:
            (out/(name+'.stdout')).write_bytes(error.stdout or b'')
            (out/(name+'.stderr')).write_bytes(error.stderr or b'')
            row['timed_out'] = True
            raise
        for suffix, data in [('stdout',result.stdout),('stderr',result.stderr)]:
            path = out/(name+'.'+suffix); path.write_bytes(data); protect(path)
            row[suffix+'_sha256'] = native.sha(data)
        row['returncode'] = result.returncode
        if result.returncode: raise ValueError(name+' failed')
        return result
    save()
    try:
        captured, trace, verified = native_capture(args.native_capture,args.reference_root)
        report.update(native_capture=str(args.native_capture.resolve()), native_verified_artifacts=verified,
                      native_capture_commit=captured['candidate_commit'], reference_commit=native.PIN,
                      native_pointer_bits=captured['pointer_bits'], target=args.target,
                      external_sdk_compiler_library_hashes='Runner reported; external files not retained')
        if args.target and args.target != {32:'i686-pc-windows-msvc',64:'x86_64-pc-windows-msvc'}[captured['pointer_bits']]:
            raise ValueError('Rust target and native architecture differ')
        initial_commit = git('rev-parse','HEAD')
        initial_status = git('status','--porcelain','--untracked-files=no')
        report.update(candidate_commit=initial_commit, working_tree_source_hashes=bool(initial_status) or args.allow_working_tree)
        if initial_status and not args.allow_working_tree: raise ValueError('candidate tracked sources are not clean')
        paths = [Path(__file__), Path(native.__file__), native.ROOT/'tools/compat/test_document_prepare_rust_check.py',
                 native.ROOT/'rust/compat/examples/document_prepare_trace.rs', native.ROOT/'rust/compat/Cargo.toml',
                 native.ROOT/'Cargo.toml', native.ROOT/'Cargo.lock']
        paths += sorted((native.ROOT/'rust/compat/src').rglob('*.rs'))
        for path in paths:
            if not args.allow_working_tree: git('ls-files','--error-unmatch',str(path.relative_to(native.ROOT)))
            protect(path)
        run(['rustc','-vV'],'rustc-version',30)
        run(['cargo','--version'],'cargo-version',30)
        target_dir = out/'cargo-target'
        command = ['cargo','build','-p','xy-vsfilter-compat','--example','document_prepare_trace',
                   '--release','--locked','--target-dir',str(target_dir)]
        if args.target: command += ['--target',args.target]
        run(command,'build',300)
        exe = target_dir/(args.target or '')/'release/examples'/('document_prepare_trace.exe' if os.name=='nt' else 'document_prepare_trace')
        protect(exe)
        if args.target:
            report['rust_pe_machine'] = verify_pe(exe.read_bytes(),captured['pointer_bits'])
        inputs = b'\n'.join(line for line in trace.splitlines() if line.startswith(b'input '))+b'\n'
        (out/'inputs.txt').write_bytes(inputs); protect(out/'inputs.txt')
        results = []
        for i in range(2):
            result = run([str(exe)],f'trace-{i}',30,inputs)
            if result.stderr: raise ValueError('Rust oracle emitted stderr')
            results.append(result.stdout)
        count = compare_values(trace,results,captured['pointer_bits'])
        if native_capture(args.native_capture,args.reference_root)[2] != verified: raise ValueError('native capture changed during comparison')
        for path, digest in protected.items():
            if native.sha(Path(path).read_bytes()) != digest: raise ValueError('source/artifact changed during comparison: '+path)
        if git('rev-parse','HEAD') != initial_commit or git('status','--porcelain','--untracked-files=no') != initial_status:
            raise ValueError('candidate checkout changed during comparison')
        report.update(complete=True, equal=True, repeat_equal=True, stderr_empty=True, inputs_unchanged=True, observations=count)
    except (ValueError, OSError, KeyError, IndexError, struct.error, subprocess.SubprocessError) as error:
        report['unresolved'] = str(error); save(); return 1
    save(); return 0


if __name__ == '__main__':
    raise SystemExit(main())
