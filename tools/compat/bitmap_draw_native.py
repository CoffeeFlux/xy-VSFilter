#!/usr/bin/env python3
"""Compare actual cleanup bitmap initialization, drawing, final alpha and aliases."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

from document_native_lifecycle import ROOT, PIN, LIBRARIES, SYSTEM_LIBRARIES, find_library, git, sha

SOURCE = ROOT / "tools/compat/bitmap_draw_native.cpp"
CASES = 7074
INPUTS = 3528 + 18

def validate(data,bits):
    rows=data.splitlines()
    if not rows or rows[0]!=f'bitmap-draw pointer_bits={bits}'.encode():raise ValueError('wrong bitmap architecture')
    if len(rows)!=CASES*7+2:raise ValueError('missing bitmap records')
    status=rb'[0-9a-f]+,[0-9a-f]+,-?[0-9]+,[0-9]+'
    for index in range(CASES):
        row=rows[index*7+1];prefix=f'bitmap-case index={index} '.encode()
        if not row.startswith(prefix) or not re.fullmatch(rb'width=[0-6] layout=[01] addition=[01] origin=[0-2] clip=[0-2] color=[0-6] flags=[0-3] sticky=[01] empty=[01]',row[len(prefix):]):raise ValueError('invalid bitmap input')
        for stage in range(6):
            prefix=f'bitmap-stage index={index} stage={stage} '.encode();row=rows[index*7+2+stage]
            if not row.startswith(prefix):raise ValueError('reordered bitmap stage')
            match=re.fullmatch(rb'present=([01]) alias=([01]) dirty=-?[0-9]+,-?[0-9]+,-?[0-9]+,-?[0-9]+ memory_delta=-?[0-9]+ before='+status+rb' after='+status+rb'(?: fields=([01]),(-?[0-9]+),(-?[0-9]+),([0-9]+),([0-9]+),([0-9]+) residue=([0-9]+) offsets=([0-9]+),(-?[0-9]+),(-?[0-9]+),(-?[0-9]+) bytes=([0-9]+) pixels=([0-9a-f]*))?',row[len(prefix):])
            if not match:raise ValueError('invalid bitmap stage')
            if stage==5:
                if match[1]!=b'0' or match[2]!=b'0' or match[3] is not None:raise ValueError('unexpected released output')
            else:
                if match[1]!=b'1' or match[3] is None or (stage==4 and match[2]!=b'1'):raise ValueError('missing bitmap/lifetime')
                layout,x,y,w,h,pitch,residue=map(int,match.group(3,4,5,6,7,8,9))
                offsets=list(map(int,match.group(10,11,12,13)))
                count=int(match[14]);expected=(pitch if layout else w)*h*4
                if pitch!=((w+15)&~15)*(1 if layout else 4) or residue!=(x*(1 if layout else 4))&15:raise ValueError('wrong bitmap allocation geometry')
                if offsets!=([0,pitch*h,pitch*h*2,pitch*h*3] if layout else [0,-1,-1,-1]):raise ValueError('wrong bitmap plane offsets')
                if count!=expected or len(match[15])!=count*2:raise ValueError('invalid initialized bitmap bytes')
    if rows[-1]!=f'bitmap-draw-complete cases={CASES}'.encode():raise ValueError('missing bitmap completion')


def compare(traces,bits):
    if len(traces)!=4:raise ValueError('two runs per role required')
    for trace in traces:validate(trace,bits)
    for n,trace in enumerate(traces[1:],1):
        if trace!=traces[0]:
            a=traces[0].splitlines();b=trace.splitlines()
            at=next((i for i,pair in enumerate(zip(a,b)) if pair[0]!=pair[1]),min(len(a),len(b)))
            raise ValueError(f'bitmap trace {n} differs at row {at}')


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
                  kind="actual-bitmap-draw", cases=CASES, inputs=INPUTS, builds={},
                  probe_sha256=sha(SOURCE.read_bytes()),
                  runner_sha256=sha(Path(__file__).read_bytes()),
                  environment={key:os.environ.get(key) for key in ("VCToolsVersion","WindowsSDKVersion","VSCMD_ARG_TGT_ARCH")},
                  limits=["7056 drawing cases (3528 inputs with two sticky states), plus 18 zero-sized bitmap cases, twice per role",
                          "Seven widths around SIMD thresholds, packed/planar, normal/addition dispatch, three origins and clips, seven color histories including backward/empty segments and nonzero first boundary",
                          "Creation, two draws, final packed alpha flip, surviving shared bitmap after alpha release, final release. Exact initialized bytes, plane offsets, alignment residue, native allocation deltas and FP/errno/LastError",
                          "Packed padding and uninitialized bpp are never sampled. Planar addition follows production DrawItem routing to normal Draw. Malformed unbounded switches, unsupported layout, debug-box macro, failure injection and arbitrary aliases/concurrency remain unclosed"])




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
            exe=work/"bitmap-draw-observer.exe"
            command=["cl","/nologo","/Bv","/O2","/Oi","/Ob2","/fp:precise","/EHsc","/std:c++14","/MT","/Gy","/showIncludes"]
            if bits==32:command.append("/arch:IA32")
            command += ["/D"+define for define in defines]+["/I"+str(path) for path in includes]
            command += [str(SOURCE),"/Fo:"+str(work/"probe.obj"),"/Fe:"+str(exe),"/link","/SUBSYSTEM:CONSOLE","/MANIFEST:EMBED","/MANIFESTUAC:level='asInvoker' uiAccess='false'","/OPT:REF","/OPT:ICF","/VERBOSE:LIB","/LIBPATH:"+str(root/"lib")]
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
                    raise ValueError("unexpected filter probe stderr")
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
