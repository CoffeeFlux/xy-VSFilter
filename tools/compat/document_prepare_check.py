#!/usr/bin/env python3
"""Bounded pinned preparation suffix observations using actual Windows ATL.

No trimming, locale classification, WebVTT, codepage conversion or append claim.
Generate-only is portable; observations require a VS developer prompt with ATL.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import struct

ROOT = Path(__file__).resolve().parents[2]
PIN = '4911cacb102bc0cbd41449793e54473265b04e62'
MARKER = 'document-prepare-v1 complete'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def fixtures():
    # Explicit initialized code units, including embedded NUL sections; no stress.
    texts = [[], [65], [13], [10], [13, 10], [10, 13, 10], [92, 78],
             [65, 0, 66], [10, 0, 10], [13, 0, 13],
             [65, 0, 10, 66, 0, 13, 10, 67], [0, 10, 0, 10],
             [0, 13, 0, 13], [0xd800, 10, 0xdc00],
             [0xd800, 0, 13, 10, 0xdc00], [32, 9, 10, 13, 32],
             [10, 10, 10], [13, 13, 13], [0, 0], [65, 0, 0, 10, 0, 66]]
    default = list(map(ord, 'Default'))
    styles = [[], default, list(map(ord, 'default')), [42], [42, 42],
              [42, 65], [32, 42, 65], [0, 65], [42, 0, 42, 65],
              default + [0, 42, 0xd800], [0xd800, 42], [42, 0xdc00],
              [42, 42, 0, 42, 0, 66], [0, 0], [42, 68, 101, 102, 97, 117, 108, 116]]
    rows = [(mode, text, style) for mode in ('add', 'entry') for text in texts for style in styles]
    rows += [('set', text, []) for text in texts]
    return rows


def extract(source):
    result = {}
    for name, signature in [('add', 'void CSimpleTextSubtitle::Add('),
                            ('entry', 'void CSimpleTextSubtitle::AddSTSEntryOnly('),
                            ('set', 'void CSimpleTextSubtitle::SetStr(')]:
        method = source.index(signature)
        start = source.index("    str.Replace", method) if name == 'set' else source.index("    str.Remove", method)
        end = source.index('\n\n    if(stse.fUnicode', start) if name == 'set' else source.index('    STSEntry sub;', start)
        suffix = source[start:end].rstrip()
        if name == 'set' and suffix.strip() != 'str.Replace(L"\\n", L"\\\\N");':
            raise ValueError('SetStr suffix fence changed')
        if name != 'set' and ('str.Remove' not in suffix or 'style.TrimLeft' not in suffix):
            raise ValueError('Add suffix fence changed')
        result[name] = {'normalized_character_start': start, 'normalized_character_end': end, 'sha256': sha(suffix.encode('utf-8')), 'text': suffix}
    return result


def units(values):
    return 'CStringW()' if not values else 'Make({' + ','.join(str(v) for v in values) + '})'


def generate(extracted):
    functions = '\n'.join('static void ' + name + '(CStringW& str, CStringW& style) {\n' + value['text'] + '\n}\n' for name, value in extracted.items())
    cases = '\n'.join('Run("%s",%d,%s,%s);' % (mode, i, units(text), units(style)) for i, (mode, text, style) in enumerate(fixtures()))
    return r'''#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS
#include <afx.h>
#include <afxwin.h>
#include <atlstr.h>
#include <typeinfo>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
static CStringW g_default_style(L"Default");
static CStringW Make(std::initializer_list<unsigned> units) {
    CStringW value; wchar_t* p=value.GetBufferSetLength((int)units.size());
    size_t i=0; for(unsigned unit:units) p[i++]=(wchar_t)unit;
    value.ReleaseBufferSetLength((int)units.size()); return value;
}
''' + functions + r'''
static void Print(const CStringW& value) {
    std::printf(" %d ",value.GetLength());
    if(value.IsEmpty()) std::printf("-");
    for(int i=0;i<value.GetLength();++i) std::printf("%04x",(unsigned)(uint16_t)value[i]);
}
static void Run(const char* mode,unsigned index,CStringW text,CStringW style) {
    std::printf("input %s %u",mode,index); Print(text); Print(style); std::puts("");
    if(mode[0]=='a') add(text,style); else if(mode[0]=='e') entry(text,style); else set(text,style);
    std::printf("value %s %u",mode,index); Print(text); Print(style); std::puts("");
}
int main() {
    if(!AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0)){ std::fputs("AfxWinInit failed\n",stderr); return 2; }
    static_assert(sizeof(wchar_t)==2,"Windows UTF16 required");
    std::printf("provenance pointer_bits=%u\n",(unsigned)(sizeof(void*)*8));
    std::printf("cstring_type %s\n",typeid(CStringW).name());
''' + cases + '\nstd::puts("' + MARKER + '"); return 0;\n}\n'


def encoded(values):
    return ''.join(f'{v:04x}' for v in values) or '-'


def validate_trace(data, bits):
    if len(data) > 1024 * 1024:
        raise ValueError('trace exceeds bound')
    lines = data.decode('ascii').splitlines()
    rows = fixtures()
    if len(lines) != 2 * len(rows) + 3 or lines[0] != f'provenance pointer_bits={bits}' or lines[-1] != MARKER:
        raise ValueError('trace shape/provenance/completion mismatch')
    if not lines[1].startswith('cstring_type ') or 'StrTraitMFC<wchar_t' not in lines[1]:
        raise ValueError('actual MFC wchar_t CString type provenance missing')
    for i, (mode, text, style) in enumerate(rows):
        expected = f'input {mode} {i} {len(text)} {encoded(text)} {len(style)} {encoded(style)}'
        if lines[2 + 2*i] != expected:
            raise ValueError('input fixture mismatch')
        fields = lines[3 + 2*i].split(' ')
        if len(fields) != 7 or fields[:3] != ['value', mode, str(i)]:
            raise ValueError('output identity mismatch')
        for length, raw in [(fields[3], fields[4]), (fields[5], fields[6])]:
            if not re.fullmatch(r'0|[1-9][0-9]*', length) or int(length) > 512:
                raise ValueError('invalid output length')
            if raw != '-' and not re.fullmatch(r'(?:[0-9a-f]{4})+', raw):
                raise ValueError('invalid raw UTF16')
            if (0 if raw == '-' else len(raw)//4) != int(length):
                raise ValueError('raw UTF16 length mismatch')
    return len(rows)


def compare_traces(left, right, bits):
    count = validate_trace(left, bits)
    validate_trace(right, bits)
    if left != right:
        raise ValueError('native observations are not repeatable')
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--generate-only', action='store_true')
    parser.add_argument('--bits', type=int, choices=[32, 64])
    parser.add_argument('--platform', choices=['Win32', 'x64'])
    parser.add_argument('--reference-root', type=Path)
    args = parser.parse_args()
    out = args.output.resolve(); out.mkdir(parents=True, exist_ok=True)
    report = {'schema': 1, 'complete': False, 'reference_commit': PIN,
              'rust_parity': False, 'scope': 'actual ATL final Add/EntryOnly suffix and SetStr LF-only; excludes FastTrim, locale, WebVTT, charset conversion, append and callbacks'}
    path = out / 'report.json'
    def save():
        path.write_text(json.dumps(report, indent=2) + '\n')
    save()
    try:
        bits = args.bits or ({'Win32':32,'x64':64}.get(args.platform))
        if args.bits and args.platform and args.bits != {'Win32':32,'x64':64}[args.platform]:
            raise ValueError('platform/bits mismatch')
        candidate_commit = subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,timeout=30).decode().strip()
        candidate_status = subprocess.check_output(['git','status','--porcelain','--untracked-files=no'],cwd=ROOT,timeout=30)
        if candidate_status and not args.generate_only:
            raise ValueError('candidate tracked source must be clean')
        candidate_inputs = {str(Path(__file__).relative_to(ROOT)): sha(Path(__file__).read_bytes()), 'tools/compat/test_document_prepare_check.py': sha((ROOT/'tools/compat/test_document_prepare_check.py').read_bytes())}
        report.update(candidate_commit=candidate_commit, candidate_tracked_clean=not bool(candidate_status), candidate_status_sha256=sha(candidate_status), candidate_source_sha256=candidate_inputs)
        reference = args.reference_root.resolve() if args.reference_root else ROOT
        if args.reference_root:
            head = subprocess.check_output(['git','rev-parse','HEAD'],cwd=reference,timeout=30).decode().strip()
            dirty = subprocess.check_output(['git','status','--porcelain'],cwd=reference,timeout=30)
            if head != PIN or dirty:
                raise ValueError('reference root must be exact pinned clean checkout')
        elif not args.generate_only:
            raise ValueError('runtime requires --reference-root')
        raw = subprocess.check_output(['git','show',f'{PIN}:src/subtitles/STS.cpp'],cwd=reference,timeout=30)
        if args.reference_root and (reference/'src/subtitles/STS.cpp').read_bytes().replace(b'\r\n',b'\n') != raw.replace(b'\r\n',b'\n'):
            raise ValueError('reference working source differs from pinned blob')
        extracted = extract(raw.decode('utf-8-sig').replace('\r\n','\n'))
        cpp = generate(extracted).encode('utf-8')
        generated = out / 'document_prepare_probe.cpp'; generated.write_bytes(cpp)
        runner_hash = sha(Path(__file__).read_bytes())
        report.update(reference_source_sha256=sha(raw), runner_sha256=runner_hash,
                      extracted_suffixes=extracted, generated_sha256=sha(cpp), fixture_count=len(fixtures()),
                      host_platform=os.name, requested_platform=args.platform, fixture_sha256=sha(json.dumps(fixtures(),separators=(',',':')).encode()), source_encoding='UTF8 BOM removed; CRLF normalized to LF before exact suffix slicing')
        save()
        if args.generate_only:
            report['generated_only'] = True; save(); return 0
        if os.name != 'nt' or bits is None:
            raise ValueError('runtime requires Windows and explicit --bits')
        compiler = shutil.which('cl')
        if not compiler:
            raise ValueError('cl not found; use VS developer prompt with ATL')
        exe = out / 'document_prepare_probe.exe'
        command = [compiler,'/nologo','/Bv','/showIncludes','/MT','/O2','/EHsc','/std:c++14','/DUNICODE','/D_UNICODE','/DNDEBUG','/D_AFX_NO_MFC_CONTROLS_IN_DIALOGS','/DWINVER=0x0601','/D_WIN32_WINNT=0x0601',str(generated),'/Fe:'+str(exe),'/link','/VERBOSE:LIB']
        report.update(command=command, compiler_path=compiler, compiler_sha256=sha(Path(compiler).read_bytes()), runtime='static CRT/MFC /MT; actual MFC CStringW StrTraitMFC<wchar_t>; AfxWinInit', sanitizer='none'); save()
        built = subprocess.run(command,cwd=out,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
        (out/'build.log').write_bytes(built.stdout)
        if built.returncode:
            raise ValueError('native compilation failed')
        headers = {}
        for line in built.stdout.decode('utf-8',errors='replace').splitlines():
            match = re.search(r'([A-Za-z]:[\\/].*)$',line)
            if match:
                header = Path(match.group(1).strip())
                if header.is_file() and header != generated and header.suffix.lower() in ('.h','.hpp','.inl'):
                    headers[str(header)] = sha(header.read_bytes())
        selected_headers = ['afx.h','afxwin.h','afxstr.h','atlstr.h','cstringt.h']
        if not all(any(Path(p).name.lower() == name for p in headers) for name in selected_headers):
            raise ValueError('actual MFC/ATL include provenance missing')
        libraries = {}
        for match in re.finditer(r'([A-Za-z]:\\[^\r\n]*?\.lib)(?=[:\s(]|$)',built.stdout.decode(errors='replace'),re.I):
            library = Path(match[1])
            if library.is_file(): libraries[str(library)] = sha(library.read_bytes())
        binary = exe.read_bytes()
        if binary[:2] != b'MZ' or len(binary) < 64:
            raise ValueError('invalid PE binary')
        pe = struct.unpack_from('<I',binary,60)[0]
        if binary[pe:pe+4] != b'PE\0\0':
            raise ValueError('invalid PE signature')
        machine = struct.unpack_from('<H',binary,pe+4)[0]
        if machine != {32:0x14c,64:0x8664}[bits]:
            raise ValueError('PE architecture mismatch')
        report.update(exe_sha256=sha(binary), pe_machine=machine, header_sha256=headers,
                      build_log_sha256=sha(built.stdout), selected_include_names=selected_headers, linker_observed_library_sha256=libraries, explicit_link_libraries=[], implicit_link_libraries='ATL/CRT header directives and compiler defaults retained; full build log archived')
        save()
        traces = []
        for i in range(2):
            result = subprocess.run([str(exe)],cwd=out,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=20)
            (out/f'trace-{i}.txt').write_bytes(result.stdout)
            (out/f'stderr-{i}.txt').write_bytes(result.stderr)
            if result.returncode or result.stderr:
                raise ValueError('native execution failed or emitted stderr')
            traces.append(result.stdout)
        report['observations'] = compare_traces(*traces,bits)
        if sha(Path(__file__).read_bytes()) != runner_hash or generated.read_bytes() != cpp:
            raise ValueError('harness source changed during run')
        if sha(Path(compiler).read_bytes()) != report['compiler_sha256']:
            raise ValueError('compiler changed during run')
        if args.reference_root and subprocess.check_output(['git','status','--porcelain'],cwd=reference,timeout=30):
            raise ValueError('reference checkout changed during run')
        for relative, expected in candidate_inputs.items():
            if sha((ROOT/relative).read_bytes()) != expected:
                raise ValueError('candidate source changed during run')
        if subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,timeout=30).decode().strip() != candidate_commit:
            raise ValueError('candidate commit changed during run')
        if subprocess.check_output(['git','status','--porcelain','--untracked-files=no'],cwd=ROOT,timeout=30) != candidate_status:
            raise ValueError('candidate tracked source status changed during run')
        if args.reference_root and subprocess.check_output(['git','rev-parse','HEAD'],cwd=reference,timeout=30).decode().strip() != PIN:
            raise ValueError('reference commit changed during run')
        for header, expected in {**headers, **libraries}.items():
            if sha(Path(header).read_bytes()) != expected:
                raise ValueError('native header changed during run')
        if sha(subprocess.check_output(['git','show',f'{PIN}:src/subtitles/STS.cpp'],cwd=reference,timeout=30)) != sha(raw):
            raise ValueError('reference source changed')
        report.update(cstring_type=traces[0].decode('ascii').splitlines()[1].removeprefix('cstring_type '), complete=True, candidate_source_stable=True, repeat_equal=True, stderr_empty=True, pointer_bits=bits,
                      trace_sha256=[sha(t) for t in traces])
        save(); return 0
    except (ValueError, OSError, struct.error, subprocess.SubprocessError) as error:
        report['error'] = str(error); save(); return 1


if __name__ == '__main__':
    raise SystemExit(main())
