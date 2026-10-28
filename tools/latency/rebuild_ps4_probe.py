"""Relink a private PS4 probe using unchanged existing objects and libraries.

Ninja is used only to READ its compile/link commands. All compilation outputs,
PDBs, manifests and final binaries are redirected into a fresh private folder.
"""
import argparse
import ctypes
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from project import digest, write_json_new
from toolchain import tool_path


def split_windows(command):
    api = ctypes.windll.shell32.CommandLineToArgvW
    api.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
    api.restype = ctypes.POINTER(ctypes.c_wchar_p)
    count = ctypes.c_int()
    pointer = api(command, ctypes.byref(count))
    if not pointer:
        raise ValueError('Cannot parse compiler command')
    try:
        return [pointer[i] for i in range(count.value)]
    finally:
        ctypes.windll.kernel32.LocalFree(ctypes.cast(pointer, ctypes.c_void_p))


def normalized(path):
    return str(path).replace('\\', '/').lower()


def build(fixture, destination, execute):
    private = ROOT / '.local/input-latency-v080-r1'
    if not fixture.is_relative_to(private) or not destination.is_relative_to(private):
        raise ValueError('Private fixture/output directory required')
    preparation = json.loads((fixture / 'preparation.json').read_text(encoding='utf-8'))
    for item in preparation['files']:
        if digest(Path(item['source'])) != item['original_sha256'] or digest(Path(item['copy'])) != item['copy_sha256']:
            raise ValueError('Preparation source identity changed')
    destination.mkdir(exist_ok=False)
    ninja = tool_path('ninja', ROOT)
    plan, input_hashes = [], {}
    for part, target, names in (
        ('ps4-native', 'KetsuiNativeProbe', ['native_probe.cpp', 'native_pad.cpp', 'sdl_gamepad.cpp']),
        ('ps4-gpu', 'KetsuiGpuRuntime', ['presenter.cpp'])):
        original_build = ROOT / 'out' / part
        rule = 'CXX_COMPILER__' + target + '_unscanned_RelWithDebInfo'
        entries = json.loads(subprocess.check_output(
            [str(ninja), '-C', str(original_build), '-t', 'compdb', rule], encoding='utf-8'))
        replacements = {}
        for name in names:
            matches = [e for e in entries if Path(e['file']).name == name]
            if len(matches) != 1:
                raise ValueError('Ambiguous compiler recipe: ' + name)
            entry = matches[0]
            original_source = Path(entry['file'])
            patched = fixture / name
            output = destination / (part + '-' + name + '.obj')
            replacements[normalized(entry['output'])] = str(output)
            arguments = split_windows(entry['command'])
            changed_source = changed_output = False
            rewritten = []
            for arg in arguments:
                if arg.startswith('/Fo'):
                    rewritten.append('/Fo' + str(output)); changed_output = True
                elif arg.startswith('/Fd') or arg.lower() == '/showincludes':
                    continue
                elif arg.lower() in ('-zi', '/zi'):
                    rewritten.append('/Z7')
                elif normalized(arg) == normalized(original_source):
                    rewritten.append(str(patched)); changed_source = True
                else:
                    rewritten.append(arg)
            if not changed_source or not changed_output:
                raise ValueError('Could not redirect all compiler outputs')
            index = rewritten.index('--')
            rewritten[index:index] = ['-I' + str(original_source.parent), '-I' + str(ROOT / 'tools/latency')]
            plan.append(dict(stage='compile', cwd=str(original_build), command=rewritten,
                             output=str(output)))

        extension = '.exe' if part == 'ps4-native' else '.dll'
        link_rule = ('CXX_EXECUTABLE_LINKER__' if extension == '.exe'
                     else 'CXX_SHARED_LIBRARY_LINKER__') + target + '_RelWithDebInfo'
        # Expand Ninja response files from its graph. These transient files are
        # normally removed after a production link; never recreate one there.
        link_entries = json.loads(subprocess.check_output([str(ninja), '-C', str(original_build),
            '-t', 'compdb', '-x', link_rule], encoding='utf-8'))
        link_entries = [e for e in link_entries if e['output'] == target + extension]
        if len(link_entries) != 1:
            raise ValueError('Ambiguous linker recipe: ' + target)
        final = link_entries[0]['command']
        match = re.fullmatch(r'.*? /C "cd \. && (.+) && cd \."', final)
        if not match:
            raise ValueError('Unrecognized CMake linker wrapper')
        arguments = split_windows(match[1])
        rewrite, output_options, replaced_objects = [], set(), set()
        for arg in arguments:
            lower = arg.lower()
            if arg.startswith('@'):
                raise ValueError('Unexpanded linker response file')
            if lower.startswith('--intdir='):
                folder = destination / (part + '-link')
                folder.mkdir()
                rewrite.append('--intdir=' + str(folder))
            elif normalized(arg) in replacements:
                rewrite.append(replacements[normalized(arg)])
                replaced_objects.add(normalized(arg))
            elif any(lower.startswith(prefix) for prefix in ('/out:', '/pdb:', '/implib:')):
                key, value = arg.split(':', 1)
                rewrite.append(key + ':' + str(destination / Path(value).name))
                output_options.add(key.lower())
            elif lower == '/incremental':
                rewrite.append('/INCREMENTAL:NO')
            else:
                rewrite.append(arg)
            candidate = Path(arg) if Path(arg).is_absolute() else original_build / arg
            if lower.endswith(('.obj', '.lib')) and candidate.is_file():
                resolved = candidate.resolve()
                input_hashes[str(resolved)] = digest(resolved)
        if output_options != {'/out', '/pdb', '/implib'}:
            raise ValueError('Incomplete linker output redirection')
        if replaced_objects != replacements.keys():
            raise ValueError('Not every measurement object reached the linker')
        plan.append(dict(stage='link', cwd=str(original_build), command=rewrite,
                         output=str(destination / (target + extension))))

    write_json_new(destination / 'build-plan.json', dict(commands=plan,
        reused_input_sha256=input_hashes, preparation_sha256=digest(fixture / 'preparation.json')))
    if not execute:
        print('Prepared only: ' + str(destination)); return
    for index, step in enumerate(plan, 1):
        print(f"[{index}/{len(plan)}] {step['stage']}: {Path(step['output']).name}", flush=True)
        subprocess.run(step['command'], cwd=step['cwd'], check=True)
    for name, identity in input_hashes.items():
        if digest(Path(name)) != identity:
            raise ValueError('An original build input changed during probe build')
    for item in preparation['files']:
        if digest(Path(item['source'])) != item['original_sha256']:
            raise ValueError('Original source changed during probe build')
    write_json_new(destination / 'build-result.json', dict(
        files=[dict(path=step['output'], sha256=digest(Path(step['output']))) for step in plan],
        original_inputs_unchanged=True, release_certified=False,
        plan_sha256=digest(destination / 'build-plan.json')))
    print('Private measurement build complete: ' + str(destination))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--destination', type=Path, required=True)
    parser.add_argument('--execute', action='store_true')
    args = parser.parse_args()
    build(args.fixture.resolve(strict=True), args.destination.resolve(), args.execute)
