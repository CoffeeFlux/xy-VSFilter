#!/usr/bin/env python3
"""Compile the actual style API helper against pinned/current Windows headers.

No link, native execution, rendering or behavioral comparison. Flags reproduce
VS2022 Release helper compilation captured in run 38640323843, with include paths
from VSFilter.props and helper wrapper. /showIncludes and /Bv add provenance.
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
PIN = '4911cacb102bc0cbd41449793e54473265b04e62'


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def command(root, helper, output, platform, kind, experimental, rust_text=False):
    flags = ['/c', '/nologo', '/Bv', '/showIncludes', '/Zi', '/W3', '/WX-',
             '/diagnostics:column', '/O2', '/Ob2', '/Ot', '/GL', '/GF', '/Gm-',
             '/EHsc', '/MT', '/GS-', '/Gy', '/fp:precise', '/Zc:wchar_t',
             '/Zc:forScope', '/Zc:inline', '/external:W3', '/Gd', '/TP', '/FC',
             '/errorReport:queue', '/Y-', '/w34706']
    defines = ['XY_DOCUMENT_STYLE_TEST_HELPER', '_AFX_NO_MFC_CONTROLS_IN_DIALOGS',
               'NDEBUG', '_CRT_SECURE_NO_WARNINGS', '_SCL_SECURE_NO_WARNINGS',
               'WINVER=0x0601', '_WIN32_WINNT=0x0601', 'PSAPI_VERSION=1',
               '_WINDOWS', 'WINDOWS', '_USE_MATH_DEFINES', '_WINDLL', '_UNICODE', 'UNICODE']
    defines += ['_WIN64', 'WIN64'] if platform == 'x64' else ['WIN32']
    if kind == 'XySubFilter':
        defines.append('XY_SUB_FILTER_DLL')
    if rust_text or experimental:
        defines.append('XY_VSFILTER_RUST_TEXT')
    if experimental:
        defines.append('XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL')
    includes = ['src', 'src/filters/transform/vsfilter', 'src/filters/BaseClasses',
                'src/thirdparty/boost_lib', 'src/thirdparty/log4cplus/include',
                'src/thirdparty/VirtualDub/h', 'include']
    return ['cl', *flags, *('/D' + d for d in defines),
            *('/I' + str(root / p) for p in includes), str(helper),
            '/Fo' + str(output.with_suffix('.obj')), '/Fd' + str(output.with_suffix('.pdb'))]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference-root', type=Path, required=True)
    parser.add_argument('--candidate-root', type=Path, default=ROOT)
    parser.add_argument('--platform', choices=['Win32', 'x64'], required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--generate-only', action='store_true', help='Write commands only; never complete evidence')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    report = {'schema': 'document-style-api-compile-v1', 'complete': False,
              'scope': 'Compile-only actual helper and headers; no link, execution or parity claim',
              'platform': args.platform, 'reference_commit': PIN, 'commands': [],
              'source_hashes': {}, 'included_headers': {}, 'objects': {},
              'environment': {k: os.environ.get(k) for k in
                              ('VCToolsVersion', 'VisualStudioVersion', 'VSCMD_ARG_TGT_ARCH',
                               'WindowsSDKVersion', 'INCLUDE', 'CL', '_CL_', 'PATH')}}
    path = out / 'report.json'

    def save():
        path.write_text(json.dumps(report, indent=2) + '\n')

    save()  # Incomplete before any external call.
    try:
        candidate, reference = args.candidate_root.resolve(), args.reference_root.resolve()
        helper = candidate / 'tools/compat/document_style_api_probe.cpp'
        for root in (reference, candidate):
            for name in ['src/common.props', 'src/filters/transform/vsfilter/VSFilter.props',
                         'src/filters/transform/vsfilter/VSFilter.vcxproj',
                         'src/filters/transform/vsfilter/xy_sub_filter.vcxproj']:
                p = root / name
                report['source_hashes'][str(p)] = digest(p)
        for p in [helper, Path(__file__).resolve()]:
            report['source_hashes'][str(p)] = digest(p)
        for side, root, experimental in [('reference', reference, False),
                                         ('candidate-native', candidate, False),
                                         ('candidate-experimental', candidate, True)]:
            for kind in ['VSFilter', 'XySubFilter']:
                label = side + '-' + kind
                report['commands'].append({'label': label, 'cwd': str(root),
                    'command': command(root, helper, out / label, args.platform, kind, experimental, side != 'reference')})
        save()
        if args.generate_only:
            report['generated'] = True
            save()
            return
        if os.name != 'nt':
            raise RuntimeError('actual Windows MSVC/ATL/MFC required')
        if os.environ.get('CL') or os.environ.get('_CL_'):
            raise RuntimeError('ambient CL/_CL_ overrides are not admitted')
        arch = os.environ.get('VSCMD_ARG_TGT_ARCH', '').lower()
        if arch != ('x64' if args.platform == 'x64' else 'x86'):
            raise RuntimeError('developer command prompt target does not match requested platform')
        compiler = shutil.which('cl')
        if not compiler:
            raise RuntimeError('cl missing')
        report['compiler'] = {'path': compiler, 'sha256': digest(compiler)}
        for side, root in [('reference', reference), ('candidate', candidate)]:
            commit = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], timeout=60).decode().strip()
            report[side + '_commit'] = commit
            dirty = subprocess.check_output(['git', '-C', str(root), 'status', '--porcelain', '--untracked-files=no'], timeout=60)
            if dirty.strip():
                raise RuntimeError(side + ' tracked source is not clean')
            if side == 'reference' and commit != PIN:
                raise RuntimeError('reference checkout is not the pinned commit')
        save()
        # English include diagnostics make every actual header path inspectable.
        env = dict(os.environ, VSLANG='1033')
        for item in report['commands']:
            save()
            try:
                result = subprocess.run(item['command'], cwd=item['cwd'], env=env,
                                        capture_output=True, timeout=180)
            except subprocess.TimeoutExpired as error:
                (out / (item['label'] + '.stdout')).write_bytes(error.stdout or b'')
                (out / (item['label'] + '.stderr')).write_bytes(error.stderr or b'')
                item['timed_out'] = True
                raise
            item['returncode'] = result.returncode
            for suffix, data in [('stdout', result.stdout), ('stderr', result.stderr)]:
                p = out / (item['label'] + '.' + suffix)
                p.write_bytes(data)
                item[suffix + '_sha256'] = digest(p)
            for header in re.findall(r'Note: including file:\s*([^\r\n]+)',
                                     (result.stdout + result.stderr).decode('utf-8', errors='replace')):
                p = Path(header.strip()).resolve()
                value = digest(p)
                if str(p) in report['included_headers'] and report['included_headers'][str(p)] != value:
                    raise RuntimeError('included header changed between compiles: ' + str(p))
                report['included_headers'][str(p)] = value
            if result.returncode:
                raise RuntimeError(item['label'] + ' failed to compile')
            obj = out / (item['label'] + '.obj')
            if not obj.is_file() or not obj.stat().st_size:
                raise RuntimeError('compiler did not produce an object')
            report['objects'][obj.name] = digest(obj)
            save()
        if not report['included_headers']:
            raise RuntimeError('actual header provenance missing')
        for source, old in {**report['source_hashes'], **report['included_headers']}.items():
            if digest(source) != old:
                raise RuntimeError('source/header changed during compile capture: ' + source)
        if digest(compiler) != report['compiler']['sha256']:
            raise RuntimeError('compiler changed during capture')
        for side, root in [('reference', reference), ('candidate', candidate)]:
            commit = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], timeout=60).decode().strip()
            dirty = subprocess.check_output(['git', '-C', str(root), 'status', '--porcelain', '--untracked-files=no'], timeout=60)
            if commit != report[side + '_commit'] or dirty.strip():
                raise RuntimeError(side + ' HEAD or tracked source changed during capture')
        report.update(complete=True, inputs_unchanged=True, tracked_sources_clean=True, heads_unchanged=True)
        save()
        print(json.dumps({'complete': True, 'compiled_objects': len(report['objects']),
                          'scope': report['scope']}))
    except Exception as error:
        report['error'] = str(error)
        save()
        raise


if __name__ == '__main__':
    main()
