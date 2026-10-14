#!/usr/bin/env python3
"""Inventory Rust source unsafe sites; counts are not a soundness proof.

Includes all platform/feature branches and excludes cfg(test) items from the
production count. Masks comments and literals before counting keyword tokens.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def mask_noncode(source):
    chars = list(source)
    def erase(start, end):
        for k in range(start, end):
            if chars[k] != '\n':
                chars[k] = ' '
    i = 0
    while i < len(source):
        start = i
        if source.startswith('//', i):
            i = source.find('\n', i)
            if i < 0:
                i = len(source)
        elif source.startswith('/*', i):
            i += 2
            depth = 1
            while i < len(source) and depth:
                if source.startswith('/*', i):
                    depth += 1
                    i += 2
                elif source.startswith('*/', i):
                    depth -= 1
                    i += 2
                else:
                    i += 1
            assert depth == 0
        else:
            raw = re.match(r'(?:br|r)(#*)"', source[i:])
            if raw:
                end = source.find('"' + raw[1], i + raw.end())
                assert end >= 0
                i = end + 1 + len(raw[1])
            elif source[i] == '"':
                i += 1
                while i < len(source):
                    if source[i] == '\\':
                        i += 2
                    elif source[i] == '"':
                        i += 1
                        break
                    else:
                        i += 1
            elif source[i] == "'":
                char = re.match(r"'(?:[^'\\\n]|\\(?:u\{[0-9a-fA-F_]+\}|x[0-9a-fA-F]{2}|.))'", source[i:])
                if not char:
                    i += 1
                    continue
                i += char.end()
            else:
                i += 1
                continue
        erase(start, i)
    return ''.join(chars)


def test_ranges(code):
    ranges = []
    for match in re.finditer(r'#\s*\[\s*cfg\s*\(\s*test\s*\)\s*\]', code):
        start = code.index('{', match.end())
        depth, end = 1, start + 1
        while depth:
            depth += (code[end] == '{') - (code[end] == '}')
            end += 1
        ranges.append((match.start(), end))
    return ranges


def inventory(directory):
    records = []
    for path in sorted(directory.rglob('*.rs')):
        source = path.read_text()
        code = mask_noncode(source)
        tests = test_ranges(code)
        def is_test(position):
            return any(a <= position < b for a, b in tests)
        sites = []
        for match in re.finditer(r'\bunsafe\b', code):
            following = code[match.end():]
            if re.match(r'\s*\{', following):
                kind = 'block'
            elif re.match(r'\s+(?:extern\s+)?fn\b', following):
                kind = 'function'
            elif re.match(r'\s+impl\b', following):
                kind = 'impl'
            elif re.match(r'\s+trait\b', following):
                kind = 'trait'
            else:
                kind = 'other'
            sites.append({'line': source.count('\n', 0, match.start()) + 1,
                          'kind': kind, 'test': is_test(match.start())})
        production = ''.join(' ' if is_test(i) and c != '\n' else c for i, c in enumerate(code))
        counts = {f'{scope}_{kind}': sum(site['kind'] == kind and site['test'] == (scope == 'test') for site in sites)
                  for scope in ('production', 'test') for kind in ('block', 'function', 'impl', 'trait', 'other')}
        records.append({'path': str(path.relative_to(ROOT)), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                        'physical_lines_including_tests': len(source.splitlines()),
                        'nonblank_code_lines_excluding_tests': sum(bool(line.strip()) for line in production.splitlines()),
                        'counts': counts, 'sites': sites,
                        'production_extern_functions': len(re.findall(r'\bextern\s+fn\b', production)),
                        'operations': {name: len(re.findall(pattern, production)) for name, pattern in {
                            'raw_shared_slice_construction': r'\bfrom_raw_parts\s*\(',
                            'raw_mutable_slice_construction': r'\bfrom_raw_parts_mut\s*\(',
                            'box_from_raw': r'\bBox\s*::\s*from_raw\s*\(',
                            'raw_nonoverlapping_copy': r'\bcopy_nonoverlapping\s*\(',
                            'unchecked_index_calls': r'\bget_unchecked(?:_mut)?\s*\(',
                            'transmute_calls': r'\btransmute(?:_copy)?\s*(?:\(|::)',
                            'inline_assembly': r'\b(?:asm|global_asm)\s*!',
                            'mutable_statics': r'\bstatic\s+mut\b',
                        }.items()}})
    return {'file_count': len(records), 'totals': {key: sum(r['counts'][key] for r in records) for key in records[0]['counts']},
            'physical_lines_including_tests': sum(r['physical_lines_including_tests'] for r in records),
            'nonblank_code_lines_excluding_tests': sum(r['nonblank_code_lines_excluding_tests'] for r in records),
            'production_extern_functions': sum(r['production_extern_functions'] for r in records),
            'operations': {key: sum(r['operations'][key] for r in records) for key in records[0]['operations']},
            'files': records}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    result = {'schema': 1, 'candidate_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
              'method': 'Lexical source inventory with comments/string/character literals masked and cfg(test) item bodies excluded from production. All platform and feature branches included, not one compiled target. Source hashes identify the measured files; the inventory script may be uncommitted. Block counts and unsafe-function declarations overlap and must not be added as independent operations. Operation categories can overlap and omit raw-pointer dereferences; they are examples, not an exhaustive soundness classification.',
              'limitations': 'Not a proof of soundness or a migration completion percentage. Does not measure C/C++/assembly, native libraries, standard-library internals, borrow correctness at native callsites or dynamic path coverage.',
              'crates': {name: inventory(ROOT / 'rust' / name / 'src') for name in ('compat', 'ffi')}}
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    for name, crate in result['crates'].items():
        print(name, {k:v for k,v in crate.items() if k != 'files'})


if __name__ == '__main__':
    main()
