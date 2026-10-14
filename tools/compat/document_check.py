#!/usr/bin/env python3
"""Prepared-record document suffix differential; portable ATL/value approximation.

Runs pinned native method bodies/tails against a safe Rust owner directly and
through the experimental private C ABI.
This deliberately bypasses text/style/WebVTT preparation, styles/metadata owners,
allocation failures, host callbacks/reentry and rendering. Sorting/rounding stay
native; their generated permutations/time pairs exercise safe ownership handoffs.
Windows x86/x64 ATL/host integration and exact pixels remain required.
"""
import argparse
import hashlib
import itertools
import json
import os
import re
import shlex
import platform
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
REFERENCE = '4911cacb102bc0cbd41449793e54473265b04e62'

def run(args, timeout=60, **kwargs):
    try:
        return subprocess.run(args, cwd=ROOT, check=True, text=True, capture_output=True, timeout=timeout, **kwargs)
    except subprocess.CalledProcessError as error:
        print(error.stdout, error.stderr)
        raise

def method(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth, end = 1, brace+1
    while depth:
        depth += (source[end]=='{')-(source[end]=='}')
        end += 1
    return source[start:end]

def extract(destination):
    raw = {p: subprocess.run(['git','show',f'{REFERENCE}:{p}'],cwd=ROOT,check=True,capture_output=True,timeout=60).stdout
           for p in ['src/subtitles/STS.cpp','src/subtitles/STS.h','src/subtitles/RTS.cpp']}
    source = raw['src/subtitles/STS.cpp'].decode('utf-8-sig')
    header = raw['src/subtitles/STS.h'].decode('utf-8-sig')
    parts = ['#include "document_shell.h"', method(header,'class STSSegment')+';']
    parts += ['''class OriginalDocument {
public:
 CAtlArray<STSEntry> m_entries; CAtlArray<STSSegment> m_segments;
 int m_subtitleType=0,changed=0,logged=0,m_mode=TIME;
 int SearchSub(REFERENCE_TIME,double);
 const STSSegment* SearchSubs(REFERENCE_TIME,double,int*,int*);
 STSSegment* SearchSubs2(REFERENCE_TIME,double,int*,int*);
 REFERENCE_TIME TranslateStart(int,double);
 REFERENCE_TIME TranslateSegmentStart(int,double);
 REFERENCE_TIME TranslateSegmentEnd(int,double);
 void OnChanged(){++changed;} void LogSegments(const CAtlArray<STSSegment>&){++logged;}
 void AddPrepared(STSEntry sub);void EntryOnly(STSEntry sub);void CreateSegments();
 void RemoveAllEntries();void ClearPrepared();void CorrectLast(REFERENCE_TIME bound);
 void Feedback(size_t index,bool value);
 void Sort(bool fRestoreReadorder);void ConvertToTimeBased(double fps);void ConvertToFrameBased(double fps);
 void SetPrepared(int i,const std::vector<uint16_t>& str,int flag);
 void ReplayOrder(const std::vector<size_t>& order);void ReplayTimes(const std::vector<std::pair<REFERENCE_TIME,REFERENCE_TIME>>& times);
 void RenamePrepared(const std::vector<uint16_t>& name,const std::vector<uint16_t>& name3);
};''', method(source,'static bool SegmentCompStart(')]
    add = method(source,'void CSimpleTextSubtitle::Add(')
    # Feed the initialized record immediately before the native readorder assignment.
    readorder = add[add.index('    sub.readorder  ='):add.index(';',add.index('    sub.readorder  ='))+1]
    preamble = 'auto start=sub.start,end=sub.end;int readorder=sub.readorder;\n'
    parts += ['void OriginalDocument::AddPrepared(STSEntry sub){\n'+preamble+readorder+'\n'+add[add.index('    int n = (int)m_entries.GetCount();'):]]
    entry = method(source,'void CSimpleTextSubtitle::AddSTSEntryOnly(')
    parts += ['void OriginalDocument::EntryOnly(STSEntry sub){int readorder=sub.readorder;\n'+entry[entry.index('    sub.readorder  ='):]]
    for name in ['CreateSegments','RemoveAllEntries']:
        parts += [method(source,f'void CSimpleTextSubtitle::{name}()').replace('CSimpleTextSubtitle::','OriginalDocument::')]
    for signature in ['int CSimpleTextSubtitle::SearchSub(', 'const STSSegment* CSimpleTextSubtitle::SearchSubs(', 'STSSegment* CSimpleTextSubtitle::SearchSubs2(', 'REFERENCE_TIME CSimpleTextSubtitle::TranslateStart(', 'REFERENCE_TIME CSimpleTextSubtitle::TranslateSegmentStart(', 'REFERENCE_TIME CSimpleTextSubtitle::TranslateSegmentEnd(']:
        parts += [method(source,signature).replace('CSimpleTextSubtitle::','OriginalDocument::')]
    empty = method(source,'void CSimpleTextSubtitle::Empty()')
    parts += ['void OriginalDocument::ClearPrepared(){\n'+empty[empty.index('    m_segments.RemoveAll();'):]]
    # Original MicroDVD last-stored-entry correction; caller's MS2RT result supplied as bound.
    correction = 'stse.end = min(stse.end, MS2RT(start));'
    if correction not in source: raise RuntimeError('correction extraction seam changed')
    parts += ['void OriginalDocument::CorrectLast(REFERENCE_TIME bound){\nSTSEntry& stse = m_entries[m_entries.GetCount()-1];\n'+correction.replace('MS2RT(start)','bound')+'\n}']
    style = method(source,'void CSimpleTextSubtitle::AddStyle(')
    rename_start = style.index('        for (size_t j = 0, count = m_entries.GetCount();')
    rename = method(style[rename_start:],'for (size_t j = 0, count = m_entries.GetCount();')
    parts += ['void OriginalDocument::RenamePrepared(const std::vector<uint16_t>& name,const std::vector<uint16_t>& name3){\n'+rename+'\n}']
    feedback = 'stss->animated |= s->m_fAnimated2;'
    if feedback not in raw['src/subtitles/RTS.cpp'].decode('utf-8-sig'): raise RuntimeError('feedback seam changed')
    parts += ['void OriginalDocument::Feedback(size_t index,bool value){auto* stss=&m_segments.at(index);struct Animation {bool m_fAnimated2;} animation{value};auto* s=&animation;'+feedback+'}']
    for signature in ['template <typename T> int SGN', 'static int comp1(', 'static int comp2(',
                      'void CSimpleTextSubtitle::Sort(', 'void CSimpleTextSubtitle::ConvertToTimeBased(',
                      'void CSimpleTextSubtitle::ConvertToFrameBased(']:
        parts += [method(source,signature).replace('CSimpleTextSubtitle::','OriginalDocument::')]
    setstr=method(source,'void CSimpleTextSubtitle::SetStr(')
    assignment='stse.str = str;'
    if assignment not in setstr:raise RuntimeError('prepared SetStr assignment seam changed')
    convert=method(source,'void CSimpleTextSubtitle::ConvertUnicode(')
    flag_assignment='stse.fUnicode = fUnicode;'
    if flag_assignment not in convert:raise RuntimeError('ConvertUnicode flag seam changed')
    parts += ['void OriginalDocument::SetPrepared(int i,const std::vector<uint16_t>& str,int flag){STSEntry& stse=m_entries.GetAt(i);'+assignment+'if(flag>=0){bool fUnicode=flag!=0;'+flag_assignment+'}}']
    # Bounded value handoff around native-selected ordering. This is not a
    # replacement reference ordering algorithm; full native Sort runs separately.
    sort_body=method(source,'void CSimpleTextSubtitle::Sort(')
    rebuild=sort_body[sort_body.index('    CreateSegments();'):]
    parts += ['void OriginalDocument::ReplayOrder(const std::vector<size_t>& order){if(order.size()!=m_entries.GetCount())throw std::runtime_error("order count");std::vector<bool> seen(order.size(),false);for(auto i:order){if(i>=order.size()||seen[i])throw std::runtime_error("order bijection");seen[i]=true;}auto before=m_entries;m_entries.RemoveAll();for(auto i:order)m_entries.Add(before[i]);\n'+rebuild]
    timebody=method(source,'void CSimpleTextSubtitle::ConvertToTimeBased(')
    loop=method(timebody,'for(int i = 0, j = m_entries.GetCount();')
    loop=loop.replace('std::llround(stse.start * UNITS_FLOAT / fps)','times[i].first').replace('std::llround(stse.end * UNITS_FLOAT / fps)','times[i].second')
    if 'llround' in loop:raise RuntimeError('time assignment extraction seam changed')
    parts += ['void OriginalDocument::ReplayTimes(const std::vector<std::pair<REFERENCE_TIME,REFERENCE_TIME>>& times){if(times.size()!=m_entries.GetCount())throw std::runtime_error("time count");'+loop+'\n'+rebuild]
    generated = '\n'.join(parts)
    (destination/'original_document.h').write_text(generated,newline='')
    return {'reference_commit':REFERENCE,'reference_sources':{p:hashlib.sha256(b).hexdigest() for p,b in raw.items()},
            'extracted_header_sha256':hashlib.sha256(generated.encode()).hexdigest(),
            'seams':['Add suffix from readorder assignment (initialized prepared record)', 'AddSTSEntryOnly suffix from readorder assignment',
                     'CreateSegments and RemoveAllEntries complete bodies','Empty entries/segments tail only',
                     'MicroDVD end correction with precomputed MS2RT bound','AddStyle entry rename loop only','RTS renderer animation OR assignment','SearchSub/SearchSubs/SearchSubs2 and native translation bodies, query wrapper temporarily fixes TIME even after native conversion metadata becomes FRAME'],
            'storage_shell':'portable std::vector arrays plus immutable trivially-copyable raw-unit handles, including on Windows; not CString/ATL layout or allocation parity',
            'native_retained_controls':'full extracted platform qsort Sort and llround conversion methods choose permutations/time pairs; Rust replays bounded handoffs, not CRT algorithms',
            'adapter_constants':{'UNITS':10000000,'UNITS_FLOAT':10000000.0},
            'additional_seams':['SetStr prepared assignment and optional ConvertUnicode flag assignment; native preparation/conversion bypassed','Sort full body controls plus permutation-copy/rebuild adapter','ConvertToTimeBased/ConvertToFrameBased full bodies select native rounded values; replay assignment loop substitutes supplied time pairs, then rebuilds']}

def corpus():
    sequences=[]
    choices=[(0,5,-1),(1,4,0),(3,7,9),(5,8,1),(2,2,-1)]
    for events in itertools.product(choices,repeat=3):
        seq=['C']
        for i,(a,b,order) in enumerate(events):
            seq.append(f'A 0 {i+1} {a} {b} {order}')
        seq += ['R','K','N 1 55','L 3','R','B','D','R','A 0 4 8 9 -1','C']
        sequences.append(seq)
    # Append-only keeps zero-length records; rebuild uses entry index, not readorder.
    sequences += [['C','A 1 1 0 8 9','A 1 2 2 2 -1','A 1 3 2 6 0','R','F 0 1','K','L 3','N 1 99','R','B','F 0 0','D','C'],
                  ['C','A 0 1 0 10 -1','F 0 1','A 0 2 2 8 -1','K','A 0 3 4 12 0','B','R','D','K','R','B','C'],
                  # WebVTT lower_bound must point to a live segment, or start with no segments.
                  ['C','A 2 1 5 10 -1','A 2 2 5 10 -1','A 2 3 3 5 -1','A 2 4 3 10 -1','A 2 5 1 2 -1','R','C']]
    sequences += [['C','A 0 0 0 2 -1','A 1 0 2 2 -1','R','D','R','C']]
    sequences += [
        # Native entry search preserves binary-search behavior for unsorted starts.
        ['C','A 1 1 5 9 9','A 1 2 0 4 0','A 1 3 5 8 1','A 1 4 5 7 -1','R','C'],
        # Correction can create a reversed stored record; no reversed Add input.
        ['C','A 0 1 10 20 -1','L 5','R','C'],
        # Rebuild members use entry order; incremental insertion uses readorder.
        ['C','A 1 1 0 10 9','A 1 2 0 10 0','A 1 3 0 10 5','R','A 0 4 2 8 1','R','C'],
        ['C','A 0 1 -9223372036854775808 -9223372036854775807 -1',
         'A 0 2 9223372036854775806 9223372036854775807 0','R','L 0','R','C'],
    ]
    # Native-selected permutations/time values become owner handoff inputs.
    # Equal comparator keys intentionally exercise the platform's unstable qsort.
    sequences += [
        ['C']+[f'A 1 {i+1} 0 100 0 0' for i in range(7)]+
            ['R','F 0 1','K','E 0 31 -1','E 3 0 0','S 0','E 6 32 1','S 1','B','S 1','C'],
        ['C','A 1 1 7 12 9 0','A 1 2 0 4 1 0','A 1 3 7 10 0 0',
         'R','F 0 1','S 0','A 0 4 2 8 1','E 1 0 -1','S 1','K','D','O 0','B','C'],
        ['C','A 1 1 -10000001 10000001 0','A 1 2 1 3 9','R','F 0 1',
         'V 0 25','V 1 25','V 1 25','K','E 0 42 0','V 0 25','V 0 25','B','V 1 29.97','V 0 29.97','C'],
        ['C','A 1 1 1000000 3000000 0','A 1 2 -3000000 -1000000 0','R','V 1 -60','V 0 -60','C'],
        ['C','A 1 1 200000 600000 0','A 1 2 -600000 -200000 0','R','V 1 25','V 0 25','C'],
        ['C','O 0','T 0','V 1 25','V 1 25','V 0 25','C'],
        ['C','A 0 1 0 10 0','F 0 1','K','T 1 3 3','E 0 0 -1','T 1 8 2','B','C'],
    ]
    # Larger bounded handoffs exercise native qsort partition paths without
    # importing its ordering into Rust or multiplying the smaller corpus.
    sequences += [
        ['C']+[f'A 1 {i+1} 0 100 0 0' for i in range(32)]+
            ['R','F 0 1','K','S 0','F 0 1','S 1','E 15 73 -1',
             'A 0 40 25 75 0 0','S 0','B','S 1','C'],
        ['C']+[f'A 1 {i+1} {i%4} {100+i%5} {i%3} {i%2}' for i in range(32)]+
            ['R','F 0 1','K','S 0','F 0 1','S 1','E 17 74 0',
             'A 0 41 2 80 1 0','S 0','B','S 1','C'],
    ]
    # Observe seeks against each persistent mutation, including stale segments.
    expanded=[]
    for sequence in sequences:
        history=[]
        for op in sequence:
            history.append(op)
            for time in [-9223372036854775808,-9223372036854775807,-1,0,1,2,3,5,8,10,12,8,2,-1,9223372036854775806,9223372036854775807]:
                for mode in range(3):history.append(f'Q {mode} {time}')
        expanded.append(history)
    return expanded

def validate_replay(original, replay):
    source_lines,replay_lines=original.splitlines(),replay.splitlines()
    if len(source_lines)!=len(replay_lines):raise RuntimeError('native replay line count mismatch')
    for original_line,replay_line in zip(source_lines,replay_lines):
        before,after=original_line.split(),replay_line.split()
        if before[0]=='S':
            if not after or after[0]!='O':raise RuntimeError('Sort replay must be permutation')
            count=int(after[1]);order=list(map(int,after[2:]))
            if not 0<=count<=4096:raise RuntimeError('native replay exceeds bounded record count')
            if count!=len(order) or sorted(order)!=list(range(count)):raise RuntimeError('invalid native replay permutation')
        elif before[0]=='V':
            if after==['P']:continue
            if not after or after[0]!='T':raise RuntimeError('conversion replay must be times or no-op')
            count=int(after[1]);times=list(map(int,after[2:]))
            if not 0<=count<=4096:raise RuntimeError('native replay exceeds bounded record count')
            if len(times)!=2*count or any(x<-(1<<63) or x>=(1<<63) for x in times):raise RuntimeError('invalid native replay times')
        elif original_line!=replay_line:
            raise RuntimeError('native replay changed uncontrolled operation')

def compare(a,b):
    left,right=a.splitlines(),b.splitlines()
    if len(left)!=len(right): raise RuntimeError(f'trace lengths differ: {len(left)}/{len(right)}')
    for i,(x,y) in enumerate(zip(left,right)):
        if x!=y: raise RuntimeError(f'document state mismatch at operation {i}:\nreference {x}\ncandidate {y}')

def build_ffi_trace(destination, target_dir, target):
    cargo=['rustup','run','1.85.0','cargo','rustc','--locked','--offline','--release',
           '-p','xy-vsfilter-ffi','--target-dir',str(target_dir)]
    if os.name=='nt':cargo+=['--features','native-runtime']
    if target:cargo+=['--target',target]
    cargo+=['--','--print','native-static-libs']
    build_env=os.environ.copy()
    if os.name=='nt':
        # Match build_rust_windows.ps1 and the native /MT fixture, including
        # dependencies. Encoded flags would otherwise override RUSTFLAGS.
        build_env.pop('CARGO_ENCODED_RUSTFLAGS',None)
        build_env['RUSTFLAGS']='-C target-feature=+crt-static'
    built=run(cargo,env=build_env)
    match=re.search(r'native-static-libs: (.+)',built.stderr)
    if not match:raise RuntimeError('Rust static link dependencies missing')
    libraries=shlex.split(match[1])
    library_dir=target_dir/(target or '')/'release'
    executable=destination/('document-ffi.exe' if os.name=='nt' else 'document-ffi')
    source=ROOT/'tools/compat/document_ffi_trace.cpp'
    if os.name=='nt':
        command=['cl','/nologo','/MT','/O2','/EHsc','/std:c++17','/I'+str(ROOT/'include'),
                 str(source),str(library_dir/'xy_vsfilter_ffi.lib'),*libraries,
                 '/Fo'+str(destination/'document-ffi.obj'),'/Fe'+str(executable)]
    else:
        command=['c++','-std=c++17','-O1','-g','-fsanitize=address,undefined',
                 '-fno-sanitize-recover=all','-I'+str(ROOT/'include'),str(source),
                 str(library_dir/'libxy_vsfilter_ffi.a'),*libraries,'-o',str(executable)]
        if target and target.endswith('-apple-darwin'):
            command+=['-arch',{'x86_64':'x86_64','aarch64':'arm64'}[target.split('-')[0]]]
    run(command)
    return executable,{'cargo_command':cargo,'compile_command':command,
                       'rustflags':build_env.get('RUSTFLAGS'),
                       'encoded_rustflags_present':'CARGO_ENCODED_RUSTFLAGS' in build_env,
                       'binary_sha256':hashlib.sha256(executable.read_bytes()).hexdigest(),
                       'static_library_sha256':hashlib.sha256((library_dir/('xy_vsfilter_ffi.lib' if os.name=='nt' else 'libxy_vsfilter_ffi.a')).read_bytes()).hexdigest(),
                       'scope':'Experimental stable-handle C ABI lifecycle; no production native owner switch. Native fixture sanitizer does not instrument Rust library.'}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--target',help='Installed Rust target; Apple targets also select the native compiler architecture')
    parser.add_argument('--target-dir',type=Path,default=ROOT/'target',help='Cargo build directory (default: repository target)')
    args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True)
    (args.output/'report.json').write_text(json.dumps({'complete':False,'reference_commit':REFERENCE})+'\n')
    tracked=['Cargo.toml','Cargo.lock','rust/compat/Cargo.toml','tools/compat/document_check.py','tools/compat/test_document_check.py','tools/compat/document_shell.h','tools/compat/document_oracle.cpp','rust/compat/examples/document_trace.rs']
    tracked += [str(p.relative_to(ROOT)) for p in (ROOT/'rust/compat/src').rglob('*.rs')]
    tracked += ['rust/ffi/Cargo.toml','include/xy_rust_document.h','tools/compat/document_ffi_trace.cpp']
    tracked += [str(p.relative_to(ROOT)) for p in (ROOT/'rust/ffi/src').rglob('*.rs')]
    hashes=lambda:{p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in tracked}
    before=hashes()
    target_dir=args.target_dir.resolve()
    suffix='.exe' if os.name=='nt' else ''
    candidate_binary=target_dir / (args.target or '') / 'debug' / 'examples' / ('document_trace'+suffix)
    with tempfile.TemporaryDirectory(prefix='xy-document-') as tmp:
        tmp=Path(tmp);report=extract(tmp)
        (args.output/'original_document.h').write_bytes((tmp/'original_document.h').read_bytes())
        native_binary=tmp/('document-oracle'+suffix)
        if os.name=='nt':
            command=['cl','/Bv','/MT','/O2','/EHsc','/std:c++17','/I'+str(tmp),'/I'+str(ROOT/'tools/compat'),str(ROOT/'tools/compat/document_oracle.cpp'),'/Fo'+str(tmp/'document-oracle.obj'),'/Fe'+str(native_binary)]
            sanitizers=[]
        else:
            command=['c++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(tmp),'-I'+str(ROOT/'tools/compat'),str(ROOT/'tools/compat/document_oracle.cpp'),'-o',str(native_binary)]
            sanitizers=['address','undefined']
            if args.target and args.target.endswith('-apple-darwin'):
                architecture={'x86_64':'x86_64','aarch64':'arm64'}[args.target.split('-')[0]]
                command.extend(['-arch',architecture])
        compile_result=run(command)
        compiler_version=(compile_result.stdout+compile_result.stderr) if os.name=='nt' else run(['c++','--version']).stdout
        cargo_command=['rustup','run','1.85.0','cargo','build','--locked','--offline','--manifest-path','Cargo.toml','-p','xy-vsfilter-compat','--example','document_trace','--target-dir',str(target_dir)]
        if args.target:cargo_command.extend(['--target',args.target])
        run(cargo_command)
        ffi_binary,ffi_report=build_ffi_trace(tmp,target_dir,args.target)
        seq=corpus();ops=[op for s in seq for op in s];data='\n'.join(ops)+'\n'
        (args.output/'operations.txt').write_text(data,newline='')
        replay_path=args.output/'replay-operations.txt'
        repeat_replay_path=args.output/'repeat-replay-operations.txt'
        reference=run([str(native_binary),'--replay-output',str(replay_path.resolve())],input=data)
        repeat=run([str(native_binary),'--replay-output',str(repeat_replay_path.resolve())],input=data)
        compare(reference.stdout,repeat.stdout)
        if replay_path.read_bytes()!=repeat_replay_path.read_bytes():raise RuntimeError('unstable native control replay')
        if reference.stderr!=repeat.stderr:raise RuntimeError('unstable callback counts')
        replay_data=replay_path.read_text()
        validate_replay(data,replay_data)
        replay_reference=run([str(native_binary)],input=replay_data)
        compare(reference.stdout,replay_reference.stdout)
        if reference.stderr!=replay_reference.stderr:raise RuntimeError('replay callback counts differ')
        candidate=run([str(candidate_binary)],input=replay_data)
        # Compare directly against full native S/V methods, not just adapter replay.
        compare(reference.stdout,candidate.stdout)
        ffi_candidate=run([str(ffi_binary)],input=replay_data)
        if ffi_candidate.stderr:raise RuntimeError('document C ABI trace emitted unexpected stderr')
        compare(reference.stdout,ffi_candidate.stdout)
        ffi_report.update(equal=True,operations=len(ops),
                          trace_sha256=hashlib.sha256(ffi_candidate.stdout.encode()).hexdigest())
        negative_controls={}
        lines=reference.stdout.splitlines()
        changed=lines.copy();changed[0]+=' 1'
        for name,mutated in [('content','\n'.join(changed)+'\n'),('length','\n'.join(lines[:-1])+'\n')]:
            try:compare(reference.stdout,mutated)
            except RuntimeError:negative_controls[name]=True
            else:raise RuntimeError(f'negative mismatch control accepted {name} mutation')
        negative=all(negative_controls.values())
        if hashes()!=before:raise RuntimeError('candidate source changed during run')
        (args.output/'reference.txt').write_text(reference.stdout,newline='')
        (args.output/'candidate.txt').write_text(candidate.stdout,newline='')
        (args.output/'ffi-candidate.txt').write_text(ffi_candidate.stdout,newline='')
        report.update(complete=True,sequences=len(seq),operations=len(ops),queries=sum(op.startswith('Q ') for op in ops),
                      mutations_and_control_ops=sum(not op.startswith('Q ') for op in ops),negative_mismatch_control=negative,
                      native_binary_sha256=hashlib.sha256(native_binary.read_bytes()).hexdigest(),
                      candidate_binary_sha256=hashlib.sha256(candidate_binary.read_bytes()).hexdigest(),
                      ffi=ffi_report,
                      native_replay_equal=True,native_control_replay_repeat_equal=True,
                      replay_operations_sha256=hashlib.sha256(replay_path.read_bytes()).hexdigest(),
                      native_control_operations=sum(op.startswith(('S ','V ')) for op in ops),
                      control_handoffs=[{'operation_index':i,'native_operation':original,'replay_operation':replayed}
                                        for i,(original,replayed) in enumerate(zip(ops,replay_data.splitlines()))
                                        if original.startswith(('S ','V '))],
                      cargo_build_command=cargo_command,rust_target=args.target or 'toolchain host',
                      native_sanitizers=sanitizers,negative_controls=negative_controls,
                      platform=platform.platform(),machine=platform.machine(),subprocess_timeout_seconds=60,
                      compiler_version=compiler_version,
                      rustc_version=run(['rustup','run','1.85.0','rustc','--version','--verbose']).stdout,
                      cargo_version=run(['rustup','run','1.85.0','cargo','--version']).stdout,
                      candidate_head=run(['git','rev-parse','HEAD']).stdout.strip(),source_stability_verified=True,
                      native_callback_counts=reference.stderr.strip(),candidate_input_sha256=before,
                      native_compile_command=command,reference_repeat_equal=True,
                      trace_sha256=hashlib.sha256(reference.stdout.encode()).hexdigest(),
                      operations_sha256=hashlib.sha256(data.encode()).hexdigest(),
                      evidence_scope='local working-tree validation, not exact-commit Windows integration',
                      excluded_reference_outcomes=['WebVTT lower_bound=end with nonempty segments',
                        'incremental Add to stale segments containing out-of-range entry members'],
                      limits=['prepared records only: native acceptance and text/style/WebVTT preprocessing bypassed',
                              'clear/copy covers entries and segments only; styles/metadata lifecycle excluded',
                              'rename loop only on NUL-free style names: native style-map ordering/duplicate naming and embedded-NUL comparison excluded',
                              'callback counts observed only; reentry, callbacks copying full host state excluded',
                              'queries use integer TIME mode only, no frame/FPS conversion claim',
                              'replay transport is bounded to 4096 records per control operation',
                              'native qsort/llround retained: only ordering/time handoffs are Rust-owned; portable value shells approximate CString/ATL storage and layout',
                              'no allocation failures, production native integration or rendering'])
        (args.output/'report.json').write_text(json.dumps(report,indent=2)+'\n')
        print(f'{len(seq)} document sequences / {len(ops)} complete state comparisons passed; negative mismatch control passed')
if __name__=='__main__':main()
