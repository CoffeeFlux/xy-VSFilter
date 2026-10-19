#!/usr/bin/env python3
"""Observe actual cleanup style aliases, raw values, floating flags and live NaN keys."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

from document_native_lifecycle import ROOT, PIN, LIBRARIES, SYSTEM_LIBRARIES, find_library, git, sha

SOURCE = ROOT / "tools/compat/style_intern_native.cpp"
CASES = 196
INPUTS = CASES

def validate(data,bits,scenario="interning"):
    if scenario=="document-rectangle-history":return validate_document_history(data,bits)
    if scenario=="document-rectangles":return validate_document_rectangles(data,bits)
    if scenario=="default-style":return validate_default(data,bits)
    if scenario in ("map-lifecycle","map-growth"):return validate_map(data,bits,scenario=="map-growth")
    if scenario=="add-style":return validate_add(data,bits)
    rows=data.splitlines()
    if len(rows)!=CASES*4+4:raise ValueError("missing style observations")
    if not re.fullmatch(rb"style-provenance pointer_bits="+str(bits).encode()+rb" ansi=\d+ oem=\d+",rows[0]):raise ValueError("wrong architecture")
    for i in range(CASES):
        if not re.fullmatch(f"style-intern index={i} ".encode()+rb"alias=[01] copy_alias=1 value_equal=[01] self_equal=[01] count=\d+ rects=\d+ fp=[0-9a-f]{8} hash=[0-9a-f]{16},[0-9a-f]{16}",rows[1+i*4]):raise ValueError("wrong style state/order")
        if not re.fullmatch(f"style-fp index={i} ".encode()+rb"first=[0-9a-f]{8} second=[0-9a-f]{8} copy=[0-9a-f]{8} read=[0-9a-f]{8} equality=[0-9a-f]{8} assigned=[0-9a-f]{8} assigned_domain=[0-9a-f]{16} domains=[0-9a-f]{16}(?:,[0-9a-f]{16}){4}",rows[2+i*4]):raise ValueError("missing separated floating status")
        for row in rows[3+i*4:5+i*4]:
            m=re.fullmatch(rb"style-value rect=(-?\d+,){3}-?\d+ ints=(-?\d+,){7}-?\d+ font=(\d+):([0-9a-f]*) colors=[0-9a-f]{40} doubles=[0-9a-f]{240}",row)
            if not m or int(m[3])*4!=len(m[4]):raise ValueError("invalid full style value")
    if not re.fullmatch(rb'style-release-fp fp=[0-9a-f]{8} domains=[0-9a-f]{16}',rows[-3]):raise ValueError("missing release status")
    expected_final=b'style-final finite_released=1 retained_nan=135 styles=90 rects=1' if scenario in ('same-values','raw-twins') else b'style-final finite_released=1 retained_nan=90 styles=45 rects=1'
    if rows[-2]!=expected_final:raise ValueError("missing retained NaN accounting")
    if rows[-1]!=f'style-intern-complete cases={CASES}'.encode():raise ValueError("missing completion")


def validate_add(data,bits):
    rows=data.splitlines()
    if not rows or rows[0]!=f'style-add-provenance pointer_bits={bits}'.encode():raise ValueError('wrong AddStyle architecture')
    at=1
    for index in range(360):
        if at>=len(rows):raise ValueError('missing AddStyle case')
        m=re.fullmatch(f'style-add index={index} '.encode()+rb'count=([12]) first=[0-9a-f]{8} second=[0-9a-f]{8} domains=[0-9a-f]{16},[0-9a-f]{16}',rows[at])
        if not m:raise ValueError('invalid AddStyle status/count/order')
        count=int(m[1]);at+=1
        for name in range(2):
            present=name==0 or count==2
            if at>=len(rows) or rows[at]!=f'style-add-name present={int(present)}'.encode():raise ValueError('invalid AddStyle conflict names')
            at+=1
            if present:
                if at>=len(rows):raise ValueError('missing AddStyle value')
                value=re.fullmatch(rb'style-value rect=(-?\d+,){3}-?\d+ ints=(-?\d+,){7}-?\d+ font=(\d+):([0-9a-f]*) colors=[0-9a-f]{40} doubles=[0-9a-f]{240}',rows[at])
                if not value or int(value[3])*4!=len(value[4]):raise ValueError('invalid AddStyle raw value')
                at+=1
    if rows[at:]!=[b'style-add-complete cases=360']:raise ValueError('invalid AddStyle completion')


HISTORY_STAGES = "initial source destination input set-default alias-input drop-input add-alias drop-alias conflict snapshot equal-conflict interned copy self-copy-source drop-source empty-destination drop-destination drop-snapshot recreate add-retained drop-interned self-copy-recreated drop-recreated".split()

def validate_document_history(data,bits):
    rows=data.splitlines()
    if len(rows)!=26 or rows[0]!=f'document-history-provenance pointer_bits={bits}'.encode():raise ValueError('invalid document history architecture/length')
    for row,stage in zip(rows[1:-1],HISTORY_STAGES):
        if not re.fullmatch(rb'document-history stage='+stage.encode()+rb' rectangles=[0-9]+ styles=[0-9]+',row):raise ValueError('invalid document history phase')
    if rows[1]!=b'document-history stage=initial rectangles=0 styles=0' or rows[-2]!=b'document-history stage=drop-recreated rectangles=0 styles=0':raise ValueError('document history owner leak')
    if rows[-1]!=b'document-history-complete cases=24':raise ValueError('missing document history completion')


def validate_document_rectangles(data,bits):
    rows=data.splitlines()
    if len(rows)!=16 or rows[0]!=f'document-rect-provenance pointer_bits={bits}'.encode():raise ValueError('invalid document rectangle architecture/length')
    stages=['initial','source','destination','input','set-default','drop-input','copy','empty-source','add-named','copy-named','empty-source-again','empty-destination','drop-source','drop-destination']
    counts=[0,1,1,2,2,2,1,1,2,2,2,1,1,0]
    for index,(stage,count) in enumerate(zip(stages,counts)):
        if rows[index+1]!=f'document-rect stage={stage} rectangles={count} styles=0'.encode():raise ValueError('document rectangle ownership/order differs from reference')
    if rows[-1]!=b'document-rect-complete cases=14':raise ValueError('missing document rectangle completion')


def validate_default(data,bits):
    rows=data.splitlines()
    if len(rows)!=2882 or rows[0]!=f'default-provenance pointer_bits={bits}'.encode():raise ValueError('invalid default-style architecture/length')
    for index in range(360):
        sticky=index//180
        for phase,op in enumerate(('set','copy','self','empty')):
            m=re.fullmatch(f'default-style index={index} sticky={sticky} op={op} '.encode()+rb'fp=[0-9a-f]{8} domains=([0-9a-f]{16}),([0-9a-f]{16})',rows[1+index*8+phase*2])
            if not m or bool(int(m[1],16))!=bool(sticky) or int(m[1],16)&int(m[2],16)!=int(m[1],16):raise ValueError('invalid default-style order/sticky status')
            value=re.fullmatch(rb'style-value rect=(-?\d+,){3}-?\d+ ints=(-?\d+,){7}-?\d+ font=(\d+):([0-9a-f]*) colors=[0-9a-f]{40} doubles=[0-9a-f]{240}',rows[2+index*8+phase*2])
            if not value or int(value[3])*4!=len(value[4]):raise ValueError('invalid default-style raw value')
    if rows[-1]!=b'default-complete cases=360':raise ValueError('missing default-style completion')


def validate_map(data,bits,growth=False):
    rows=data.splitlines()
    size=512 if growth else 128
    operations=([('construct',0)]+[('insert',i+1) for i in range(size)]+[('equal',size)]*128+
                [('conflict',size+1+i) for i in range(32)]+[('empty',0)]+[('reuse',i+1) for i in range(64)]+
                ([('reuse-conflict',65+i) for i in range(32)] if growth else [])+[('empty',0),('destroy',0)])
    cases=len(operations)*2
    if len(rows)!=cases*2+2 or rows[0]!=f'style-map-provenance pointer_bits={bits}'.encode():raise ValueError('invalid map architecture/length')
    for index,(op,count) in enumerate(operations*2):
        sticky=index//len(operations)
        m=re.fullmatch(f'style-map index={index} sticky={sticky} op={op} count={count} '.encode()+rb'fp=[0-9a-f]{8} domains=([0-9a-f]{16}),([0-9a-f]{16})',rows[1+index*2])
        if not m or bool(int(m[1],16))!=bool(sticky) or int(m[1],16)&int(m[2],16)!=int(m[1],16):raise ValueError('invalid map operation/status')
        names=rows[2+index*2].split(b' ')
        if names[0]!=b'style-map-names' or len(names)!=count+1 or any(not re.fullmatch(rb'(?:[0-9a-f]{4})+',name) for name in names[1:]):raise ValueError('invalid map enumeration')
    if rows[-1]!=f'style-map-complete cases={cases}'.encode():raise ValueError('missing map completion')


def compare(traces,bits,scenario="interning"):
    if len(traces)!=4:raise ValueError("two runs per implementation required")
    for trace in traces:validate(trace,bits,scenario)
    if any(trace!=traces[0] for trace in traces[1:]):raise ValueError("style traces differ or are nondeterministic")

def validate_lifecycle(data):
    rows=data.splitlines()
    if len(rows)!=5 or rows[:2]!=[b'style-move copy=1 empty=1 distinct=1 count=1',b'style-move restored=1 source_empty=1 count=1']:raise ValueError('invalid style move lifetime')
    m=re.fullmatch(rb'style-sticky before=([0-9a-f]{8}) after=\1 domains=([0-9a-f]{16}),\2 count=0 rects=0',rows[2])
    if not m or int(m[1],16)==0:raise ValueError('ambient status lost or invalid final release')
    if rows[3:]!=[b'style-lifecycle-complete count=1',b'style-late count=0 rects=0 valid=1']:raise ValueError('late owner read/release failed')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-root", type=Path, required=True)
    parser.add_argument("--platform", choices=("Win32", "x64"), required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--scenario",choices=("interning","same-values","raw-twins","add-style","default-style","map-lifecycle","map-growth","document-rectangles","document-rectangle-history"),default="interning")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("Windows ATL/MFC and built native libraries required")
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    roots = (args.reference_root.resolve(), ROOT)
    count={"add-style":360,"default-style":360,"map-lifecycle":712,"map-growth":1544,"document-rectangles":14,"document-rectangle-history":24}.get(args.scenario,CASES)
    report = dict(complete=False, equal=False, reference_commit=PIN, platform=args.platform,
                  kind="actual-renderer-style-interner", cases=count, inputs=count, scenario=args.scenario, builds={},
                  probe_sha256=sha(SOURCE.read_bytes()), 
                  runner_sha256=sha(Path(__file__).read_bytes()),
                  environment={key:os.environ.get(key) for key in ("VCToolsVersion","WindowsSDKVersion","VSCMD_ARG_TGT_ARCH")},
                  limits=["NaN owners stay alive until process reclamation; expired-key lookup and NaN destruction histories are excluded",
                          "Finite input set; no allocator/reentry failures",
                          "No claim of host or whole-renderer closure"])
    def save():
        (out/"report.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    def run(command, cwd, label, timeout):
        (out/f"{label}.command.json").write_text(json.dumps(command,indent=2)+"\n",encoding="utf-8")
        result=subprocess.run(command,cwd=cwd,capture_output=True,timeout=timeout)
        (out/f"{label}.stdout").write_bytes(result.stdout)
        (out/f"{label}.stderr").write_bytes(result.stderr)
        if result.returncode:
            raise RuntimeError(f"{label} exited {result.returncode}; unresolved")
        return result
    save()
    try:
        commits=[git(root,"rev-parse","HEAD") for root in roots]
        if commits[0]!=PIN or any(git(root,"status","--porcelain","--untracked-files=no") for root in roots):
            raise ValueError("clean committed candidate and pinned reference required")
        report["candidate_commit"]=commits[1]
        bits=32 if args.platform=="Win32" else 64
        target="i686-pc-windows-msvc" if bits==32 else "x86_64-pc-windows-msvc"
        traces=[]
        for label,root in zip(("reference","candidate"),roots):
            work=out/label;work.mkdir()
            libraries=[find_library(root,args.platform,name) for name in LIBRARIES]
            defines=["UNICODE","_UNICODE","NDEBUG","WINVER=0x0601","_WIN32_WINNT=0x0601","PSAPI_VERSION=1",
                     "_USE_MATH_DEFINES","_CRT_SECURE_NO_WARNINGS","_SCL_SECURE_NO_WARNINGS","_AFX_NO_MFC_CONTROLS_IN_DIALOGS",
                     "WINDOWS","_WINDOWS","WIN32" if bits==32 else "WIN64"]
            if label=="reference":
                defines += ["XY_DOCUMENT_PINNED_REFERENCE"]
            else:
                defines += ["XY_VSFILTER_RUST_TEXT","XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL"]
                libraries.append(root/"target"/target/"release/xy_vsfilter_ffi.lib")
            includes=[root/path for path in ("src/subtitles","src/filters/transform/vsfilter","src/filters/BaseClasses",
                      "src/thirdparty/boost_lib","src/thirdparty/log4cplus/include","src/thirdparty/VirtualDub/h",
                      "src/thirdparty","include","include/unrar")]
            exe=work/"style-interner.exe"
            command=["cl","/nologo","/Bv","/O2","/Oi","/Ob2","/fp:precise","/EHsc","/std:c++14","/MT","/Gy","/showIncludes"]
            command += ["/D"+define for define in defines]+["/I"+str(path) for path in includes]
            command += [str(SOURCE),"/Fo:"+str(work/"probe.obj"),"/Fe:"+str(exe),"/link","/SUBSYSTEM:CONSOLE","/OPT:REF","/OPT:ICF","/VERBOSE:LIB","/LIBPATH:"+str(root/"lib")]
            command += list(map(str,libraries))+list(SYSTEM_LIBRARIES)
            build={"library_sha256":{str(path):sha(path.read_bytes()) for path in libraries}}
            report["builds"][label]=build;save()
            compiled=run(command,work,label+"-build",240)
            headers={}
            for line in compiled.stdout.decode(errors="replace").splitlines():
                match=re.search(r"([A-Za-z]:\\.+?)\s*$",line)
                if match and Path(match[1]).is_file():
                    headers[match[1]]=sha(Path(match[1]).read_bytes())
            build["included_source_sha256"]=headers
            build["binary_sha256"]=sha(exe.read_bytes())
            for repeat in (1,2):
                result=run([str(exe)]+([] if args.scenario=="interning" else [args.scenario]),work,f"{label}-trace-{repeat}",120)
                if result.stderr:
                    raise ValueError("unexpected style interner probe stderr")
                validate(result.stdout,bits,args.scenario);traces.append(result.stdout)
            for path,digest in {**headers,**build["library_sha256"],str(exe):build["binary_sha256"]}.items():
                if sha(Path(path).read_bytes())!=digest:
                    raise ValueError("native probe input or binary changed")
        released=[]
        for label in ('reference','candidate'):
            for repeat in (1,2):
                result=run([str(out/label/'style-interner.exe'),'released-nan'],out/label,f'{label}-released-nan-{repeat}',30)
                expected=f'style-released-nan pointer_bits={bits} before=1 after=1'.encode()
                if result.stderr or result.stdout.strip()!=expected:raise ValueError("unstable or changed isolated post-release count; unresolved")
                released.append(result.stdout)
        report['isolated_nan_release']={'count_before':1,'count_after':1,'post_release_lookup_executed':False,'trace_sha256':[sha(v) for v in released]}
        lifecycle=[]
        for label in ('reference','candidate'):
            for repeat in (1,2):
                result=run([str(out/label/'style-interner.exe'),'lifecycle'],out/label,f'{label}-lifecycle-{repeat}',30)
                if result.stderr:raise ValueError('unexpected lifecycle stderr')
                validate_lifecycle(result.stdout);lifecycle.append(result.stdout)
        if any(v!=lifecycle[0] for v in lifecycle[1:]):raise ValueError('style lifecycle differs or is nondeterministic')
        report['lifecycle']={'trace_sha256':[sha(v) for v in lifecycle],'trace_lines':5,'late_owner':True,'sticky_status':True}
        compare(traces,bits,args.scenario)
        if [git(root,"rev-parse","HEAD") for root in roots]!=commits or any(git(root,"status","--porcelain","--untracked-files=no") for root in roots):
            raise ValueError("source changed during comparison")
        for path,key in ((SOURCE,"probe_sha256"),(Path(__file__),"runner_sha256")):
            if sha(path.read_bytes())!=report[key]:
                raise ValueError("probe or observer changed during comparison")
        report.update(complete=True,equal=True,repeat_equal=True,inputs_unchanged=True,
                      trace_sha256=[sha(trace) for trace in traces],trace_lines=len(traces[0].splitlines()))
    except Exception as error:
        report["unresolved"]=str(error);save();raise
    save()
    print(json.dumps({"output":str(out),"complete":True,"equal":True,"cases":report["cases"]}))


if __name__=="__main__":
    main()

