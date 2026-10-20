#!/usr/bin/env python3
"""Compare actual cleanup Rasterize metadata, initialized planes, aliases and cache accounting."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

from document_native_lifecycle import ROOT, PIN, LIBRARIES, SYSTEM_LIBRARIES, find_library, git, sha

SOURCE = ROOT / "tools/compat/raster_native.cpp"
CASES = 512
INPUTS = 16

def validate(data,bits):
    rows=data.splitlines();at=1
    if not rows or rows[0]!=f'raster pointer_bits={bits}'.encode():raise ValueError('wrong raster architecture')
    status=rb'[0-9a-f]+,[0-9a-f]+,-?[0-9]+,[0-9]+'
    previous_count=0;previous_body=False;previous_border=False
    for index in range(CASES):
        scenario=index//32;radius=index//4%8;shift=index%4
        prefix=f'raster-case index={index} scenario={scenario} radius={radius} shift={shift} missing=0 result=1 '.encode()
        if at>=len(rows) or not rows[at].startswith(prefix):raise ValueError('missing/reordered raster case')
        match=re.fullmatch(rb'origin=-?[0-9]+,-?[0-9]+ scan=(-?[0-9]+),(-?[0-9]+) offset=(-?[0-9]+),(-?[0-9]+) size=(-?[0-9]+),(-?[0-9]+) overlay=([0-9]+),([0-9]+),([0-9]+) wide_empty=([01]) memory_delta=(-?[0-9]+) before='+status+rb' after='+status,rows[at][len(prefix):])
        if not match:raise ValueError('invalid raster metadata/status')
        sx,sy,ox,oy,w,h,ow,oh,pitch,empty,delta=map(int,match.groups());at+=1
        zero=sx==0 or sy==0
        if zero:
            if any([ox,oy,w,h,ow,oh,pitch,empty]):raise ValueError('empty raster not cleaned')
        elif min(w,h,ow,oh,pitch)<=0 or ow!=((w+7)>>3)+1 or oh!=((h+7)>>3)+1 or pitch!=(ow+15)&~15:
            raise ValueError('inconsistent raster dimensions')
        count=pitch*oh
        if count>1000000:raise ValueError('unexpected raster size')
        retained=bool(shift%2 and previous_body)
        expected_delta=count*(int(not zero)+int(not zero and not empty))-previous_count*(int(previous_body)+int(previous_border)-int(retained))
        if delta!=expected_delta:raise ValueError('cache allocation count changed')
        for label,present,length in [('body',not zero,count),('border',not zero and not empty,count),('retained',bool(shift%2 and previous_body),previous_count)]:
            if at>=len(rows):raise ValueError('missing raster plane')
            plane=re.fullmatch(rb'raster-plane '+label.encode()+rb' present=([01]) bytes=([0-9]+) ([0-9a-f]*)',rows[at]);at+=1
            if not plane or int(plane[1])!=present or int(plane[2])!=(length if present else 0) or len(plane[3])!=int(plane[2])*2:
                raise ValueError('incomplete raster plane')
        previous_count=count;previous_body=not zero;previous_border=not zero and not empty
    if rows[at:]!=[b'raster-complete cases=512']:raise ValueError('missing raster completion')

def compare(traces,bits):
    if len(traces)!=4:raise ValueError('two runs per role required')
    for trace in traces:validate(trace,bits)
    for n,trace in enumerate(traces[1:],1):
        if trace!=traces[0]:
            a=traces[0].splitlines();b=trace.splitlines()
            at=next((i for i,pair in enumerate(zip(a,b)) if pair[0]!=pair[1]),min(len(a),len(b)))
            raise ValueError(f'raster trace {n} differs at row {at}')


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
                  kind="actual-rasterize", cases=CASES, inputs=INPUTS, builds={},
                  probe_sha256=sha(SOURCE.read_bytes()), 
                  runner_sha256=sha(Path(__file__).read_bytes()),
                  environment={key:os.environ.get(key) for key in ("VCToolsVersion","WindowsSDKVersion","VSCMD_ARG_TGT_ARCH")},
                  limits=["512 reused-overlay calls from 16 paths, 8 radii and 4 shifts; all initialized pixels and padding, retained prior body aliases and allocation-count deltas",
                          "Actual cleanup Rasterize; blur and variance padding, allocation failure and corrupt geometry remain unclosed",
                          "Default FP controls plus sticky division-by-zero, Release build; no full-host closure claimed"])
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
            exe=work/"raster.exe"
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
                    raise ValueError("unexpected raster probe stderr")
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

