# Adapted from ESPGaluda2-PC ba85ac0, scripts/analyze_modules.py.
"""Run each native static census separately, without guest execution or a window."""
import argparse
import datetime as dt
import json
from pathlib import Path
import subprocess

from analysis_paths import analysis_child, private_output
from build_receipt import verify_sdk
from project import ROOT, digest, write_json_new
from xex_inventory import inventory


def verify_inputs(game, data_dir):
    if game != 'espgaluda2':
        raise ValueError('Unsupported game adaptation')
    lock = json.loads((ROOT / 'games' / game / 'inputs.json').read_text(encoding='utf-8'))
    for item in lock['xex']:
        path = analysis_child(data_dir, item['path'])
        if not path.is_file() or path.stat().st_size != item['bytes'] or digest(path) != item['sha256']:
            raise ValueError(f'Input does not match the declared version: {item["path"]}')
    return lock


def inspect_report(report, name, expected, target, output):
    if (report.get('module') != name or report.get('toolMode') is not True
            or report.get('guestExecution') is not False or any(report.get(key) is not False
            for key in ('gpuInitialized', 'audioInitialized', 'inputInitialized'))):
        raise ValueError('Static tool unexpectedly enabled interactive subsystems')
    if report.get('mainImageAllocationPreserved') is not True or (name != 'default.xex' and
            (report.get('staticLoadCycles') != 3 or report.get('overlappingLoadRejectedSafely') is not True)):
        raise ValueError('Static image ownership regression failed')
    if (f'0x{report["imageBase"]:08X}' != expected['imageBase']
            or report.get('titleId') != int(expected['titleId'], 16)):
        raise ValueError('Loaded image identity differs from the header lock')
    dumps, seen = [], set()
    for section in report['sections']:
        if not section['inImage']:
            if section['executable'] or 'dump' in section:
                raise ValueError('Invalid excluded section')
            continue
        path = analysis_child(target, section['dump'])
        if path in seen or path.stat().st_size != section['bytes']:
            raise ValueError('Duplicate or incomplete section dump')
        seen.add(path)
        dumps.append(dict(path=path.relative_to(output).as_posix(), sha256=digest(path)))
    return dict(module=name, inputSha256=expected['sha256'], imageBytes=report['imageBytes'],
                importSummary=report['importSummary'], dumps=dumps)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', choices=('espgaluda2',), default='espgaluda2')
    parser.add_argument('--data-dir', type=Path, required=True)
    parser.add_argument('--module', action='append', help='Declared XEX name; default: all eight')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    data_dir = args.data_dir.resolve()
    declared = {item['path']: item for item in verify_inputs(args.game, data_dir)['xex']}
    modules = args.module or list(declared)
    if len(modules) != len(set(modules)) or any(name not in declared for name in modules):
        raise ValueError('Expected unique declared XEX filenames')
    stamp = dt.datetime.now(dt.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    output = private_output(args.output or ROOT / '.local/analysis' / stamp, data_dir)
    verify_sdk()
    cache = ROOT / 'out/sdk-build/CMakeCache.txt'
    if 'REXGLUE_ENABLE_TRACY:BOOL=OFF' not in cache.read_text(encoding='utf-8'):
        raise ValueError('Use the no-Tracy SDK build before running analysis')
    builds = (ROOT / 'out' / args.game, ROOT / 'out' / (args.game + '-native'))
    executable = next((path / 'bin' / (args.game + '_inspect.exe') for path in builds
                       if (path / 'bin' / (args.game + '_inspect.exe')).is_file()), None)
    if executable is None:
        raise ValueError('Build the game native analysis tools first (-NativeOnly)')
    runtime = executable.parent / 'rexruntime.dll'
    if digest(runtime) != digest(ROOT / '.deps/sdk-install/bin/rexruntime.dll'):
        raise ValueError('Analysis tool uses a different SDK runtime; rebuild it')
    before = inventory(data_dir)
    output.mkdir(parents=True, exist_ok=False)
    summaries = []
    for name in modules:
        target = output / Path(name).stem
        print(f'Inspecting {name}; no guest execution', flush=True)
        with (output / (Path(name).stem + '-stdout.log')).open('xb') as stdout, \
                (output / (Path(name).stem + '-stderr.log')).open('xb') as stderr:
            result = subprocess.run([str(executable), str(data_dir), name, str(target),
                                     '--dump-sections'], stdout=stdout, stderr=stderr, timeout=90)
        if result.returncode:
            raise RuntimeError(f'Static census failed for {name}: {result.returncode}; preserve {output}')
        report = json.loads(analysis_child(target, 'census.json').read_text(encoding='utf-8'))
        summaries.append(inspect_report(report, name, declared[name], target, output))
    if before != inventory(data_dir):
        raise ValueError('Game inputs changed during static analysis')
    write_json_new(output / 'analysis.json', dict(format=1, game=args.game, toolSha256=digest(executable),
        runtimeSha256=digest(runtime), guestExecution=False, gameDataUnchanged=True, modules=summaries))
    print(json.dumps(dict(output=str(output), modules=len(summaries), gameDataUnchanged=True), indent=2), flush=True)


if __name__ == '__main__':
    main()
