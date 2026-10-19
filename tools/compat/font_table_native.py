#!/usr/bin/env python3
"""Compare Rust font key ownership with the cleanup key-owning template.

Instrumented keys call actual STSStyleBase equality and hashing; parallel native
keys assert the same aliases. Payloads observe creation/deletion, not GDI.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

from document_native_lifecycle import ROOT, PIN, LIBRARIES, SYSTEM_LIBRARIES, find_library, git, sha

SOURCE = ROOT / 'tools/compat/font_table_native.cpp'
STEPS = 3284

def validate(data, bits):
    rows=data.splitlines()
    if not rows or rows[0]!=f'font-table pointer_bits={bits}'.encode(): raise ValueError('wrong architecture')
    pending=None;step=0;comparisons=0;live=set();created=set()
    for row in rows[1:-1]:
        words=row.split()
        if row.startswith((b'input ',b'copy ',b'drop ')):
            if pending is not None or int(words[1])!=step or not 0<=int(words[2])<704: raise ValueError('bad operation order')
            pending=int(words[2])
            if words[0]==b'input':
                if len(words)!=5 or not re.fullmatch(rb'[0-9a-f]{16}',words[3]) or not re.fullmatch(rb'[0-9a-f]{60}',words[4]): raise ValueError('invalid key packet')
            elif words[0]==b'copy':
                if len(words)!=4 or not 0<=int(words[3])<704: raise ValueError('bad copy')
            elif len(words)!=3: raise ValueError('bad drop')
        elif row.startswith(b'eq '):
            if pending is None or not re.fullmatch(rb'eq \d+ \d+ [01]',row): raise ValueError('bad comparison')
            comparisons+=1
        elif row.startswith((b'create ',b'destroy ')):
            if pending is None or len(words)!=2 or not words[1].isdigit(): raise ValueError('bad lifecycle event')
            tag=int(words[1])
            if words[0]==b'create':
                if tag in created: raise ValueError('duplicate construction')
                live.add(tag);created.add(tag)
            else:
                if tag not in live: raise ValueError('unbalanced destruction')
                live.remove(tag)
        elif row.startswith(b'state '):
            if pending is None or len(words)!=6 or int(words[1])!=step or int(words[2])!=pending: raise ValueError('bad state order')
            if not words[3].isdigit() or not re.fullmatch(rb'[0-9a-f]{176}',words[4]) or not re.fullmatch(rb'(?:-|[0-9a-f]{60})',words[5]): raise ValueError('bad state data')
            aliases=int.from_bytes(bytes.fromhex(words[4].decode()),'little')
            if (words[5]==b'-' and aliases) or (words[5]!=b'-' and not aliases&(1<<pending)): raise ValueError('bad self alias')
            pending=None;step+=1
        else: raise ValueError('unexpected trace row')
    if pending is not None or step!=STEPS or live or not created or not comparisons: raise ValueError('incomplete observations')
    if rows[-1]!=f'font-table-complete steps={STEPS} fonts=7'.encode(): raise ValueError('bad final accounting')
    return comparisons

def compare(traces,bits):
    if len(traces)!=4: raise ValueError('two runs per implementation required')
    counts=[validate(t,bits) for t in traces];rows=[t.splitlines() for t in traces]
    for index,trace in enumerate(rows[1:],1):
        if trace!=rows[0]:
            at=next((i for i,pair in enumerate(zip(rows[0],trace)) if pair[0]!=pair[1]),min(len(rows[0]),len(trace)))
            raise ValueError(f'trace {index} differs at row {at}: {rows[0][at:at+1]!r} versus {trace[at:at+1]!r}')
    return counts[0]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference-root', type=Path, required=True)
    parser.add_argument('--platform', choices=['Win32', 'x64'], required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if os.name != 'nt': parser.error('Windows native libraries and MSVC required')
    out = args.output.resolve(); out.mkdir(parents=True, exist_ok=False)
    root = args.reference_root.resolve()
    bits = 32 if args.platform == 'Win32' else 64
    target = 'i686-pc-windows-msvc' if bits == 32 else 'x86_64-pc-windows-msvc'
    report = dict(complete=False, equal=False, reference_commit=PIN, candidate_commit=git(ROOT, 'rev-parse', 'HEAD'),
                  platform=args.platform, steps=STEPS, scope='Safe Rust font key ownership and payload lifecycle; no GDI or renderer switch',
                  limits=['Floating flags, traps, allocation failure and concurrency are not established by this trace.',
                          'Mock payloads observe actual cleanup key ownership. They do not establish Windows font creation, metrics, floating status or native HFONT lifetime parity.'],
                  environment={k:os.environ.get(k) for k in ['VCToolsVersion','WindowsSDKVersion','VSCMD_ARG_TGT_ARCH']})
    inputs = [SOURCE, Path(__file__), ROOT/'rust/compat/examples/font_table_replay.rs', ROOT/'Cargo.toml', ROOT/'Cargo.lock', ROOT/'rust/compat/Cargo.toml'] + list((ROOT/'rust/compat/src').glob('*.rs'))
    report['source_sha256'] = {str(p):sha(p.read_bytes()) for p in inputs}
    def save(): (out/'report.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    def run(command, label, timeout=240):
        (out/f'{label}.command.json').write_text(json.dumps(command, indent=2)+'\n')
        result = subprocess.run(command, cwd=out, capture_output=True, timeout=timeout)
        (out/f'{label}.stdout').write_bytes(result.stdout); (out/f'{label}.stderr').write_bytes(result.stderr)
        if result.returncode: raise RuntimeError(f'{label} exited {result.returncode}; unresolved')
        return result
    save()
    try:
        if git(root, 'rev-parse', 'HEAD') != PIN or any(git(p, 'status', '--porcelain', '--untracked-files=no') for p in [root, ROOT]): raise ValueError('pinned reference and clean committed candidate required')
        libraries = [find_library(root, args.platform, name) for name in LIBRARIES]
        report['library_sha256'] = {str(p):sha(p.read_bytes()) for p in libraries}
        defines = ['UNICODE','_UNICODE','NDEBUG','WINVER=0x0601','_WIN32_WINNT=0x0601','PSAPI_VERSION=1','_USE_MATH_DEFINES','_CRT_SECURE_NO_WARNINGS','_SCL_SECURE_NO_WARNINGS','_AFX_NO_MFC_CONTROLS_IN_DIALOGS','WINDOWS','_WINDOWS','WIN32' if bits==32 else 'WIN64']
        includes = ['src/subtitles','src/filters/transform/vsfilter','src/filters/BaseClasses','src/thirdparty/boost_lib','src/thirdparty/log4cplus/include','src/thirdparty/VirtualDub/h','src/thirdparty','include','include/unrar']
        native = out/'native.exe'
        command = ['cl','/nologo','/Bv','/O2','/Oi','/Ob2','/fp:precise','/EHsc','/std:c++14','/MT','/Gy','/showIncludes']
        command += ['/D'+v for v in defines] + ['/I'+str(root/p) for p in includes]
        command += [str(SOURCE),'/Fo:'+str(out/'native.obj'),'/Fe:'+str(native),'/link','/SUBSYSTEM:CONSOLE','/OPT:REF','/OPT:ICF','/VERBOSE:LIB','/LIBPATH:'+str(root/'lib')] + list(map(str,libraries)) + list(SYSTEM_LIBRARIES)
        built = run(command,'native-build')
        headers = {}
        for row in built.stdout.decode(errors='replace').splitlines():
            m = re.search(r'([A-Za-z]:\\.+?)\s*$',row)
            if m and Path(m[1]).is_file(): headers[m[1]] = sha(Path(m[1]).read_bytes())
        report['included_source_sha256'] = headers
        run(['cargo','build','--locked','--offline','--release','--manifest-path',str(ROOT/'Cargo.toml'),'-p','xy-vsfilter-compat','--example','font_table_replay','--target',target,'--target-dir',str(ROOT/'artifacts/font-table-cargo')], 'rust-build')
        candidate = ROOT/'artifacts/font-table-cargo'/target/'release/examples/font_table_replay.exe'
        report['binary_sha256'] = {str(p):sha(p.read_bytes()) for p in [native,candidate]}; save()
        results = [run([str(native)],f'reference-{i}') for i in [1,2]]
        results += [run([str(candidate),str(out/'reference-1.stdout')],f'candidate-{i}') for i in [1,2]]
        if any(r.stderr for r in results): raise ValueError('unexpected runtime stderr')
        traces = [r.stdout for r in results]
        comparisons = compare(traces,bits)
        for p, digest in {**report['source_sha256'],**report['library_sha256'],**headers,**report['binary_sha256']}.items():
            if sha(Path(p).read_bytes()) != digest: raise ValueError('input or executable changed')
        if git(ROOT,'rev-parse','HEAD') != report['candidate_commit'] or any(git(p,'status','--porcelain','--untracked-files=no') for p in [root, ROOT]): raise ValueError('source changed')
        report.update(complete=True,equal=True,repeat_equal=True,inputs_unchanged=True,comparisons=comparisons,trace_lines=len(traces[0].splitlines()),normalized_trace_sha256=[sha(b'\n'.join(v.splitlines())+b'\n') for v in traces])
    except Exception as error:
        report['unresolved'] = str(error); save(); raise
    save(); print(json.dumps({k:report[k] for k in ['complete','equal','steps','comparisons','trace_lines']}))

if __name__ == '__main__': main()
