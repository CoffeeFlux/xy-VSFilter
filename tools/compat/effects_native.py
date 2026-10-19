#!/usr/bin/env python3
"""Compare actual cleanup effects parser state, failures and byte consumption."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

from document_native_lifecycle import ROOT, PIN, LIBRARIES, SYSTEM_LIBRARIES, find_library, git, sha

SOURCE = ROOT / "tools/compat/effects_native.cpp"
OBSERVER = ROOT / "tools/compat/document_native_lifecycle.cpp"
INPUTS = 1116
COLUMN = 953
SEQUENCE = 18
CASES = 13608

def validate(data,bits):
 lines=data.splitlines()
 if not lines or not re.fullmatch(rb"effects-provenance pointer_bits="+str(bits).encode()+rb" ansi=\d+ oem=\d+",lines[0]):raise ValueError("missing effects architecture/environment")
 if len(lines)!=CASES*6+12*6+2:raise ValueError("missing effects observations")
 offset=1
 for seed in range(12):
  for case in range(INPUTS+SEQUENCE):
   index=seed*(INPUTS+SEQUENCE)+case;group=int(case>=INPUTS)
   pattern=rb"effects-case index="+str(index).encode()+rb" seed="+str(seed).encode()+rb" group="+str(group).encode()+rb" kind=[01] accepted=(-1|[01]) errno=\d+ wrap=-?\d+ hard=-?\d+ animated=[01] input=(\d+):([0-9a-f]*)"
   row=re.fullmatch(pattern,lines[offset])
   if not row or int(row[2])*4!=len(row[3]):raise ValueError("invalid effects case/order/state")
   for slot,line in enumerate(lines[offset+1:offset+6]):
    prefix=f"slot {slot} present=".encode();suffix=(rb" type="+str(slot).encode()) if slot>=3 else b""
    if line!=prefix+b"0" and not re.fullmatch(re.escape(prefix)+b"1"+suffix+rb"( -?\d+){12}",line):raise ValueError("invalid initialized effect snapshot")
   offset+=6
  if lines[offset]!=f"effects-cleared seed={seed}".encode() or lines[offset+1:offset+6]!=[f"slot {s} present=0".encode() for s in range(5)]:raise ValueError("effects clear failed")
  offset+=6
 if lines[-1]!=f"effects-complete cases={CASES}".encode():raise ValueError("missing effects completion")

def compare(traces,bits):
 if len(traces)!=4:raise ValueError("two executions of each implementation required")
 for trace in traces:validate(trace,bits)
 if any(trace!=traces[0] for trace in traces[1:]):raise ValueError("effects traces differ or are nondeterministic")


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
                  kind="actual-renderer-effects-control", cases=CASES, inputs=INPUTS, builds={},
                  probe_sha256=sha(SOURCE.read_bytes()), observer_sha256=sha(OBSERVER.read_bytes()),
                  runner_sha256=sha(Path(__file__).read_bytes()),
                  environment={key:os.environ.get(key) for key in ("VCToolsVersion","WindowsSDKVersion","VSCMD_ARG_TGT_ARCH")},
                  limits=["Initialized document fields; no uninitialized simplicity reads",
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
            exe=work/"effects.exe"
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
                    raise ValueError("unexpected effects probe stderr")
                validate(result.stdout,bits);traces.append(result.stdout)
            for path,digest in {**headers,**build["library_sha256"],str(exe):build["binary_sha256"]}.items():
                if sha(Path(path).read_bytes())!=digest:
                    raise ValueError("native probe input or binary changed")
        compare(traces,bits)
        if [git(root,"rev-parse","HEAD") for root in roots]!=commits or any(git(root,"status","--porcelain","--untracked-files=no") for root in roots):
            raise ValueError("source changed during comparison")
        for path,key in ((SOURCE,"probe_sha256"),(OBSERVER,"observer_sha256"),(Path(__file__),"runner_sha256")):
            if sha(path.read_bytes())!=report[key]:
                raise ValueError("probe or observer changed during comparison")
        report.update(complete=True,equal=True,repeat_equal=True,inputs_unchanged=True,
                      trace_sha256=[sha(trace) for trace in traces],trace_lines=len(traces[0].splitlines()))
    except Exception as error:
        report["unresolved"]=str(error);save();raise
    save()
    print(json.dumps({"output":str(out),"complete":True,"equal":True,"cases":CASES}))


if __name__=="__main__":
    main()

