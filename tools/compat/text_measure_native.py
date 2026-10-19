#!/usr/bin/env python3
"""Compare actual CText arithmetic, raw floating inputs, partial failure and ambient flags."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

from document_native_lifecycle import ROOT, PIN, LIBRARIES, SYSTEM_LIBRARIES, find_library, git, sha

SOURCE = ROOT / "tools/compat/text_measure_native.cpp"
STEPS = 432
CASES = STEPS
INPUTS = 432

RAW_BITS=[0,0x8000000000000000,0x3ff4000000000000,0xbff4000000000000,0x4059000000000000,0xc059000000000000,1,0x0010000000000000,0x7fefffffffffffff,0x7ff0000000000000,0xfff0000000000000,0x7ff8000000000001,0x7ff0000000000001,0x41e0000000000000,0xc1e0000000000000,0x43e0000000000000,0x4059000000000001,0x4058ffffffffffff]

def validate(data,bits,raw_canonical=False):
    rows=data.splitlines()
    mode='raw-canonical' if raw_canonical else 'construction'
    if not rows or rows[0]!=f'text-measure pointer_bits={bits} mode={mode}'.encode():raise ValueError('wrong measurement architecture/mode')
    status=rb'[0-9a-f]+,[0-9a-f]+,-?[0-9]+,[0-9]+'
    at=1
    for index in range(432):
        if index%4==0:
            expected=RAW_BITS[(index//4)%18]
            pattern=f'text-measure-input index={index} bits='.encode()+(f'{expected:016x}'.encode() if raw_canonical else rb'[0-9a-f]{16}')
            if at>=len(rows) or not re.fullmatch(pattern,rows[at]):raise ValueError('invalid canonical measurement input')
            at+=1
        prefix=f'text-measure-case index={index} sticky={index//216} field={(index//72)%3} bits={RAW_BITS[(index//4)%18]:016x} history={index%4} '.encode()
        if at>=len(rows):raise ValueError('missing measurement case')
        match=re.fullmatch(prefix+rb'events=([0-9]+) output=-?[0-9]+,-?[0-9]+,-?[0-9]+ before='+status+rb' after='+status,rows[at])
        if not match:raise ValueError('invalid measurement case/order')
        events=int(match[1])
        if events<3 or events>16 or at+1+events>len(rows):raise ValueError('invalid measurement event count')
        calls=rows[at+1:at+1+events]
        for row in calls:
            if not re.fullmatch(rb'text-measure-call name=(?:select|extent|version) count=[0-9]+ result=-?[0-9]+ width=-?[0-9]+ before='+status+rb' after='+status,row):raise ValueError('invalid measurement call')
        if not calls[0].startswith(b'text-measure-call name=select ') or not calls[-1].startswith(b'text-measure-call name=select '):raise ValueError('missing selection/restoration')
        at+=events+1
    if rows[at:]!=[b'text-measure-complete cases=432']:raise ValueError('missing measurement completion')


def compare(traces,bits,raw_canonical=False):
    if len(traces)!=4:raise ValueError('two runs per role required')
    for t in traces:validate(t,bits,raw_canonical)
    for n,t in enumerate(traces[1:],1):
        if t!=traces[0]:
            a=traces[0].splitlines();b=t.splitlines();at=next(i for i,pair in enumerate(zip(a,b)) if pair[0]!=pair[1])
            raise ValueError(f'DC trace {n} differs at row {at}: {a[at]!r} / {b[at]!r}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-root", type=Path, required=True)
    parser.add_argument("--platform", choices=("Win32", "x64"), required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--raw-canonical", action="store_true", help="isolate measurement using identical raw canonical inputs; does not validate style construction")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("Windows ATL/MFC and built native libraries required")
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    roots = (args.reference_root.resolve(), ROOT)
    report = dict(complete=False, equal=False, reference_commit=PIN, platform=args.platform,
                  kind="actual-text-measurement-arithmetic", cases=CASES, inputs=INPUTS, builds={},
                  mode="raw-canonical" if args.raw_canonical else "construction",
                  probe_sha256=sha(SOURCE.read_bytes()), 
                  runner_sha256=sha(Path(__file__).read_bytes()),
                  environment={key:os.environ.get(key) for key in ("VCToolsVersion","WindowsSDKVersion","VSCMD_ARG_TGT_ARCH")},
                  limits=["432 actual measurement histories with raw scales/spacing, scripted extents/failures/legacy-version branch, integer overflow and ambient floating flags; NaN style owners stay alive until process exit",
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
            exe=work/"renderer-dc.exe"
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
                result=run([str(exe)]+(["raw-canonical"] if args.raw_canonical else []),work,f"{label}-trace-{repeat}",120)
                if result.stderr:
                    raise ValueError("unexpected renderer DC probe stderr")
                validate(result.stdout,bits,args.raw_canonical);traces.append(result.stdout)
            for path,digest in {**headers,**build["library_sha256"],str(exe):build["binary_sha256"]}.items():
                if sha(Path(path).read_bytes())!=digest:
                    raise ValueError("native probe input or binary changed")
        compare(traces,bits,args.raw_canonical)
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

