#!/usr/bin/env python3
"""Capture selected installed ATL map contracts privately in Windows CI output.

Output contains narrowly selected SDK definition excerpts for review. Do not
check those generated excerpts into this repository or upload entire headers.
This is native contract evidence, not a safe-map parity or closure claim.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
MAP_METHODS = ('CAtlMap', '~CAtlMap', 'CPair', 'CNode', 'GetHashTableSize', 'GetStartPosition',
               'GetNext', 'GetNextAssoc', 'GetNextKey', 'Lookup', 'operator[]',
               'SetAt', 'RemoveNode', 'RemoveKey', 'RemoveAll', 'InitHashTable', 'SetOptimalLoad',
               'Rehash', 'PickSize', 'UpdateRehashThresholds', 'NewNode', 'FreeNode',
               'GetNode', 'FindNextNode', 'IsLocked', 'EnableAutoRehash', 'DisableAutoRehash')
TRAIT_CLASSES = ('CStringElementTraits', 'CStringElementTraitsBase', 'CElementTraitsBase')
SELECTED_HEADERS = ('atlcoll.h', 'atlstr.h', 'atlsimpstr.h', 'atlbase.h', 'atlcore.h', 'afxstr.h', 'cstringt.h')
TRAIT_METHODS = ('Hash', 'CompareElements', 'CompareElementsOrdered')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def masked(source):
    # Preserve offsets/newlines while hiding C++ comments and ordinary literals.
    pattern = r'//[^\n]*|/\*[\s\S]*?\*/|(?:L|u8|u|U)?"(?:\\[\s\S]|[^"\\])*"|(?:L|u|U)?\'(?:\\[\s\S]|[^\'\\])*\''
    return re.sub(pattern, lambda m: ''.join('\n' if c == '\n' else ' ' for c in m[0]), source)


def closing(code, start, left='{', right='}'):
    depth = 0
    for index in range(start, len(code)):
        if code[index] == left: depth += 1
        elif code[index] == right:
            depth -= 1
            if depth == 0: return index + 1
    raise ValueError('unterminated selected definition')


def class_ranges(code, name):
    ranges = []
    for match in re.finditer(r'\b(?:class|struct)\s+(?:ATL_NO_VTABLE\s+)?' + re.escape(name) + r'\b', code):
        opening = code.find('{', match.end()); semi = code.find(';', match.end())
        if opening >= 0 and (semi < 0 or opening < semi):
            ranges.append((match.start(), closing(code, opening)))
    return ranges


def select_definitions(source, header):
    """Select methods and type/default declarations, never complete SDK classes."""
    code = masked(source)
    maps = class_ranges(code, 'CAtlMap')
    traits = [span for name in TRAIT_CLASSES for span in class_ranges(code, name)]
    records, spans = [], set()
    def add(start, end, symbol, kind):
        # Include nearby template/signature lines but no previous definition.
        start = code.rfind('\n', 0, start) + 1
        while start > 0:
            previous_end = start - 1
            previous_start = code.rfind('\n', 0, previous_end) + 1
            previous = code[previous_start:previous_end].strip()
            if not previous or previous.endswith((';', '}', '{')) or previous.endswith(':'):
                break
            if source.count('\n', previous_start, end) > 500:
                break
            start = previous_start
        if end - start > 24000 or source.count('\n', start, end) > 500:
            raise ValueError('selected definition exceeds bounded excerpt limit: ' + symbol)
        if (start, end) in spans: return
        spans.add((start, end))
        excerpt = source[start:end]
        records.append({'header': str(header), 'symbol': symbol, 'kind': kind,
                        'first_line': source.count('\n', 0, start) + 1,
                        'last_line': source.count('\n', 0, end) + 1,
                        'sha256_utf8': sha(excerpt.encode('utf-8')), 'excerpt': excerpt})
    method_pattern = r'\b(' + '|'.join(re.escape(name) for name in MAP_METHODS + TRAIT_METHODS if name != 'operator[]') + r')\s*\(|\boperator\s*\[\s*\]\s*\('
    for match in re.finditer(method_pattern, code):
        symbol = match[1] or 'operator[]'
        if symbol == 'CAtlMap' and match.start() > 0 and code[match.start()-1] == '~':
            symbol = '~CAtlMap'
        in_map = any(a <= match.start() < b for a, b in maps)
        in_trait = any(a <= match.start() < b for a, b in traits)
        def declaration_depth(spans):
            for a, b in spans:
                if a <= match.start() < b:
                    fragment = code[code.find('{', a):match.start()]
                    return fragment.count('{') - fragment.count('}')
            return 0
        if in_map and declaration_depth(maps) != 1 and not (symbol in ('CPair', 'CNode') and declaration_depth(maps) == 2):
            in_map = False
        if in_trait and declaration_depth(traits) != 1:
            in_trait = False
        # External template definitions have a qualified class name on signature.
        line_start = code.rfind('\n', 0, match.start()) + 1
        signature_prefix = code[max(0, line_start - 500):match.start()]
        qualified_map = bool(re.search(r'CAtlMap\s*<[^;{}]*::\s*~?$', signature_prefix))
        if not ((symbol in MAP_METHODS and (in_map or qualified_map)) or (symbol in TRAIT_METHODS and in_trait)):
            continue
        # Ignore calls inside function bodies: selected signatures begin with
        # declarations/qualification, rather than control flow/member calls.
        prefix = code[line_start:match.start()]
        if re.search(r'\b(?:return|if|while|for)\b|[.=!]', prefix): continue
        paren = code.find('(', match.start()); after = closing(code, paren, '(', ')')
        if code[after:].lstrip().startswith(','): continue  # initializer call, not constructor definition
        body = code.find('{', after); semi = code.find(';', after)
        end = closing(code, body) if body >= 0 and (semi < 0 or body < semi) else semi + 1
        if end <= after: continue
        add(match.start(), end, symbol, 'definition' if body >= 0 and (semi < 0 or body < semi) else 'declaration')
    for first, last in maps + traits:
        # Capture the argument aliases and member/default declarations needed
        # to interpret selected functions, without retaining the whole class.
        for match in re.finditer(r'^[^\n]*(?:\b(?:KINARGTYPE|INARGTYPE|m_nBins|m_fOptimalLoad|m_fLoThreshold|m_fHiThreshold|m_nHiRehashThreshold|m_nLoRehashThreshold)\b)[^\n]*;', code[first:last], re.M):
            line = match[0]
            if not re.search(r'\b(?:typedef|using|UINT|ULONG|float|double|size_t)\b', line): continue
            add(first + match.start(), first + match.end(), 'argument/load declaration', 'declaration')
    for name in ('CPair', 'CNode'):
        for first, last in class_ranges(code, name):
            if not any(a <= first < b for a, b in maps): continue
            for match in re.finditer(r'^[^\n]*\b(?:m_key|m_value|m_pNext|m_nHash)\b[^\n]*;', code[first:last], re.M):
                line = match[0].strip()
                if re.match(r'(?:const\s+)?(?:K|V|CNode\s*\*|UINT|ULONG)\s*', line):
                    add(first + match.start(), first + match.end(), name + ' field declaration', 'declaration')
    for match in re.finditer(r'\bs_anPrimes\s*\[[^\]]*\]\s*=\s*\{', code):
        end = closing(code, code.find('{', match.start()))
        if code[end:].lstrip().startswith(';'): end = code.find(';', end) + 1
        add(match.start(), end, 's_anPrimes', 'definition')
    for match in re.finditer(r'^[^\n]*\btypedef\b[^\n]*\bCString(?:W|A)?\s*;', code, re.M):
        add(match.start(), match.end(), 'CString alias', 'declaration')
    if sum(len(row['excerpt']) for row in records) > 180000:
        raise ValueError('selected SDK excerpts exceed total limit')
    return records


PROBE = r'''// Generated bounded native contract observation, no SDK implementation copied.
#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS
#include <afx.h>
#include <afxwin.h>
#include <atlbase.h>
#include <atlcoll.h>
#include <type_traits>
#include <typeinfo>
#include <cstdio>
struct Token { unsigned id; };
using Traits=ATL::CStringElementTraits<CString>;
using Map=ATL::CAtlMap<CString,Token*,Traits>;
static unsigned seq=0;
static void units(const CString& key) {
    for(int i=0;i<key.GetLength();++i)printf("%04x",unsigned(static_cast<unsigned short>(key[i])));
}
static void state(const char* op,const Map& map) {
    printf("{\"event\":\"state\",\"seq\":%u,\"op\":\"%s\",\"count\":%zu,\"bins\":%u,\"order\":[",seq++,op,map.GetCount(),map.GetHashTableSize());
    POSITION pos=map.GetStartPosition();bool comma=false;
    while(pos){const auto pair=map.GetNext(pos);if(comma)printf(",");comma=true;
        printf("{\"id\":%u,\"length\":%d,\"units\":\"",pair->m_value->id,pair->m_key.GetLength());units(pair->m_key);printf("\"}");}
    puts("]}");
}
static CString key(unsigned n){CString s;s.Format(L"Style%03u",n);return s;}
int main(){
    if(!AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0)){fputs("AfxWinInit failed\n",stderr);return 2;}
    printf("{\"event\":\"provenance\",\"pointer_bits\":%zu,\"msc\":%d,\"atl\":%d,\"cstring_type\":\"%s\",\"hash_bits\":%zu,\"arg_is_lpcwstr\":%s,\"arg_is_const_cstring_ref\":%s}\n",sizeof(void*)*8,_MSC_FULL_VER,_ATL_VER,typeid(CString).name(),sizeof(decltype(Traits::Hash(L"A")))*8,std::is_same<Traits::INARGTYPE,LPCWSTR>::value?"true":"false",std::is_same<Traits::INARGTYPE,const CString&>::value?"true":"false");
    Token tokens[130];for(unsigned i=0;i<130;++i)tokens[i].id=i;
    Map map;state("construct",map);
    for(unsigned i=0;i<128;++i){map[key(i)]=&tokens[i];state("insert",map);}
    map[key(0)]=&tokens[128];state("existing-update",map);
    for(unsigned i=0;i<128;i+=2){map.RemoveKey(key(i));state("remove",map);}
    for(unsigned i=0;i<128;i+=2){map[key(i)]=&tokens[i];state("reinsert",map);}
    Map copy;POSITION pos=map.GetStartPosition();while(pos){const auto pair=map.GetNext(pos);copy[pair->m_key]=pair->m_value;state("ordered-copy",copy);}
    map.RemoveAll();state("clear",map);map[key(7)]=&tokens[7];state("reuse",map);
    const wchar_t raw[][5]={{0},{0,L'X'},{L'A'},{L'A',0,L'B'},{L'A',0,L'C'},{L'a'},{0xd800},{0xffff},{L'A',L'0'},{L'A',L'1'}};
    const int lengths[]={0,2,1,3,3,1,1,1,2,2};
    Map names;
    for(unsigned i=0;i<10;++i){CString s(raw[i],lengths[i]);printf("{\"event\":\"key\",\"index\":%u,\"length\":%d,\"units\":\"",i,s.GetLength());units(s);printf("\",\"hash\":%lu,\"equal_A\":%s}\n",static_cast<unsigned long>(Traits::Hash(s)),Traits::CompareElements(s,L"A")?"true":"false");names[s]=&tokens[i];state("raw-key-insert",names);}
    puts("{\"event\":\"complete\",\"marker\":\"document-atl-contract-v1 complete\"}");return 0;
}
'''


def validate_trace(data, bits):
    rows = [json.loads(line) for line in data.decode('ascii').splitlines()]
    if not rows or rows[0].get('event') != 'provenance' or rows[0].get('pointer_bits') != bits:
        raise ValueError('missing architecture provenance')
    if 'StrTraitMFC' not in rows[0].get('cstring_type', ''):
        raise ValueError('actual MFC CString type provenance missing')
    if rows[-1] != {'event': 'complete', 'marker': 'document-atl-contract-v1 complete'}:
        raise ValueError('missing completion')
    states = [row for row in rows if row.get('event') == 'state']
    expected = {'construct': 1, 'insert': 128, 'existing-update': 1, 'remove': 64,
                'reinsert': 64, 'ordered-copy': 128, 'clear': 1, 'reuse': 1, 'raw-key-insert': 10}
    if {op: sum(r['op'] == op for r in states) for op in expected} != expected or len(states) != sum(expected.values()):
        raise ValueError('incomplete bounded history')
    if [r['seq'] for r in states] != list(range(len(states))): raise ValueError('state sequence')
    for row in states:
        if row['count'] != len(row['order']) or not 0 <= row['count'] <= 128 or row['bins'] <= 0:
            raise ValueError('invalid state shape')
        for key in row['order']:
            if not 0 <= key['id'] < 130 or not re.fullmatch(r'(?:[0-9a-f]{4})*', key['units']) or len(key['units']) != key['length'] * 4:
                raise ValueError('invalid key trace')
    keys = [row for row in rows if row.get('event') == 'key']
    if [r['index'] for r in keys] != list(range(10)): raise ValueError('incomplete key histories')
    if any(r.get('event') not in ('provenance', 'state', 'key', 'complete') for r in rows) or sum(r.get('event') == 'complete' for r in rows) != 1:
        raise ValueError('extra trace event')
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--platform', choices=('Win32','x64'), required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if os.name != 'nt': parser.error('Windows installed ATL developer environment required')
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    report={'schema':'document-atl-contract-v1','complete':False,'rust_parity':False,'platform':args.platform,'executions':[],
            'reference_commit':json.loads((ROOT/'tools/compat/reference.json').read_text())['commit'],
            'candidate_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT).decode().strip(),
            'environment':{k:os.environ.get(k) for k in ('VCToolsVersion','VCToolsInstallDir','WindowsSDKVersion','VSCMD_ARG_TGT_ARCH','INCLUDE','LIB')},
            'limits':['Selected installed SDK excerpts are private generated review artifacts, not checked-in source.','No ATL algorithm inferred from traces; reviewer must inspect definitions.','No AddStyle/CopyStyles/native allocation failure/host closure claim.','CString uses MFC afx.h/afxwin.h before ATL, matching document stdafx explicit-constructor macro.','Load settings captured from selected constructor/SetOptimalLoad definitions; probe does not modify defaults.']}
    stable={}
    def protect(path):
        path=Path(path).resolve();stable[str(path)]=sha(path.read_bytes());return stable[str(path)]
    def save(): (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    def run(command,name,timeout):
        row={'name':name,'command':command};report['executions'].append(row);save()
        try: result=subprocess.run(command,cwd=out,capture_output=True,timeout=timeout)
        except subprocess.TimeoutExpired as error:
            (out/(name+'.stdout')).write_bytes(error.stdout or b'');(out/(name+'.stderr')).write_bytes(error.stderr or b'');row['status']='timeout';raise
        (out/(name+'.stdout')).write_bytes(result.stdout);(out/(name+'.stderr')).write_bytes(result.stderr)
        row.update(returncode=result.returncode,stdout_sha256=protect(out/(name+'.stdout')),stderr_sha256=protect(out/(name+'.stderr')))
        if result.returncode: raise RuntimeError(name+' failed; unresolved')
        return result
    save()
    try:
        protect(Path(__file__))
        subprocess.check_output(['git','ls-files','--error-unmatch',Path(__file__).resolve().relative_to(ROOT).as_posix()],cwd=ROOT)
        if subprocess.check_output(['git','status','--porcelain','--untracked-files=no'],cwd=ROOT):raise ValueError('tracked source not clean')
        compiler=shutil.which('cl')
        if not compiler:raise ValueError('cl missing')
        report['compiler_sha256']=protect(compiler)
        source=out/'probe.cpp';source.write_text(PROBE);protect(source)
        exe=out/'probe.exe'
        built=run([compiler,'/nologo','/Bv','/showIncludes','/std:c++14','/EHsc','/O2','/MT','/fp:precise','/DNDEBUG','/DUNICODE','/D_UNICODE','/D_AFX_NO_MFC_CONTROLS_IN_DIALOGS','/DWINVER=0x0601','/D_WIN32_WINNT=0x0601',str(source),'/Fo:'+str(out/'probe.obj'),'/Fe:'+str(exe),'/link','/VERBOSE:LIB'],'build',120)
        headers={};libraries={}
        log=(built.stdout+b'\n'+built.stderr).decode(errors='replace')
        for line in log.splitlines():
            match=re.search(r'([A-Za-z]:\\.+?)\s*$',line)
            if match and Path(match[1]).is_file():headers[match[1]]=protect(match[1])
        for match in re.finditer(r'([A-Za-z]:\\[^\r\n]*?\.lib)(?=[:\s(]|$)',log,re.I):
            if Path(match[1]).is_file():libraries[match[1]]=protect(match[1])
        report.update(included_header_sha256=headers,linker_observed_library_sha256=libraries,binary_sha256=protect(exe))
        excerpts=[]
        for header in headers:
            if Path(header).name.lower() in SELECTED_HEADERS:
                try:
                    raw=Path(header).read_bytes();text=raw.decode('utf-8-sig',errors='strict')
                    excerpts+=select_definitions(text,header)
                except (ValueError, UnicodeError) as error:
                    report.setdefault('source_extraction_errors', []).append({'header': header, 'error': str(error)})
        required=('CAtlMap','Lookup','Rehash','PickSize','UpdateRehashThresholds','SetOptimalLoad','Hash','CompareElements','operator[]','RemoveNode','RemoveKey','RemoveAll','s_anPrimes','CPair')
        private=out/'selected-sdk-definitions.private.json';private.write_text(json.dumps({'redistribution':'Private review excerpts only; do not check in SDK source','definitions':excerpts},indent=2)+'\n');report['selected_definitions_sha256']=protect(private)
        report['definition_index']=[{k:v for k,v in r.items() if k!='excerpt'} for r in excerpts]
        report['missing_required_definitions']=[name for name in required if not any(r['symbol']==name and r['kind']=='definition' for r in excerpts)]
        traces=[]
        for i in range(2):
            result=run([str(exe)],'trace-'+str(i),30)
            if result.stderr:raise ValueError('runtime stderr nonempty')
            rows=validate_trace(result.stdout,32 if args.platform=='Win32' else 64);traces.append(result.stdout)
        if traces[0]!=traces[1]:raise ValueError('native histories nondeterministic')
        structured=out/'observations.json';structured.write_text(json.dumps(rows,indent=2)+'\n');protect(structured)
        if subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT).decode().strip()!=report['candidate_commit'] or subprocess.check_output(['git','status','--porcelain','--untracked-files=no'],cwd=ROOT):raise ValueError('source changed')
        for path,expected in stable.items():
            if sha(Path(path).read_bytes())!=expected:raise ValueError('input/artifact changed: '+path)
        report.update(repeat_equal=True,stderr_empty=True,artifact_sha256=stable,behavior_capture_complete=True)
        if report['missing_required_definitions'] or report.get('source_extraction_errors'):
            raise ValueError('required selected ATL definitions unavailable; native observations and partial excerpts retained')
        report['complete']=True
    except Exception as error:
        report['unresolved']=str(error);raise
    finally:save()


if __name__=='__main__':main()
