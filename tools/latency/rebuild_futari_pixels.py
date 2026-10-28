"""Compile one private presenter object and relink rexruntime without original writes."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from project import digest, write_json_new
from toolchain import tool_path
from rebuild_ps4_probe import split_windows, normalized


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--destination', type=Path, required=True)
    args = parser.parse_args()
    fixture, destination = args.fixture.resolve(), args.destination.resolve()
    private = ROOT / '.local/input-latency-v080-r1'
    if not all(p.is_relative_to(private) for p in (fixture, destination)):
        raise ValueError('Private paths required')
    receipt = json.loads((fixture / 'preparation.json').read_text(encoding='utf8'))
    for key, expected in (('source', 'original_sha256'), ('copy', 'copy_sha256')):
        if digest(Path(receipt[key])) != receipt[expected]: raise ValueError('Source identity changed')
    if digest(ROOT / 'tools/latency/d3d12_pixel_probe.h') != receipt['pixel_header_sha256']:
        raise ValueError('Pixel header changed after preparation')
    build = ROOT / 'out/sdk-build'
    ninja = str(tool_path('ninja'))
    entries = json.loads(subprocess.check_output([ninja, '-C', str(build), '-t', 'compdb',
        'CXX_COMPILER__rexui_unscanned_Release'], encoding='utf8'))
    entry, = [e for e in entries if e['file'].endswith('/d3d12_presenter.cpp')]
    destination.mkdir(exist_ok=False)
    obj = destination / 'd3d12_presenter.cpp.obj'
    command, out_count, source_count = [], 0, 0
    original = iter(split_windows(entry['command']))
    for arg in original:
        if arg in ('-o', '-MT', '-MF'):
            next(original)
            value = str(obj) + ('.d' if arg == '-MF' else '')
            command.extend([arg, value]); out_count += arg == '-o'
        elif normalized(arg) == normalized(entry['file']):
            command.append(receipt['copy']); source_count += 1
        else: command.append(arg)
    if (out_count, source_count) != (1, 1): raise ValueError('Compile redirection incomplete')
    command.insert(1, '-I' + str(ROOT / 'tools/latency'))
    entries = json.loads(subprocess.check_output([ninja, '-C', str(build), '-t', 'compdb', '-x',
        'CXX_SHARED_LIBRARY_LINKER__rexruntime_Release'], encoding='utf8'))
    entry_link, = entries
    pieces = entry_link['command'].split(' && ')
    if len(pieces) != 3 or '__create_def' not in pieces[0] or pieces[2] != 'cd ."':
        raise ValueError('Unexpected SDK linker wrapper')
    # Existing exports.def is READ, never regenerated in the production tree.
    link, changed, output_kinds, inputs = [], 0, set(), {}
    iterator = iter(split_windows(pieces[1]))
    for arg in iterator:
        lower = arg.lower()
        if arg == '-o':
            next(iterator); link.extend(['-o', str(destination / 'rexruntime.dll')]); output_kinds.add('dll')
        elif lower.startswith('/implib:'):
            link.append('/implib:' + str(destination / 'rexruntime.lib')); output_kinds.add('lib')
        elif lower.startswith('/pdb:'):
            link.append('/pdb:' + str(destination / 'rexruntime.pdb')); output_kinds.add('pdb')
        elif normalized(arg) == normalized(entry['output']):
            link.append(str(obj)); changed += 1
        else:
            link.append(arg)
        candidate = Path(arg) if Path(arg).is_absolute() else build / arg
        if lower.endswith(('.obj', '.lib', '.def')) and candidate.is_file():
            inputs[str(candidate.resolve())] = digest(candidate)
    if changed != 1 or output_kinds != {'dll', 'lib', 'pdb'}:
        raise ValueError('Linker output/object redirection incomplete: ' + repr(output_kinds))
    write_json_new(destination / 'build-plan.json', dict(compile=command, link=link,
        cwd=str(build), input_sha256=inputs, preparation=receipt))
    print('Compile private presenter', flush=True)
    subprocess.run(command, cwd=build, check=True)
    print('Relink private runtime', flush=True)
    subprocess.run(link, cwd=build, check=True)
    for name, expected in inputs.items():
        if digest(Path(name)) != expected: raise ValueError('Original input changed: ' + name)
    write_json_new(destination / 'build-result.json', dict(files={
        p.name: digest(p) for p in destination.iterdir() if p.suffix in ('.dll', '.lib', '.pdb', '.obj')},
        production_inputs_unchanged=True, release_certified=False))
    print('Private pixel runtime complete', flush=True)


if __name__ == '__main__': main()
