#!/usr/bin/env python3
"""Compare actual cleanup raster, blur and chained subpixel variance with initialized-byte scope."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

from document_native_lifecycle import ROOT, PIN, LIBRARIES, SYSTEM_LIBRARIES, find_library, git, sha

SOURCE = ROOT / "tools/compat/overlay_native.cpp"
CASES = 17924
INPUTS = 8

def validate(data,bits):
    rows=data.splitlines();at=1
    if not rows or rows[0]!=f'overlay-stages pointer_bits={bits}'.encode():raise ValueError('wrong overlay architecture')
    status=rb'[0-9a-f]+,[0-9a-f]+,-?[0-9]+,[0-9]+'
    parents={}
    for index in range(CASES):
        if index<17920:
            stage=(index//10)%4;step=index%10
            name=['raster','be','gaussian','combined'][stage]
            shift=4 if step==0 else 9 if step==1 else (step-2)//2+(5 if step%2 else 0)
        else:name='empty';shift=index-17920;step=2;stage=0
        prefix=f'stage index={index} name={name} shift={shift} '.encode()
        if at>=len(rows) or not rows[at].startswith(prefix):raise ValueError('missing/reordered overlay case')
        m=re.fullmatch(rb'offset=-?[0-9]+,-?[0-9]+ size=(-?[0-9]+),(-?[0-9]+) overlay=([0-9]+),([0-9]+),([0-9]+) empty=([01]) full=([01]) memory_delta=(-?[0-9]+) before='+status+rb' after='+status,rows[at][len(prefix):])
        if not m:raise ValueError('invalid overlay metadata/status')
        w,h,width,height,pitch,empty,full,delta=map(int,m.groups());at+=1
        if min(w,h,width,height,pitch)<0 or width<=0 or height<=0 or pitch!=(width+15)&~15 or pitch*height>1000000:raise ValueError('invalid overlay dimensions')
        if step>=2 and (width!=((w+7)>>3)+1 or height!=((h+7)>>3)+1):raise ValueError('invalid variance dimensions')
        geometry=(width,height,pitch)
        if name=='empty' or stage!=2:expected_full=True
        elif step<2:expected_full=False
        else:
            parent_geometry,parent_full=parents['base' if shift<4 else 'first']
            copies=pitch==parent_geometry[2] and width==parent_geometry[0] and height>=parent_geometry[1]
            expected_full=parent_full or not copies
        if full!=expected_full:raise ValueError('unknown padding scope changed')
        if step<2:parents['base']=(geometry,bool(full))
        elif shift<4:parents['first']=(geometry,bool(full))
        if step>=2 and delta!=pitch*height*(1+int(not empty)):raise ValueError('variance allocation accounting changed')
        if (step==1 or (step==0 and stage==0)) and delta!=0:raise ValueError('raster observation or alias copy allocated')
        for plane,present in [(0,True),(1,not empty)]:
            if at>=len(rows):raise ValueError('missing overlay plane')
            p=re.fullmatch(f'stage-plane index={plane} present='.encode()+rb'([01]) ([0-9a-f]*)',rows[at]);at+=1
            count=(pitch if full else width)*height if present else 0
            if not p or int(p[1])!=present or len(p[2])!=count*2:raise ValueError('incomplete overlay bytes')
    if rows[at:]!=[b'overlay-stages-complete cases=17924']:raise ValueError('missing overlay completion')

def compare(traces,bits):
    if len(traces)!=4:raise ValueError('two runs per role required')
    for trace in traces:validate(trace,bits)
    for n,trace in enumerate(traces[1:],1):
        if trace!=traces[0]:
            a=traces[0].splitlines();b=trace.splitlines()
            at=next((i for i,pair in enumerate(zip(a,b)) if pair[0]!=pair[1]),min(len(a),len(b)))
            raise ValueError(f'overlay trace {n} differs at row {at}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-root", type=Path, required=True)
    parser.add_argument("--platform", choices=("Win32", "x64"), required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("Windows ATL/MFC and built native libraries required")
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    roots = (args.reference_root.resolve(), ROOT)
    report = dict(complete=False, equal=False, reference_commit=PIN, platform=args.platform,
                  kind="actual-overlay-stages", cases=CASES, inputs=INPUTS, builds={},
                  probe_sha256=sha(SOURCE.read_bytes()), 
                  runner_sha256=sha(Path(__file__).read_bytes()),
                  environment={key:os.environ.get(key) for key in ("VCToolsVersion","WindowsSDKVersion","VSCMD_ARG_TGT_ARCH")},
                  limits=["17924 actual raster/BeBlur/Gaussian/combined/copy/variance calls: eight paths, four radii and fourteen settings, four shifts, chained shifts and empty source",
                          "Every known plane byte and metadata/status/allocation delta; Gaussian padding is omitted when uninitialized and stays outside parity claims",
                          "Default FP controls plus sticky division-by-zero, Release build; allocator failures, invalid geometry and full host closure remain unclosed"])
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
            exe=work/"overlay.exe"
            command=["cl","/nologo","/Bv","/O2","/Oi","/Ob2","/fp:precise","/EHsc","/std:c++14","/MT","/Gy","/showIncludes"]
            if bits==32:command.append("/arch:IA32")
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
                result=run([str(exe)],work,f"{label}-trace-{repeat}",120)
                if result.stderr:
                    raise ValueError("unexpected overlay probe stderr")
                validate(result.stdout,bits);traces.append(result.stdout)
            for path,digest in {**headers,**build["library_sha256"],str(exe):build["binary_sha256"]}.items():
                if sha(Path(path).read_bytes())!=digest:
                    raise ValueError("native probe input or binary changed")
        compare(traces,bits)
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

