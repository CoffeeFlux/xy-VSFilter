#!/usr/bin/env python3
"""Compare actual CText measurement, paths, selection and scripted GDI failures."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

from document_native_lifecycle import ROOT, PIN, LIBRARIES, SYSTEM_LIBRARIES, find_library, git, sha

SOURCE = ROOT / "tools/compat/renderer_dc_text_native.cpp"
STEPS = 40
CASES = STEPS
INPUTS = 40

def validate(data,bits):
    rows=data.splitlines()
    if not rows or rows[0]!=f'dc-text pointer_bits={bits}'.encode():raise ValueError('wrong text DC architecture')
    status=rb'[0-9a-f]+,[0-9a-f]+,-?[0-9]+,[0-9]+'
    at=1
    for index in range(40):
        spacing=index//20;text=(index//5)%4;mode=index%5
        prefix=f'dc-text-case index={index} spacing={spacing} text={text} mode={mode} '.encode()
        if at>=len(rows):raise ValueError('missing text case')
        match=re.fullmatch(prefix+rb'events=([0-9]+) measurement_events=([0-9]+) result=[01] measured=-?[0-9]+,-?[0-9]+,-?[0-9]+ selected_stock=[01] selected_null=[01] points=([0-9]+) measure_status='+status+rb' path_status='+status,rows[at])
        if not match:raise ValueError('invalid text case/order')
        events,measured,points=map(int,match.groups())
        if measured>events or events>128 or at+2+events>len(rows):raise ValueError('invalid event count')
        path=rows[at+1].split(b' ')
        if path[0]!=b'dc-text-path' or len(path)!=points+1 or any(not re.fullmatch(rb'[0-9a-f]{2}:-?[0-9]+,-?[0-9]+',p) for p in path[1:]):raise ValueError('invalid full path payload')
        for row in rows[at+2:at+2+events]:
            if not re.fullmatch(rb'dc-text-call name=(?:select|extent|text|begin|close|end|abort|get) a=-?[0-9]+ b=-?[0-9]+ result=-?[0-9]+ extent=-?[0-9]+,-?[0-9]+ before='+status+rb' after='+status,row):raise ValueError('invalid text GDI call')
        at+=events+2
    if rows[at:]!=[b'dc-text-complete cases=40']:raise ValueError('missing text completion')


def compare(traces,bits):
    if len(traces)!=4:raise ValueError('two runs per role required')
    for t in traces:validate(t,bits)
    for n,t in enumerate(traces[1:],1):
        if t!=traces[0]:
            a=traces[0].splitlines();b=t.splitlines();at=next(i for i,pair in enumerate(zip(a,b)) if pair[0]!=pair[1])
            raise ValueError(f'DC trace {n} differs at row {at}: {a[at]!r} / {b[at]!r}')


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
                  kind="actual-renderer-device-context-text-path", cases=CASES, inputs=INPUTS, builds={},
                  probe_sha256=sha(SOURCE.read_bytes()), 
                  runner_sha256=sha(Path(__file__).read_bytes()),
                  environment={key:os.environ.get(key) for key in ("VCToolsVersion","WindowsSDKVersion","VSCMD_ARG_TGT_ARCH")},
                  limits=["Forty actual text/path histories: spacing, embedded NUL, surrogate, empty input, four GDI failure sites, raw points/types, selection state and separate floating/errno/last-error observations",
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
                result=run([str(exe)],work,f"{label}-trace-{repeat}",120)
                if result.stderr:
                    raise ValueError("unexpected renderer DC probe stderr")
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

