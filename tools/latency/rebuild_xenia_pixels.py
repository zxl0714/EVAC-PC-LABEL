"""Reuse pinned Xenia objects; privately compile one presenter and relink."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from project import digest, write_json_new
from toolchain import tool_path
from rebuild_ps4_probe import split_windows, normalized


def unwrap(command):
    pieces = command.split(' && ')
    if len(pieces) != 3 or not pieces[0].endswith('"cd .') or pieces[2] != 'cd ."':
        raise ValueError('Unexpected Xenia command wrapper')
    return split_windows(pieces[1])


def keyboard_passthrough(entries, destination, remember):
    """Navigation commands <900 use the test pad; >=900 uses original drivers."""
    source, = [e for e in entries if e['file'].endswith('/hid/input_system.cc')]
    archive, = [e for e in entries if e['output'].endswith('xenia-hid.lib')]
    text = Path(source['file']).read_text(encoding='utf8')
    anchor = 'if (evac_latency::test_pad && user_index == 0) {'
    if text.count(anchor) != 1:
        raise ValueError('Synthetic input navigation guard changed')
    copy = destination/'input_system.cc'
    copy.write_text(text.replace(anchor,
        'if (evac_latency::test_pad && user_index == 0 &&\n'
        '      evac_latency::CurrentTestCommand() < 900) {'), encoding='utf8')
    obj, lib = destination/'input_system.cc.obj', destination/'xenia-hid.lib'
    compile_args, changed = [], set()
    for arg in split_windows(source['command']):
        if normalized(arg) == normalized(source['file']):
            compile_args.append(str(copy)); changed.add('source')
        elif arg.startswith('/Fo'):
            compile_args.append('/Fo'+str(obj)); changed.add('obj')
        elif arg.startswith('/Fd'):
            compile_args.append('/Fd'+str(destination/'input.pdb')); changed.add('pdb')
        else: compile_args.append(arg)
    if changed != {'source','obj','pdb'}:
        raise ValueError('Input compile redirection incomplete')
    archive_args, changed = [], set()
    for arg in unwrap(archive['command']):
        remember(arg)
        if normalized(arg) == normalized(source['output']):
            archive_args.append(str(obj)); changed.add('obj')
        elif arg.lower().startswith('/out:'):
            archive_args.append('/out:'+str(lib)); changed.add('lib')
        else: archive_args.append(arg)
    if changed != {'obj','lib'}:
        raise ValueError('Input archive redirection incomplete')
    return dict(compile=compile_args, archive=archive_args,
        original_lib=archive['output'], replacement_lib=str(lib),
        source=source['file'], source_sha256=digest(Path(source['file'])),
        copy=str(copy), copy_sha256=digest(copy))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--destination', type=Path, required=True)
    parser.add_argument('--keyboard-passthrough', action='store_true')
    args = parser.parse_args()
    fixture, destination = args.fixture.resolve(), args.destination.resolve()
    private = ROOT / '.local/input-latency-v080-r1'
    if not all(p.is_relative_to(private) for p in (fixture, destination)):
        raise ValueError('Private paths required')
    receipt = json.loads((fixture/'preparation.json').read_text())
    for key, expected in (('source','original_sha256'),('copy','copy_sha256')):
        if digest(Path(receipt[key])) != receipt[expected]: raise ValueError('Presenter changed')
    build = private/'xenia-build'
    entries = json.loads(subprocess.check_output([str(tool_path('ninja')), '-C', str(build),
        '-t','compdb','-x','CXX_COMPILER__xenia-ui-d3d12_unscanned_Release',
        'CXX_STATIC_LIBRARY_LINKER__xenia-ui-d3d12_Release',
        'CXX_COMPILER__xenia-hid_unscanned_Release',
        'CXX_STATIC_LIBRARY_LINKER__xenia-hid_Release',
        'CXX_EXECUTABLE_LINKER__xenia-app_Release'], encoding='utf8'))
    presenter, = [r for r in entries if r['file'].endswith('/d3d12_presenter.cc')]
    archive, = [r for r in entries if r['output'].endswith('xenia-ui-d3d12.lib')]
    executable, = [r for r in entries if r['output'].endswith('xenia_canary.exe')]
    destination.mkdir(exist_ok=False)
    obj, lib = destination/'d3d12_presenter.cc.obj', destination/'xenia-ui-d3d12.lib'
    inputs = {}
    def remember(arg):
        candidate = Path(arg) if Path(arg).is_absolute() else build/arg
        if candidate.suffix.lower() in ('.obj','.lib','.res','.manifest') and candidate.is_file():
            inputs[str(candidate.resolve())] = digest(candidate)
    compile_args, changed = [], set()
    for arg in split_windows(presenter['command']):
        if normalized(arg) == normalized(presenter['file']):
            compile_args.append(receipt['copy']); changed.add('source')
        elif arg.startswith('/Fo'):
            compile_args.append('/Fo'+str(obj)); changed.add('obj')
        elif arg.startswith('/Fd'):
            compile_args.append('/Fd'+str(destination/'presenter.pdb')); changed.add('pdb')
        else: compile_args.append(arg)
    if changed != {'source','obj','pdb'}: raise ValueError('Compile redirection incomplete')
    archive_args, changed = [], set()
    for arg in unwrap(archive['command']):
        remember(arg)
        if normalized(arg) == normalized(presenter['output']):
            archive_args.append(str(obj)); changed.add('obj')
        elif arg.lower().startswith('/out:'):
            archive_args.append('/out:'+str(lib)); changed.add('lib')
        else: archive_args.append(arg)
    if changed != {'obj','lib'}: raise ValueError('Archive redirection incomplete')
    keyboard = keyboard_passthrough(entries, destination, remember) if args.keyboard_passthrough else None
    link_args, changed = [], set()
    for arg in unwrap(executable['command']):
        remember(arg)
        lower = arg.lower()
        if normalized(arg) == normalized(archive['output']):
            link_args.append(str(lib)); changed.add('lib')
        elif keyboard and normalized(arg) == normalized(keyboard['original_lib']):
            link_args.append(keyboard['replacement_lib']); changed.add('input_lib')
        elif lower.startswith('--intdir='):
            link_args.append('--intdir='+str(destination/'link')); changed.add('intdir')
        elif lower.startswith('/out:'):
            link_args.append('/out:'+str(destination/'xenia_canary.exe')); changed.add('exe')
        elif lower.startswith('/implib:'):
            link_args.append('/implib:'+str(destination/'xenia_canary.lib')); changed.add('implib')
        elif lower.startswith('/pdb:'):
            link_args.append('/pdb:'+str(destination/'xenia_canary.pdb')); changed.add('pdb')
        else: link_args.append(arg)
    if changed != ({'lib','intdir','exe','implib','pdb'} | ({'input_lib'} if keyboard else set())):
        raise ValueError('Link redirection incomplete')
    (destination/'link').mkdir()
    plan = dict(compile=compile_args, archive=archive_args, link=link_args,
                preparation=receipt, keyboard_passthrough=keyboard, cwd=str(build), input_sha256=inputs)
    write_json_new(destination/'build-plan.json', plan)
    commands = [('Compile',compile_args),('Archive',archive_args)]
    if keyboard: commands += [('Compile input',keyboard['compile']),('Archive input',keyboard['archive'])]
    commands += [('Link',link_args)]
    for label, command in commands:
        print(label, flush=True)
        subprocess.run(command, cwd=build, check=True)
    if any(digest(Path(name)) != expected for name,expected in inputs.items()):
        raise ValueError('A reused original input changed')
    for source in (build/'bin/Windows').glob('*.dll'):
        shutil.copy2(source, destination/source.name)
    write_json_new(destination/'build-result.json', dict(preparation=receipt,
        keyboard_passthrough=keyboard, exe_sha256=digest(destination/'xenia_canary.exe'),
        reused_inputs_unchanged=True))


if __name__ == '__main__': main()
