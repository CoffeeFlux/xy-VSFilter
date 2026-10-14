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

CSIZE_HEADERS = ('atltypes.h', 'afx.h', 'afx.inl')


def conditional_context(source, offset):
    """Record source guards, without pretending to evaluate SDK conditions."""
    lines = masked(source[:offset]).splitlines()
    full = source.splitlines()
    masked_full = masked(source).splitlines()
    stack = []
    for index, line in enumerate(lines):
        match = re.match(r'\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)', line)
        if not match:
            continue
        directive, expression = match.groups()
        raw_directive = full[index].strip()
        continuation = index + 1
        while raw_directive.endswith('\\') and continuation < len(full):
            raw_directive += '\n' + full[continuation].strip()
            continuation += 1
        if directive in ('if', 'ifdef', 'ifndef'):
            # A conventional header guard is scaffolding, not a choice between
            # alternative constructor implementations. Other guards stay unknown.
            guard = re.fullmatch(r'\s*([A-Za-z_]\w*)\s*', expression)
            following = next((value.strip() for value in masked_full[index + 1:]
                              if value.strip()), '')
            scaffolding = all(not value.strip() or re.fullmatch(r'\s*#\s*pragma\s+once\s*', value)
                              for value in lines[:index])
            include_guard = (scaffolding and not stack and directive == 'ifndef' and guard is not None
                             and re.match(r'#\s*define\s+' + re.escape(guard[1]) + r'\b', following) is not None)
            stack.append({'first_line': index + 1, 'directive': raw_directive,
                          'branch_line': index + 1, 'branch': raw_directive,
                          'include_guard': include_guard})
        elif directive in ('elif', 'else'):
            if not stack:
                raise ValueError('unmatched conditional branch before CSize definition')
            stack[-1] = {**stack[-1], 'branch_line': index + 1, 'branch': raw_directive,
                         'include_guard': False}
        elif stack:
            stack.pop()
        else:
            raise ValueError('unmatched conditional end before CSize definition')
    if sum(len(row['directive']) + len(row['branch']) for row in stack) > 2048 or len(stack) > 16:
        raise ValueError('CSize conditional context exceeds bound')
    return stack


def select_csize_definitions(source, header):
    """Only CSize class/base declaration and zero-argument ctor signatures/body.

    No construction or read is performed. Conditional alternatives are retained
    as unresolved source authority, rather than selected using guessed macros.
    """
    code = masked(source)
    ranges = class_ranges(code, 'CSize')
    records = []

    def add(start, end, symbol, kind):
        line_start = code.rfind('\n', 0, start) + 1
        if code[line_start:start].strip() in ('', 'inline', '_ATLINLINE'):
            start = line_start
        excerpt = source[start:end]
        if len(excerpt.encode('utf-8')) > 2048 or excerpt.count('\n') + 1 > 40:
            raise ValueError('CSize excerpt exceeds bounded source selection')
        internal = []
        raw_lines = excerpt.splitlines()
        for number, line in enumerate(masked(excerpt).splitlines()):
            if re.match(r'\s*#\s*(?:if|ifdef|ifndef|elif|else|endif)\b', line):
                directive = raw_lines[number].strip()
                following = number + 1
                while directive.endswith('\\') and following < len(raw_lines):
                    directive += '\n' + raw_lines[following].strip()
                    following += 1
                internal.append({'first_line': source.count('\n', 0, start) + number + 1,
                                 'directive': directive})
        records.append({'header': str(header), 'symbol': symbol, 'kind': kind,
                        'first_line': source.count('\n', 0, start) + 1,
                        'last_line': source.count('\n', 0, end) + 1,
                        'internal_conditions': internal,
                        'source_utf8_sha256': sha(source.encode('utf-8')),
                        'sha256_utf8': sha(excerpt.encode('utf-8')), 'excerpt': excerpt,
                        'conditional_context': conditional_context(source, start)})

    for first, last in ranges:
        opening = code.find('{', first, last)
        add(first, opening, 'CSize class/base', 'declaration')
    pattern = r'\bCSize\s*::\s*CSize\s*\(\s*(?:void\s*)?\)|\bCSize\s*\(\s*(?:void\s*)?\)'
    for match in re.finditer(pattern, code):
        if match.start() > 0 and code[match.start() - 1] == '~':
            continue
        qualified = '::' in match[0]
        if not qualified:
            enclosing = next(((a, b) for a, b in ranges if a < match.start() < b), None)
            if enclosing is None:
                continue
            opening = code.find('{', enclosing[0])
            prefix = code[opening:match.start()]
            if prefix.count('{') - prefix.count('}') != 1:
                continue
        after = match.end()
        body, semi = code.find('{', after), code.find(';', after)
        if semi < 0 and body < 0:
            raise ValueError('unterminated CSize constructor signature')
        is_body = body >= 0 and (semi < 0 or body < semi)
        end = closing(code, body) if is_body else semi + 1
        # A defaulted/deleted declaration is source evidence too; no guessed body.
        signature = code[after:body if is_body else semi]
        if ':' in signature:
            raise ValueError('CSize constructor initializer requires explicit source review')
        kind = 'definition' if is_body or re.search(r'=\s*default\b', signature) else 'declaration'
        if re.search(r'=\s*delete\b', signature):
            kind = 'deleted-declaration'
        add(match.start(), end, 'CSize::CSize default', kind)
    if len(records) > 16 or sum(len(row['excerpt'].encode('utf-8')) for row in records) > 8192:
        raise ValueError('CSize excerpts exceed total source selection bound')
    return records


def csize_source_contract(records):
    defaults = [row for row in records if row['symbol'] == 'CSize::CSize default' and row['kind'] == 'definition']
    declarations = [row for row in records if row['symbol'] == 'CSize class/base']
    unknown = [dict(header=row['header'], first_line=row['first_line'],
                    conditional_context=[c for c in row['conditional_context'] if not c['include_guard']],
                    internal_conditions=row['internal_conditions'])
               for row in records if row['internal_conditions'] or any(not c['include_guard'] for c in row['conditional_context'])]
    unambiguous = len(defaults) == 1 and len(declarations) == 1
    return {'scope': 'Source-only selected CSize default constructor; no default object values read or inferred',
            'source_capture_complete': unambiguous, 'complete': unambiguous and not unknown,
            'default_definition_count': len(defaults), 'class_declaration_count': len(declarations),
            'active_conditions_resolved': not unknown, 'active_conditions_unresolved': unknown,
            'unresolved': (([] if unambiguous else ['Missing or ambiguous selected CSize default constructor/class declaration'])
                           + (['Active CSize conditional directives are not evaluated'] if unknown else []))}

CSTRING_PREPARATION_HEADERS = ('cstringt.h', 'atlsimpstr.h')
CSTRING_PREPARATION_METHODS = ('Remove', 'Replace', 'TrimLeft', 'IsEmpty')


def select_cstring_preparation(source, header):
    """Select only the raw-code-unit overloads used by native Add preparation."""
    code = masked(source)
    owners = [(name, first, last) for name in ('CStringT', 'CSimpleStringT')
              for first, last in class_ranges(code, name)]
    records = []

    def add(start, end, owner, symbol, kind):
        line_start = code.rfind('\n', 0, start) + 1
        if not re.search(r'[;{}]', code[line_start:start]):
            start = line_start
        excerpt = source[start:end]
        if len(excerpt.encode('utf-8')) > 24000 or excerpt.count('\n') + 1 > 500:
            raise ValueError('CString preparation excerpt exceeds bound: ' + symbol)
        internal = []
        raw_lines = excerpt.splitlines()
        for index, line in enumerate(masked(excerpt).splitlines()):
            if re.match(r'\s*#\s*(?:if|ifdef|ifndef|elif|else|endif)\b', line):
                directive = raw_lines[index].strip()
                following = index + 1
                while directive.endswith('\\') and following < len(raw_lines):
                    directive += '\n' + raw_lines[following].strip()
                    following += 1
                internal.append({'first_line': source.count('\n', 0, start) + index + 1,
                                 'directive': directive})
        records.append({'header': str(header), 'symbol': symbol, 'owner': owner, 'kind': kind,
                        'first_line': source.count('\n', 0, start) + 1,
                        'last_line': source.count('\n', 0, end) + 1,
                        'source_utf8_sha256': sha(source.encode('utf-8')),
                        'sha256_utf8': sha(excerpt.encode('utf-8')), 'excerpt': excerpt,
                        'conditional_context': conditional_context(source, start),
                        'internal_conditions': internal})

    for owner, first, last in owners:
        opening = code.find('{', first, last)
        add(first, opening, owner, owner + ' class/base', 'declaration')
        # Only argument/base aliases, at class depth one; never unrelated fields.
        for match in re.finditer(r'\b(?:typedef|using)\b[^;{}]*\b(?:XCHAR|PCXSTR|CThisSimpleString)\b[^;{}]*;', code[opening:last]):
            start = opening + match.start()
            prefix = code[opening:start]
            if prefix.count('{') - prefix.count('}') == 1:
                add(start, opening + match.end(), owner, owner + ' argument/base alias', 'declaration')
    pattern = r'\b(' + '|'.join(CSTRING_PREPARATION_METHODS) + r')\s*\('
    for match in re.finditer(pattern, code):
        owner = None
        enclosing = next(((name, a, b) for name, a, b in owners if a <= match.start() < b), None)
        if enclosing:
            name, first, _ = enclosing
            opening = code.find('{', first)
            fragment = code[opening:match.start()]
            if fragment.count('{') - fragment.count('}') == 1:
                owner = name
        else:
            prefix = code[max(0, match.start() - 500):match.start()]
            qualified = re.search(r'\b(CStringT|CSimpleStringT)\s*<[^;{}]*::\s*$', prefix)
            if qualified:
                owner = qualified[1]
        if owner is None:
            continue
        method = match[1]
        paren = code.find('(', match.start())
        after = closing(code, paren, '(', ')')
        params = code[paren + 1:after - 1]
        chars = len(re.findall(r'\bXCHAR\b', params))
        strings = len(re.findall(r'\bPCXSTR\b', params))
        if method == 'IsEmpty':
            if params.strip() not in ('', 'void'):
                continue
            suffix = '()'
        elif owner != 'CStringT' or re.search(r'[*&]', params):
            continue
        elif method in ('Remove', 'TrimLeft') and chars == 1 and strings == 0 and ',' not in params:
            suffix = '(XCHAR)'
        elif method == 'Replace' and strings == 2 and chars == 0 and params.count(',') == 1:
            suffix = '(PCXSTR,PCXSTR)'
        else:
            continue
        body, semi = code.find('{', after), code.find(';', after)
        is_body = body >= 0 and (semi < 0 or body < semi)
        if body < 0 and semi < 0:
            raise ValueError('Unterminated CString preparation signature')
        end = closing(code, body) if is_body else semi + 1
        add(match.start(), end, owner, owner + '::' + method + suffix,
            'definition' if is_body else 'declaration')
    if len(records) > 32 or sum(len(row['excerpt'].encode('utf-8')) for row in records) > 80000:
        raise ValueError('CString preparation selections exceed total bound')
    return records


def cstring_preparation_contract(records):
    required = {'Remove(XCHAR)': 'CStringT::Remove(XCHAR)',
                'Replace(PCXSTR,PCXSTR)': 'CStringT::Replace(PCXSTR,PCXSTR)',
                'TrimLeft(XCHAR)': 'CStringT::TrimLeft(XCHAR)',
                'IsEmpty()': None}
    counts = {name: sum(row['kind'] == 'definition' and
                       (row['symbol'] == symbol if symbol else row['symbol'] in ('CStringT::IsEmpty()', 'CSimpleStringT::IsEmpty()'))
                       for row in records) for name, symbol in required.items()}
    unknown = [{'header': row['header'], 'first_line': row['first_line'],
                'symbol': row['symbol'],
                'conditional_context': [c for c in row['conditional_context'] if not c['include_guard']],
                'internal_conditions': row['internal_conditions']}
               for row in records if row['internal_conditions'] or any(not c['include_guard'] for c in row['conditional_context'])]
    unique = all(count == 1 for count in counts.values())
    return {'scope': 'Selected SDK source overloads for native Add final preparation; no string transformation runtime parity claim',
            'source_capture_complete': unique, 'complete': unique and not unknown,
            'definition_counts': counts, 'active_conditions_resolved': not unknown,
            'active_conditions_unresolved': unknown,
            'unresolved': (([] if unique else ['Missing or ambiguous required CString preparation definitions'])
                           + (['Selected CString conditional directives are not evaluated'] if unknown else []))}

CSTRING_HELPER_HEADERS = ('cstringt.h', 'atlsimpstr.h', 'atlstr.h', 'afxstr.h')


def select_cstring_helpers(source, header):
    """Narrow source evidence, not a preprocessor or inferred trait dispatch."""
    code = masked(source)
    records = []

    def add(start, end, owner, symbol, kind):
        start = code.rfind('\n', 0, start) + 1
        excerpt = source[start:end]
        if len(excerpt.encode('utf-8')) > 12000 or excerpt.count('\n') > 250:
            raise ValueError('CString helper excerpt exceeds bound')
        internal = [line.strip() for line in masked(excerpt).splitlines()
                    if re.match(r'\s*#\s*(?:if|ifdef|ifndef|elif|else|endif)\b', line)]
        records.append({'header': str(header), 'owner': owner, 'symbol': symbol, 'kind': kind,
                        'first_line': source.count('\n', 0, start) + 1,
                        'last_line': source.count('\n', 0, end) + 1,
                        'source_utf8_sha256': sha(source.encode('utf-8')),
                        'sha256_utf8': sha(excerpt.encode('utf-8')), 'excerpt': excerpt,
                        'conditional_context': conditional_context(source, start),
                        'internal_conditions': internal})

    for owner in ('ChTraitsBase', 'ChTraitsCRT', 'StrTraitATL', 'StrTraitMFC', 'CStringT', 'CSimpleStringT'):
        for first, last in class_ranges(code, owner):
            opening = code.find('{', first, last)
            declaration = code[first:opening]
            # Explicit narrow specializations never establish the wide dispatch.
            specialization = re.search(r'\b' + owner + r'\s*<([^>]*)>', declaration)
            if specialization and not re.search(r'\bwchar_t\b', specialization[1]):
                continue
            if owner in ('ChTraitsBase', 'ChTraitsCRT') and not specialization:
                # Generic/ANSI declarations do not establish the wchar_t aliases.
                continue
            template_prefix = code[max(0, first - 2000):first]
            template_match = re.search(r'\btemplate\s*<[^;{}]*>\s*$', template_prefix)
            if template_match:
                add(max(0, first - 2000) + template_match.start(), first, owner,
                    owner + ' helper template', 'declaration')
            add(first, opening, owner, owner + ' helper class/base', 'declaration')
            for alias in re.finditer(r'\b(?:typedef|using)\b[^;{}]*;', code[opening:last]):
                start = opening + alias.start()
                prefix = code[opening:start]
                if prefix.count('{') - prefix.count('}') != 1:
                    continue
                declaration_text = alias[0]
                # Keep only dispatch/string argument aliases, never data members.
                names = re.findall(r'\b(XCHAR|PXSTR|PCXSTR|StringTraits|CThisSimpleString)\b', declaration_text)
                if names:
                    add(start, opening + alias.end(), owner,
                        owner + ' helper alias', 'declaration')
            for match in re.finditer(r'\b(CharNext|StringFindString|StringLength|SafeStringLen)\s*\(', code[opening:last]):
                start = opening + match.start()
                prefix = code[opening:start]
                if prefix.count('{') - prefix.count('}') != 1:
                    continue
                paren = code.find('(', start)
                after = closing(code, paren, '(', ')')
                params = code[paren + 1:after - 1]
                # Capture only pointer string overloads, never character overloads.
                if not re.search(r'\b(?:LPCWSTR|LPWSTR|PCXSTR|PXSTR)\b|\bwchar_t\s*\*', params):
                    continue
                if match[1] == 'StringFindString' and (params.count(',') != 1 or re.search(r'\bLPWSTR\b', params.split(',')[0])):
                    continue
                if match[1] != 'StringFindString' and ',' in params:
                    continue
                body, semi = code.find('{', after), code.find(';', after)
                is_body = body >= 0 and (semi < 0 or body < semi)
                if body < 0 and semi < 0:
                    raise ValueError('Unterminated CString helper signature')
                end = closing(code, body) if is_body else semi + 1
                add(start, end, owner, owner + '::' + match[1],
                    'definition' if is_body else 'declaration')
    if len(records) > 40 or sum(len(r['excerpt'].encode('utf-8')) for r in records) > 80000:
        raise ValueError('CString helper selections exceed total bound')
    return records


def cstring_helper_contract(records):
    required = ('ChTraitsCRT::CharNext', 'ChTraitsCRT::StringFindString',
                'ChTraitsCRT::SafeStringLen')
    counts = {symbol: sum(r['symbol'] == symbol and r['kind'] == 'definition' for r in records)
              for symbol in required}
    bases = {owner: sum(r['symbol'] == owner + ' helper class/base' for r in records)
             for owner in ('ChTraitsBase', 'ChTraitsCRT', 'StrTraitATL', 'StrTraitMFC', 'CStringT', 'CSimpleStringT')}
    unknown = [dict(header=r['header'], first_line=r['first_line'], symbol=r['symbol'],
                    conditional_context=[c for c in r['conditional_context'] if not c['include_guard']],
                    internal_conditions=r['internal_conditions']) for r in records
               if r['internal_conditions'] or any(not c['include_guard'] for c in r['conditional_context'])]
    aliases = {owner + '::' + name: sum(r['owner'] == owner and r['symbol'] == owner + ' helper alias'
                and re.search(r'\b' + name + r'\b', r['excerpt']) is not None for r in records)
               for owner in ('ChTraitsBase', 'CSimpleStringT', 'CStringT') for name in ('XCHAR', 'PCXSTR')}
    templates = {owner: sum(r['symbol'] == owner + ' helper template' for r in records)
                 for owner in ('StrTraitATL', 'StrTraitMFC', 'CStringT', 'CSimpleStringT')}
    overrides = [r['symbol'] for r in records if r['owner'] in ('StrTraitATL', 'StrTraitMFC')
                 and r['kind'] == 'definition']
    unique = (not overrides and all(n == 1 for n in counts.values()) and all(n == 1 for n in bases.values())
              and all(n >= 1 for n in aliases.values()) and all(n == 1 for n in templates.values()))
    return {'scope': 'Selected wide helper definitions and trait class dispatch declarations; source only, no runtime parity',
            'definition_counts': counts, 'dispatch_declaration_counts': bases,
            'dispatch_alias_counts': aliases, 'dispatch_template_counts': templates,
            'unsupported_trait_overrides': overrides,
            'source_capture_complete': unique, 'complete': unique and not unknown,
            'active_conditions_resolved': not unknown, 'active_conditions_unresolved': unknown,
            'unresolved': ([] if unique else ['Missing or ambiguous wide helper/dispatch source']) +
                          (['Selected helper conditions are not evaluated'] if unknown else [])}


CSIZE_CONTEXT_SOURCE = '''// Source-only preprocessing; no object declarations or execution.
#include <stdafx.h>
#include <STS.h>
#if defined(_ATL_STATIC_LIB_IMPL)
XY_DOCUMENT_CSIZE_ATL_STATIC_LIB_IMPL_DEFINED
#else
XY_DOCUMENT_CSIZE_ATL_STATIC_LIB_IMPL_UNDEFINED
#endif
'''


def csize_context_command(compiler, source, platform, experimental):
    # Native subtitles Release context; /E stops before compilation/linking and retains source origins.
    flags = ['/nologo', '/Bv', '/E', '/showIncludes', '/std:c++14', '/EHsc',
             '/O2', '/Oi', '/Ob2', '/MT', '/fp:precise', '/Y-']
    defines = ['NDEBUG', '_CRT_SECURE_NO_WARNINGS', '_SCL_SECURE_NO_WARNINGS',
               'WINVER=0x0601', '_WIN32_WINNT=0x0601', 'PSAPI_VERSION=1',
               '_WINDOWS', 'WINDOWS', '_USE_MATH_DEFINES', '_UNICODE', 'UNICODE',
               'XY_VSFILTER_RUST_TEXT']
    defines += ['_WIN64', 'WIN64'] if platform == 'x64' else ['WIN32']
    if experimental:
        defines.append('XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL')
    includes = ['src/subtitles', 'src/filters/transform/vsfilter',
                'src/filters/BaseClasses', 'src/thirdparty/boost_lib',
                'src/thirdparty/log4cplus/include', 'src/thirdparty/VirtualDub/h',
                'src/thirdparty', 'include', 'include/unrar']
    return [compiler, *flags, *('/D' + value for value in defines),
            *('/I' + str(ROOT / value) for value in includes), str(source)]


def csize_body_tokens(record):
    code = masked(record['excerpt'])
    opening = code.find('{')
    if opening < 0:
        return None
    return re.sub(r'\s+', '', code[opening:closing(code, opening)])


def select_active_csize(stdout, source_name, selected_source_records):
    """Keep bounded active ctor excerpts, never a full preprocessed SDK dump."""
    text = stdout.decode('utf-8', errors='replace')
    marker = re.findall(r'^XY_DOCUMENT_CSIZE_ATL_STATIC_LIB_IMPL_(DEFINED|UNDEFINED)\s*$', text, re.M)
    diagnostics, code = [], []
    for line in text.splitlines(keepends=True):
        if 'Note: including file:' in line or line.startswith('XY_DOCUMENT_CSIZE_ATL_STATIC_LIB_IMPL_'):
            diagnostics.append(line)
        elif not re.match(r'^\s*#\s*(?:line\s+)?\d+\s+"', line):
            code.append(line)
    records = select_csize_definitions(''.join(code), 'preprocessed:' + source_name)
    status = csize_source_contract(records)
    active = [row for row in records if row['symbol'] == 'CSize::CSize default' and row['kind'] == 'definition']
    original = [row for row in selected_source_records if row['symbol'] == 'CSize::CSize default' and row['kind'] == 'definition']
    matched = (len(active) == len(original) == 1 and csize_body_tokens(active[0]) is not None
               and csize_body_tokens(active[0]) == csize_body_tokens(original[0]))
    exact = not any('\ufffd' in row['excerpt'] for row in records)
    complete = status['complete'] and matched and len(marker) == 1 and exact
    return {'complete': complete, 'scope': 'Actual active preprocessed default constructor; no CSize object constructed or read',
            'preprocessed_stream_sha256': sha(stdout), 'preprocessed_stream_bytes': len(stdout),
            'full_preprocessed_stream_retained': False,
            'macro_state_after_includes': marker[0].lower() if len(marker) == 1 else 'unresolved',
            'active_default_body_matches_selected_sdk_source': matched,
            'active_selection_status': status, 'selected_definitions': records,
            'selected_sdk_default_sha256': original[0]['sha256_utf8'] if len(original) == 1 else None}, ''.join(diagnostics).encode('utf-8')


def preprocessed_origins(text):
    """MSVC /E line markers; unknown/unmapped lines never establish origins."""
    lines, origins = [], []
    header, number = None, None
    for line in text.splitlines(keepends=True):
        marker = re.match(r'^\s*#\s*(?:line\s+)?(\d+)\s+"([^"\r\n]+)"\s*$', line)
        if marker:
            header = marker[2].replace('\\\\', '\\')
            number = int(marker[1])
            continue
        if 'Note: including file:' in line or line.startswith('XY_DOCUMENT_CSIZE_'):
            continue
        lines.append(line)
        origins.append((header, number))
        if number is not None:
            number += 1
    return ''.join(lines), origins


def select_active_cstring_helpers(stdout, source_name, source_records, included_headers=None):
    """Keep authoritative macro-expanded bodies with exact /E source origins."""
    code, origins = preprocessed_origins(stdout.decode('utf-8', errors='replace'))
    records = select_cstring_helpers(code, 'preprocessed:' + source_name)
    status = cstring_helper_contract(records)
    def path_key(path):
        return str(path).replace('\\', '/').casefold()
    headers = {path_key(path): digest for path, digest in (included_headers or {}).items()}
    matches = []
    for row in records:
        if row['kind'] != 'definition':
            continue
        mapped = origins[row['first_line'] - 1:row['last_line']]
        candidates = []
        for raw in source_records:
            if raw['kind'] != 'definition' or raw['symbol'] != row['symbol'] or raw['owner'] != row['owner']:
                continue
            key = path_key(raw['header'])
            # All nonblank active method lines must originate within this raw
            # method, in the same actually included hashed SDK header.
            substantive = [(origin, line) for origin, line in zip(mapped, code.splitlines()[row['first_line']-1:row['last_line']]) if line.strip()]
            if (headers.get(key) != raw.get('header_sha256') or key not in headers or not substantive
                or any(origin[0] is None or path_key(origin[0]) != key or origin[1] is None
                       or not raw['first_line'] <= origin[1] <= raw['last_line'] for origin, _ in substantive)):
                continue
            candidates.append(raw)
        body = csize_body_tokens(row)
        matches.append({'symbol': row['symbol'], 'active_sha256': row['sha256_utf8'],
                        'active_body_sha256': sha((body or '').encode()),
                        'source_origins': [{'header': h, 'line': n} for h, n in mapped],
                        'source_matches': [{'sha256': raw['sha256_utf8'],
                                            'raw_body_sha256': sha((csize_body_tokens(raw) or '').encode()),
                                            'exact_body_tokens': body == csize_body_tokens(raw)} for raw in candidates]})
    matched = bool(matches) and all(row['source_matches'] for row in matches)
    return {'complete': status['complete'] and matched and not any('\ufffd' in r['excerpt'] for r in records),
            'scope': 'Authoritative active wide helpers mapped to included SDK source spans; macro expansion need not preserve body tokens',
            'preprocessed_stream_sha256': sha(stdout), 'full_preprocessed_stream_retained': False,
            'active_selection_status': status, 'active_source_origins_verified': matched,
            'body_matches': matches, 'selected_definitions': records}


def capture_csize_contexts(out, compiler, platform, source_records, protect, report, save, helper_records=()):
    results = {}
    helper_results = {}
    for experimental in (False, True):
        name = 'csize-context-' + ('experimental' if experimental else 'native-rust-text')
        source = out / (name + '.cpp')
        source.write_text(CSIZE_CONTEXT_SOURCE)
        source_hash = protect(source)
        command = csize_context_command(compiler, source, platform, experimental)
        row = {'name': name, 'command': command, 'source_sha256': source_hash,
               'scope': 'Preprocess only; full SDK stdout discarded after hash and narrow selection'}
        report['executions'].append(row)
        save()
        try:
            result = subprocess.run(command, cwd=out, env=dict(os.environ, VSLANG='1033'),
                                    capture_output=True, timeout=120)
        except subprocess.TimeoutExpired as error:
            # Never persist the potentially full SDK stdout, including partial output.
            (out / (name + '.stderr')).write_bytes(error.stderr or b'')
            row['status'] = 'timeout'
            row['partial_preprocessed_stream_sha256'] = sha(error.stdout or b'')
            results[name] = {'complete': False, 'unresolved': 'preprocessor timeout'}
            continue
        (out / (name + '.stderr')).write_bytes(result.stderr)
        row.update(returncode=result.returncode, stderr_sha256=protect(out / (name + '.stderr')),
                   raw_preprocessed_stream_sha256=sha(result.stdout))
        if result.returncode:
            results[name] = {'complete': False, 'unresolved': 'preprocessor failed; full SDK stdout discarded'}
            continue
        try:
            selected, diagnostic = select_active_csize(result.stdout, name, source_records)
        except (ValueError, UnicodeError) as error:
            results[name] = {'complete': False, 'unresolved': str(error)}
            continue
        # /showIncludes may be emitted on either stream. Retain only include
        # diagnostics and the final macro marker, never the preprocessed program.
        (out / (name + '.stdout')).write_bytes(diagnostic)
        row['stdout_sha256'] = protect(out / (name + '.stdout'))
        headers = {}
        for header in re.findall(r'Note: including file:\s*([^\r\n]+)',
                                 (result.stdout + result.stderr).decode('utf-8', errors='replace')):
            path = Path(header.strip()).resolve()
            if path.is_file():
                headers[str(path)] = protect(path)
        try:
            helpers = select_active_cstring_helpers(result.stdout, name, helper_records, headers)
            helpers['included_header_sha256'] = headers
            if not headers: helpers['complete'] = False
            helper_private = out / (name + '-cstring-helpers.private.json')
            helper_private.write_text(json.dumps({'redistribution':'Private bounded SDK source only', **helpers},indent=2)+'\n')
            helper_results[name] = {k:v for k,v in helpers.items() if k != 'selected_definitions'}
            helper_results[name]['private_selection_sha256'] = protect(helper_private)
        except (ValueError, UnicodeError) as error:
            helper_results[name] = {'complete':False,'unresolved':str(error)}
        selected['included_header_sha256'] = headers
        if not headers:
            selected['complete'] = False
            selected['unresolved'] = 'Actual include-header provenance missing'
        private = out / (name + '.private.json')
        private.write_text(json.dumps({'redistribution': 'Private bounded excerpts only; do not check in SDK source',
                                       **selected}, indent=2) + '\n')
        selected_hash = protect(private)
        results[name] = {key: value for key, value in selected.items() if key != 'selected_definitions'}
        results[name]['private_selection_sha256'] = selected_hash
        save()
    report['cstring_helper_active_contexts'] = helper_results
    if 'cstring_helper_contract' in report:
        helper_contract = report['cstring_helper_contract']
        helper_contract['raw_source_selection_status'] = dict(helper_contract)
        resolved = len(helper_results) == 2 and all(r['complete'] for r in helper_results.values())
        helper_contract['complete'] = resolved
        helper_contract['active_conditions_resolved'] = resolved
        helper_contract['resolution_basis'] = 'Actual /E native-rust-text and experimental contexts; active method origins map to bounded included SDK source spans, macro-expanded bodies authoritative'
        if resolved:
            helper_contract['source_capture_complete'] = True
            helper_contract['active_conditions_unresolved'] = []
            helper_contract['unresolved'] = []
    report['csize_active_contexts'] = results
    contract = report['csize_source_contract']
    if contract['source_capture_complete'] and len(results) == 2 and all(value['complete'] for value in results.values()):
        contract['source_conditions_requiring_resolution'] = contract['active_conditions_unresolved']
        contract.update(complete=True, active_conditions_resolved=True, active_conditions_unresolved=[], unresolved=[],
                        resolution_basis='Exact default-body tokens in actual native-rust-text and experimental preprocessed subtitles contexts match selected SDK source; final macro marker alone is not the proof')
    else:
        contract['complete'] = False
        contract['unresolved'].append('Actual active preprocessed constructor evidence incomplete')
    save()


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
        csize_excerpts=[]
        preparation_excerpts=[]
        for header in headers:
            if Path(header).name.lower() in SELECTED_HEADERS:
                try:
                    raw=Path(header).read_bytes();text=raw.decode('utf-8-sig',errors='strict')
                    excerpts+=select_definitions(text,header)
                except (ValueError, UnicodeError) as error:
                    report.setdefault('source_extraction_errors', []).append({'header': header, 'error': str(error)})
            if Path(header).name.lower() in CSIZE_HEADERS:
                try:
                    text=Path(header).read_bytes().decode('utf-8-sig',errors='strict')
                    selected=select_csize_definitions(text,header)
                    for row in selected: row['header_sha256']=headers[header]
                    csize_excerpts+=selected
                except (ValueError, UnicodeError) as error:
                    report.setdefault('csize_source_extraction_errors', []).append({'header': header, 'error': str(error)})
            if Path(header).name.lower() in CSTRING_PREPARATION_HEADERS:
                try:
                    text=Path(header).read_bytes().decode('utf-8-sig',errors='strict')
                    selected=select_cstring_preparation(text,header)
                    for row in selected: row['header_sha256']=headers[header]
                    preparation_excerpts+=selected
                except (ValueError, UnicodeError) as error:
                    report.setdefault('cstring_preparation_extraction_errors', []).append({'header': header, 'error': str(error)})
        helper_excerpts=[]
        for header in headers:
            if Path(header).name.lower() in CSTRING_HELPER_HEADERS:
                try:
                    selected=select_cstring_helpers(Path(header).read_bytes().decode('utf-8-sig',errors='strict'),header)
                    for row in selected: row['header_sha256']=headers[header]
                    helper_excerpts+=selected
                except (ValueError, UnicodeError) as error:
                    report.setdefault('cstring_helper_extraction_errors', []).append({'header':header,'error':str(error)})
        report['cstring_helper_contract']=cstring_helper_contract(helper_excerpts)
        if report.get('cstring_helper_extraction_errors'):
            report['cstring_helper_contract']['complete']=False
            report['cstring_helper_contract']['source_capture_complete']=False
            report['cstring_helper_contract']['unresolved'].append('Helper source extraction failed')
        excerpts+=helper_excerpts
        report['cstring_preparation_contract']=cstring_preparation_contract(preparation_excerpts)
        if report.get('cstring_preparation_extraction_errors'):
            report['cstring_preparation_contract']['complete']=False
            report['cstring_preparation_contract']['source_capture_complete']=False
            report['cstring_preparation_contract']['unresolved'].append('Selected CString source extraction failed')
        excerpts+=preparation_excerpts
        report['csize_source_contract']=csize_source_contract(csize_excerpts)
        if report.get('csize_source_extraction_errors'):
            report['csize_source_contract']['complete']=False
            report['csize_source_contract']['source_capture_complete']=False
            report['csize_source_contract']['unresolved'].append('Selected CSize source extraction failed')
        excerpts+=csize_excerpts
        required=('CAtlMap','Lookup','Rehash','PickSize','UpdateRehashThresholds','SetOptimalLoad','Hash','CompareElements','operator[]','RemoveNode','RemoveKey','RemoveAll','s_anPrimes','CPair')
        private=out/'selected-sdk-definitions.private.json';private.write_text(json.dumps({'redistribution':'Private review excerpts only; do not check in SDK source','definitions':excerpts},indent=2)+'\n');report['selected_definitions_sha256']=protect(private)
        report['definition_index']=[{k:v for k,v in r.items() if k!='excerpt'} for r in excerpts]
        report['missing_required_definitions']=[name for name in required if not any(r['symbol']==name and r['kind']=='definition' for r in excerpts)]
        capture_csize_contexts(out,compiler,args.platform,csize_excerpts,protect,report,save,helper_excerpts)
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
