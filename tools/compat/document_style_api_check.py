#!/usr/bin/env python3
"""Windows paired actual-DLL style API comparison; never modifies project files."""
import argparse
import hashlib
import json
import os
import shutil
from pathlib import Path
import subprocess
import uuid
from xml.sax.saxutils import escape

import com_contract_compare as com
from document_native_lifecycle import find_library, LIBRARIES, PIN, ROOT


def commit_id(root):
    return com.git(root, "rev-parse", "HEAD").decode("ascii").strip()


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def wrapper_xml(project, helper, root, out, intermediate, library_dir):
    """Sibling wrapper preserves original relative items/imports, unique build state."""
    def x(value):
        return escape(str(value), {'"': '&quot;'})
    return f'''<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
<PropertyGroup><OutDir>{x(out)}\\</OutDir><IntDir>{x(intermediate)}\\</IntDir></PropertyGroup>
<Import Project="{x(project.name)}" />
<ItemDefinitionGroup><Link><AdditionalLibraryDirectories>{x(library_dir)};%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories></Link></ItemDefinitionGroup>
<ItemGroup><ClCompile Include="{x(helper)}"><PrecompiledHeader>NotUsing</PrecompiledHeader><AdditionalOptions>/Y- %(AdditionalOptions)</AdditionalOptions><PreprocessorDefinitions>XY_DOCUMENT_STYLE_TEST_HELPER;%(PreprocessorDefinitions)</PreprocessorDefinitions><AdditionalIncludeDirectories>{x(root / 'src')};{x(project.parent)};%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories></ClCompile></ItemGroup>
</Project>'''


def validate_trace(data, stderr, returncode, mode, bits):
    if returncode or stderr:
        raise ValueError('nonzero exit or nonempty stderr: unresolved execution')
    lines = data.decode('utf-8', errors='strict').splitlines()
    if not lines or lines[0] != f'provenance pointer_bits={bits}' or lines[-1] != 'document-style-api-v1 complete':
        raise ValueError('missing provenance/completion')
    if lines.count('document-style-api-v1 complete') != 1:
        raise ValueError('duplicate completion')
    required = ['factory hr=0', 'create hr=0', 'load hr=0', 'count hr=0']
    required += (['baseline hr=0', 'getter.wrong_count hr=80070057', 'setter.wrong_count hr=80070057',
                  'after.wrong_count hr=0', 'getter.alias hr=0', 'getter.null_style hr=0', 'setter.missing_late hr=80004005',
                  'after.missing_late hr=0', 'setter.duplicate hr=0', 'after.duplicate hr=0', 'setter.all_null hr=0', 'after.all_null hr=0']
                 if mode == 'normal' else ['getter.long_name hr=6f'])
    positions = []
    for token in required:
        if lines.count(token) != 1:
            raise ValueError('missing/duplicate trace event: ' + token)
        positions.append(lines.index(token))
    if positions != sorted(positions):
        raise ValueError('trace event order')
    counts = [int(line[7:]) for line in lines if line.startswith('styles=')]
    if len(counts) != 1 or not 3 <= counts[0] <= 64:
        raise ValueError('invalid style count')
    if mode == 'long-name':
        partial = [int(line[13:]) for line in lines if line.startswith('partial.rows=')]
        if len(partial) != 1 or not 0 < partial[0] < counts[0]:
            raise ValueError('missing partial-write evidence')
    rows = [line for line in lines if line.partition(' ')[0].isdigit()]
    expected_blocks = 1 if mode == 'long-name' else 6
    if len(rows) != counts[0] * expected_blocks:
        raise ValueError('incomplete serialized rows')
    for index, line in enumerate(rows):
        if not line.startswith(str(index % counts[0]) + ' 64:'):
            raise ValueError('serialized row order')
    if mode == 'normal' and sum(line.startswith('alias.final ') for line in lines) != 1:
        raise ValueError('missing alias final value')


def fixture(path, mode):
    # Actual helper must observe a nonempty partial-write prefix; fixture
    # construction alone does not establish native map enumeration order.
    names = ['Default'] + [f'Short{i:02}' for i in range(40)]
    if mode == 'long-name':
        names.append('Long' + 'x' * 64)
    text = '[Script Info]\nScriptType: v4.00+\nPlayResX: 640\nPlayResY: 480\n[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n'
    text += ''.join(f'Style: {name},Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1\n' for name in names)
    text += '[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\nDialogue: 0,0:00:00.00,0:00:02.00,Default,,0,0,0,,style test\n'
    path.write_text(text, encoding='utf-8-sig')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference-root', type=Path, required=True)
    parser.add_argument('--candidate-root', type=Path, default=ROOT)
    parser.add_argument('--platform', choices=['Win32', 'x64'], required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--owner-experimental', action='store_true')
    args = parser.parse_args()
    out = args.output.resolve(); out.mkdir(parents=True, exist_ok=False)
    report = {'status': 'incomplete', 'reference_commit': PIN, 'platform': args.platform, 'runs': [], 'commands': [], 'artifacts': {}, 'schema': 'document-style-api-v1', 'document_storage': 'rust-experimental' if args.owner_experimental else 'native', 'builds': [], 'executions': [], 'toolchain_environment': {key: os.environ.get(key) for key in ('VCToolsVersion', 'VisualStudioVersion', 'VSCMD_ARG_TGT_ARCH', 'WindowsSDKVersion', 'VCToolsInstallDir', 'INCLUDE', 'LIB', 'PATH')}}
    report['tools'] = {name: shutil.which(name) for name in ('cl', 'msbuild', 'link')}
    protected = {}
    def protect(path):
        path = Path(path).resolve()
        protected[str(path)] = digest(path)
        return protected[str(path)]
    report_path = out / 'report.json'
    def save():
        report_path.write_text(json.dumps(report, indent=2) + '\n')
    def run(command, label, env=None):
        report['commands'].append([str(v) for v in command]); save()
        try:
            result = subprocess.run(command, capture_output=True, timeout=300 if label.endswith('build') else 30, env=env)
        except subprocess.TimeoutExpired as error:
            (out / (label + '.stdout')).write_bytes(error.stdout or b'')
            (out / (label + '.stderr')).write_bytes(error.stderr or b'')
            report['executions'].append({'label': label, 'command': [str(v) for v in command], 'returncode': None, 'status': 'timeout', 'stdout_sha256': digest(out / (label + '.stdout')), 'stderr_sha256': digest(out / (label + '.stderr'))})
            raise RuntimeError(label + ' timed out: unresolved execution') from error
        (out / (label + '.stdout')).write_bytes(result.stdout)
        (out / (label + '.stderr')).write_bytes(result.stderr)
        report['executions'].append({'label': label, 'command': [str(v) for v in command], 'returncode': result.returncode, 'stdout_sha256': digest(out / (label + '.stdout')), 'stderr_sha256': digest(out / (label + '.stderr'))})
        if result.returncode:
            raise RuntimeError(label + ' failed')
        return result
    save()
    wrappers = []
    try:
        roots = {'reference': args.reference_root.resolve(), 'candidate': args.candidate_root.resolve()}
        initial = {}
        for side, root in roots.items():
            if com.git(root, 'status', '--porcelain', '--untracked-files=no'):
                raise RuntimeError(side + ' tracked sources are not clean')
            commit = commit_id(root)
            if side == 'reference' and commit != PIN:
                raise RuntimeError('reference is not pinned')
            report[side + '_commit'] = commit
            paths = [find_library(root, args.platform, name) for name in LIBRARIES]
            paths += [com.find_dll(root, name, args.platform) for name in ['VSFilter.dll', 'XySubFilter.dll']]
            initial[side] = {str(p): digest(p) for p in paths}
        manifest, sources = com.class_manifest(PIN, roots['candidate'])
        report['interface_sources'] = sources
        report['production_artifacts'] = initial
        helper = ROOT / 'tools/compat/document_style_api_probe.cpp'
        host_source = ROOT / 'tools/compat/document_style_api_host.cpp'
        report['artifacts']['helper'] = protect(helper); report['artifacts']['host_source'] = protect(host_source)
        report['artifacts']['runner'] = protect(Path(__file__))
        host = out / 'host.exe'
        run(['cl', '/nologo', '/std:c++14', '/MT', '/EHsc', str(host_source), '/Fe:' + str(host), '/Fo:' + str(out / 'host.obj'), '/link', 'ole32.lib', 'advapi32.lib'], 'host-build')
        report['artifacts']['host'] = protect(host)
        for mode in ('normal', 'long-name'):
            fixture(out / (mode + '.ass'), mode)
            report['artifacts'][mode + '.ass'] = protect(out / (mode + '.ass'))
        for side, root in roots.items():
            libdir = find_library(root, args.platform, LIBRARIES[0]).parent
            for dll, project_name in [('VSFilter.dll', 'vsfilter.vcxproj'), ('XySubFilter.dll', 'xy_sub_filter.vcxproj')]:
                project = root / 'src/filters/transform/vsfilter' / project_name
                target = out / side / dll[:-4]; target.mkdir(parents=True)
                wrapper = project.with_name('document_style_api_' + uuid.uuid4().hex + '.vcxproj'); wrappers.append(wrapper)
                wrapper.write_text(wrapper_xml(project, helper, root, target, target / 'obj', libdir))
                retained = out / (side + '-' + dll + '-wrapper.vcxproj')
                retained.write_bytes(wrapper.read_bytes())
                props = [project.parent / 'VSFilter.props', root / 'src/configuration.props', root / 'src/common.props']
                if (root / 'src/rust-text.props').is_file():
                    props.append(root / 'src/rust-text.props')
                build = {'side': side, 'dll': dll, 'wrapper': str(retained), 'wrapper_sha256': protect(retained), 'project': str(project), 'project_sha256': protect(project), 'props': {str(path): protect(path) for path in props}, 'use_of_mfc': 'Static (inherited original project)', 'library_directory': str(libdir)}
                report['builds'].append(build)
                command = ['msbuild', str(wrapper), '/m', '/t:Build', '/p:Configuration=Release', '/p:Platform=' + args.platform, '/p:BuildProjectReferences=false', '/p:OutDir=' + str(target) + os.sep, '/p:IntDir=' + str(target / 'obj') + os.sep, '/p:SolutionDir=' + str(root) + os.sep]
                if side == 'candidate':
                    command += ['/p:RustTextDecoder=true']
                    if args.owner_experimental:
                        command += ['/p:RustDocumentOwnerExperimental=true']
                build['command'] = command
                run(command, side + '-' + dll + '-build')
                built = target / dll
                report['artifacts'][str(built)] = protect(built)
                for record in manifest:
                    if record['dll'].lower() != dll.lower() or record['class'] not in ('CDirectVobSubFilter', 'XySubFilter'):
                        continue
                    for mode in ('normal', 'long-name'):
                        ass = out / (mode + '.ass')
                        for repeat in range(2):
                            label = f'{side}-{record["class"]}-{mode}-{repeat}'
                            key = 'Software\\xyVSFilterCompatibility\\' + str(uuid.uuid4())
                            env = dict(os.environ, XY_DOCUMENT_STYLE_REGISTRY_KEY=key)
                            try:
                                result = run([str(host), str(built), record['clsid'], str(ass), mode], label, env)
                                validate_trace(result.stdout, result.stderr, result.returncode, mode, 32 if args.platform == 'Win32' else 64)
                                report['runs'].append({'side': side, 'class': record['class'], 'mode': mode, 'repeat': repeat, 'trace': label + '.stdout', 'stdout_sha256': protect(out / (label + '.stdout')), 'stderr_sha256': protect(out / (label + '.stderr')), 'returncode': result.returncode})
                            finally:
                                import winreg
                                def remove(path):
                                    try:
                                        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, path, 0, winreg.KEY_ALL_ACCESS) as handle:
                                            while True:
                                                try:
                                                    child = winreg.EnumKey(handle, 0)
                                                except OSError as error:
                                                    if error.winerror == 259: break
                                                    raise
                                                remove(path + '\\' + child)
                                        winreg.DeleteKey(winreg.HKEY_CURRENT_USER, path)
                                    except FileNotFoundError: pass
                                remove(key)
        if len(report['runs']) != 16:
            raise RuntimeError('incomplete class/mode matrix')
        for cls in ('CDirectVobSubFilter', 'XySubFilter'):
            for mode in ('normal', 'long-name'):
                traces = [(out / r['trace']).read_bytes() for r in report['runs'] if r['class'] == cls and r['mode'] == mode]
                if len(traces) != 4 or any(t != traces[0] for t in traces):
                    raise RuntimeError('trace mismatch/nondeterminism: ' + cls + '/' + mode)
        for side, root in roots.items():
            if any(digest(path) != value for path, value in initial[side].items()):
                raise RuntimeError('production artifact changed: ' + side)
            if commit_id(root) != report[side + '_commit'] or com.git(root, 'status', '--porcelain', '--untracked-files=no'):
                raise RuntimeError('source changed: ' + side)
        report['production_artifacts'] = initial
        for path, value in protected.items():
            if digest(path) != value:
                raise RuntimeError('evidence artifact changed: ' + path)
        report['verified_artifacts'] = protected
        report['status'] = 'complete'
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        for wrapper in wrappers:
            wrapper.unlink(missing_ok=True)
        save()


if __name__ == '__main__':
    main()
