#!/usr/bin/env python3
"""Compare actual cleanup ASS override parser state, failures and byte consumption."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

from document_native_lifecycle import ROOT, PIN, LIBRARIES, SYSTEM_LIBRARIES, find_library, git, sha

SOURCE = ROOT / "tools/compat/override_native.cpp"
OBSERVER = ROOT / "tools/compat/document_native_lifecycle.cpp"
INPUTS = 1144
CASES = 9312
ANIMATIONS = 2304
RELATIVES = 400

def validate(data,bits):
 lines=data.splitlines()
 if not lines or not re.fullmatch(rb"override-provenance pointer_bits="+str(bits).encode()+rb" ansi=\d+ oem=\d+",lines[0]):raise ValueError("missing override architecture/environment")
 if len(lines)!=CASES*10+ANIMATIONS+RELATIVES+2:raise ValueError("missing override observations")
 for index in range(CASES):
  seed=index//(INPUTS+20);case=index%(INPUTS+20);group=int(case>=INPUTS);animate=(seed%2) if not group else int(case-INPUTS>=10)
  prefix=f"override-case index={index} seed={seed} group={group} animate={animate} accepted=1 errno=".encode()
  if not re.fullmatch(re.escape(prefix)+rb"\d+",lines[index*10+1]):raise ValueError("invalid override case/order/state")
  if not all(line.startswith(b'style ') for line in lines[index*10+2:index*10+4]):raise ValueError("missing styles")
  if not lines[index*10+4].startswith(b'state alignment=') or not lines[index*10+5].startswith(b'clipper present='):raise ValueError("missing renderer state")
  for slot,line in enumerate(lines[index*10+6:index*10+11]):
   prefix=f"effect slot={slot} present=".encode()
   if line!=prefix+b'0' and not re.fullmatch(re.escape(prefix)+rb"1( -?\d+){12}",line):raise ValueError("invalid initialized effect fields")
 for index in range(ANIMATIONS):
  prefix=f"animation index={index} seed={index//288} animate={index%2} errno=".encode()
  if not re.fullmatch(re.escape(prefix)+rb"\d+( [0-9a-f]{16}){3}",lines[CASES*10+1+index]):raise ValueError("invalid animation observation")
 for index in range(RELATIVES):
  prefix=f"relative index={index} accepted=1 errno=".encode()
  if not re.fullmatch(re.escape(prefix)+rb"\d+( [0-9a-f]{16}){2}",lines[CASES*10+ANIMATIONS+1+index]):raise ValueError("invalid relative-size observation")
 if lines[-1]!=f"override-complete cases={CASES} inputs={INPUTS} animations={ANIMATIONS} relatives={RELATIVES}".encode():raise ValueError("missing override completion")

def compare(traces,bits):
 if len(traces)!=4:raise ValueError("two executions of each implementation required")
 for trace in traces:validate(trace,bits)
 if any(trace!=traces[0] for trace in traces[1:]):raise ValueError("override traces differ or are nondeterministic")


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
                  kind="actual-renderer-override-control", cases=CASES, inputs=INPUTS, builds={},
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
            exe=work/"override.exe"
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
                    raise ValueError("unexpected ASS override probe stderr")
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

