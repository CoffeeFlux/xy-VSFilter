#!/usr/bin/env python3
"""Compare safe map histories with a retained complete Windows ATL capture."""
import argparse
import hashlib
import json
import ntpath
import os
from pathlib import Path
import subprocess

from document_atl_contract import ROOT, validate_trace


def sha(data):
    return hashlib.sha256(data).hexdigest()


def native_capture(directory):
    """Rehash retained files; original external SDK/compiler files may be absent."""
    directory = Path(directory).resolve()
    report = json.loads((directory / 'report.json').read_text())
    if report.get('schema') != 'document-atl-contract-v1' or not all(report.get(k) is True for k in ('complete', 'repeat_equal', 'stderr_empty', 'behavior_capture_complete')):
        raise ValueError('native capture is incomplete')
    if report.get('missing_required_definitions') or report.get('source_extraction_errors'):
        raise ValueError('native definitions incomplete')
    bits = {'Win32': 32, 'x64': 64}.get(report.get('platform'))
    if bits is None: raise ValueError('native platform missing')
    expected = report['artifact_sha256']
    names = ('probe.cpp', 'probe.exe', 'build.stdout', 'build.stderr', 'trace-0.stdout', 'trace-0.stderr',
             'trace-1.stdout', 'trace-1.stderr', 'selected-sdk-definitions.private.json', 'observations.json')
    verified = {'report.json': sha((directory / 'report.json').read_bytes())}
    for name in names:
        matches = [value for path, value in expected.items() if ntpath.basename(path) == name]
        data = (directory / name).read_bytes()
        if len(matches) != 1 or sha(data) != matches[0]: raise ValueError('native retained artifact hash mismatch: ' + name)
        verified[name] = sha(data)
    if report['binary_sha256'] != verified['probe.exe'] or report['selected_definitions_sha256'] != verified['selected-sdk-definitions.private.json']:
        raise ValueError('native binary/definitions provenance mismatch')
    private = json.loads((directory / 'selected-sdk-definitions.private.json').read_text())
    definitions = private['definitions']
    if [{k: v for k, v in row.items() if k != 'excerpt'} for row in definitions] != report['definition_index']:
        raise ValueError('native definition index mismatch')
    required = ('CAtlMap','Lookup','Rehash','PickSize','UpdateRehashThresholds','SetOptimalLoad','Hash','CompareElements','operator[]','RemoveNode','RemoveKey','RemoveAll','s_anPrimes','CPair')
    if any(not any(row['symbol'] == symbol and row['kind'] == 'definition' for row in definitions) for symbol in required):
        raise ValueError('native required definition bodies missing')
    for row in definitions:
        if sha(row['excerpt'].encode('utf-8')) != row['sha256_utf8'] or row['header'] not in report['included_header_sha256'] or not 0 < row['first_line'] <= row['last_line']:
            raise ValueError('native selected definition hash/span/header mismatch')
    traces = [(directory / ('trace-' + str(i) + '.stdout')).read_bytes() for i in range(2)]
    if traces[0] != traces[1] or any((directory / ('trace-' + str(i) + '.stderr')).read_bytes() for i in range(2)):
        raise ValueError('native nondeterminism/stderr')
    for i, data in enumerate(traces):
        execution = [row for row in report['executions'] if row['name'] == 'trace-' + str(i)]
        if len(execution) != 1 or execution[0].get('returncode') != 0 or execution[0].get('stdout_sha256') != sha(data) or execution[0].get('stderr_sha256') != sha(b''):
            raise ValueError('native execution provenance mismatch')
    rows = validate_trace(traces[0], bits)
    if json.loads((directory / 'observations.json').read_text()) != rows:
        raise ValueError('native structured observations differ')
    return report, rows, verified


def compare_traces(native_rows, rust_traces):
    if len(rust_traces) != 2 or rust_traces[0] != rust_traces[1]: raise ValueError('Rust repeats incomplete/nondeterministic')
    # Reuse architecture/type and bounded-history checks by supplying the actual
    # native provenance record. Rust makes no native layout/type claim itself.
    prefix = json.dumps(native_rows[0]).encode() + b'\n'
    parsed = validate_trace(prefix + rust_traces[0], native_rows[0]['pointer_bits'])
    if json.dumps(parsed, sort_keys=True) != json.dumps(native_rows, sort_keys=True):
        raise ValueError('safe map state/key history differs from native')
    return parsed[1:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native-contract', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--candidate-root', type=Path, default=ROOT)
    parser.add_argument('--target', choices=('i686-pc-windows-msvc', 'x86_64-pc-windows-msvc'))
    parser.add_argument('--allow-working-tree', action='store_true', help='Explicit local source-hash comparison; does not claim committed sources')
    args = parser.parse_args()
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    candidate=args.candidate_root.resolve()
    report={'schema':'document-style-map-v1','complete':False,'equal':False,'executions':[],
            'scope':'Safe generic map bounded histories against retained Windows SDK capture; not AddStyle/CopyStyles/style owner/host closure',
            'environment':{k:os.environ.get(k) for k in ('VCToolsVersion','WindowsSDKVersion','VSCMD_ARG_TGT_ARCH')},'artifact_sha256':{}}
    stable=report['artifact_sha256']
    def protect(path):
        path=Path(path).resolve();stable[str(path)]=sha(path.read_bytes());return stable[str(path)]
    def save(): (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    def git(*values): return subprocess.check_output(['git','-C',str(candidate),*values],timeout=60).decode().strip()
    def run(command,name,timeout):
        row={'name':name,'command':command};report['executions'].append(row);save()
        try: result=subprocess.run(command,cwd=candidate,capture_output=True,timeout=timeout)
        except subprocess.TimeoutExpired as error:
            (out/(name+'.stdout')).write_bytes(error.stdout or b'');(out/(name+'.stderr')).write_bytes(error.stderr or b'');row['status']='timeout';raise
        for suffix,data in [('stdout',result.stdout),('stderr',result.stderr)]:
            path=out/(name+'.'+suffix);path.write_bytes(data);row[suffix+'_sha256']=protect(path)
        row['returncode']=result.returncode
        if result.returncode:raise RuntimeError(name+' failed; unresolved')
        return result
    save()
    try:
        native,rows,verified=native_capture(args.native_contract)
        report.update(native_contract=str(args.native_contract.resolve()),native_report_sha256=verified['report.json'],
                      native_verified_artifacts=verified,native_capture_commit=native['candidate_commit'],reference_commit=native['reference_commit'],platform=native['platform'],target=args.target)
        if args.target and args.target != ('i686-pc-windows-msvc' if native['platform']=='Win32' else 'x86_64-pc-windows-msvc'):
            raise ValueError('Rust target does not match native capture architecture')
        report['candidate_commit']=git('rev-parse','HEAD');initial_status=git('status','--porcelain','--untracked-files=no')
        report['working_tree_source_hashes']=bool(initial_status) or args.allow_working_tree
        if initial_status and not args.allow_working_tree: raise ValueError('tracked candidate source is not clean')
        paths=[Path(__file__),candidate/'tools/compat/document_atl_contract.py',candidate/'rust/compat/src/document_style_map.rs',candidate/'rust/compat/src/lib.rs',candidate/'rust/compat/examples/document_style_map_trace.rs',candidate/'rust/compat/Cargo.toml',candidate/'Cargo.toml',candidate/'Cargo.lock']
        for path in paths:
            if not args.allow_working_tree and path.is_relative_to(candidate):git('ls-files','--error-unmatch',path.relative_to(candidate).as_posix())
            protect(path)
        run(['rustc','-vV'],'rustc-version',30);run(['cargo','--version'],'cargo-version',30)
        cargo_target=out/'cargo-target'
        command=['cargo','build','-p','xy-vsfilter-compat','--example','document_style_map_trace','--release','--locked','--target-dir',str(cargo_target)]
        if args.target:command+=['--target',args.target]
        run(command,'build',300)
        exe=cargo_target/(args.target or '')/'release/examples'/('document_style_map_trace.exe' if os.name=='nt' else 'document_style_map_trace')
        protect(exe)
        traces=[]
        for i in range(2):
            result=run([str(exe)],'trace-'+str(i),30)
            if result.stderr:raise ValueError('Rust runtime stderr nonempty')
            traces.append(result.stdout)
        compared=compare_traces(rows,traces)
        if native_capture(args.native_contract)[2]!=verified:raise ValueError('native capture changed')
        for path,expected in stable.items():
            if sha(Path(path).read_bytes())!=expected:raise ValueError('source/artifact changed: '+path)
        if git('rev-parse','HEAD')!=report['candidate_commit'] or git('status','--porcelain','--untracked-files=no')!=initial_status:raise ValueError('candidate checkout changed')
        report.update(complete=True,equal=True,repeat_equal=True,stderr_empty=True,inputs_unchanged=True,
                      state_count=sum(r['event']=='state' for r in compared),key_count=sum(r['event']=='key' for r in compared))
    except Exception as error:
        report['unresolved']=str(error);raise
    finally:save()


if __name__=='__main__':main()
