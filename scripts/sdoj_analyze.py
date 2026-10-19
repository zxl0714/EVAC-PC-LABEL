"""Statically load known SDOJ base programs with the collection SDK; never run guest code."""
import argparse
import datetime as dt
import json
from pathlib import Path
import subprocess

from analysis_paths import analysis_child, private_output
from analyze_modules import inspect_report
from build_receipt import verify_sdk
from project import ROOT, digest, write_json_new
from sdoj_inputs import PROGRAMS, identify
from xex_inventory import xex_metadata


def program_inputs(directory):
    directory = Path(directory).resolve(strict=True)
    result = identify(directory)
    if result['variant'] is None:
        raise ValueError('Static census requires one known SDOJ base-program variant')
    checked = {item['path'].casefold(): item for item in result['checkedFiles']}
    modules = []
    for name in ['default.xex', *sorted(PROGRAMS - {'default.xex'})]:
        item = checked[name.casefold()]
        metadata = xex_metadata(analysis_child(directory, item['path']))
        if metadata['titleId'] != '435A07E7':
            raise ValueError('Expected SDOJ execution identity')
        modules.append(dict(item, **metadata, path=name))
    return dict(variant=result['variant'], programs=modules)


def validate_census(report, expected, target, output):
    if report.get('game') != 'sdoj' or report.get('entryPoint') != int(expected['entryPoint'], 16):
        raise ValueError('Static census does not match the selected SDOJ image')
    return inspect_report(report, expected['path'], expected, target, output)


def analyze(directory, output, executable):
    directory = Path(directory).resolve(strict=True)
    output = private_output(output, directory)
    before = program_inputs(directory)
    verify_sdk()
    executable = Path(executable).resolve(strict=True)
    runtime = executable.parent / 'rexruntime.dll'
    if digest(runtime) != digest(ROOT / '.deps/sdk-install/bin/rexruntime.dll'):
        raise ValueError('Rebuild the static inspector against the current SDK')
    tool_hash, runtime_hash = digest(executable), digest(runtime)
    cache = ROOT / 'out/sdk-build/CMakeCache.txt'
    if 'REXGLUE_ENABLE_TRACY:BOOL=OFF' not in cache.read_text(encoding='utf-8'):
        raise ValueError('Use the no-Tracy SDK build for static analysis')
    output.mkdir(parents=True, exist_ok=False)
    summaries = []
    for item in before['programs']:
        name = item['path']
        target = output / Path(name).stem
        print(f'SDOJ static census: {name}; no guest execution', flush=True)
        with (output / (Path(name).stem + '-stdout.log')).open('xb') as stdout, \
                (output / (Path(name).stem + '-stderr.log')).open('xb') as stderr:
            result = subprocess.run([str(executable), str(directory), name, str(target),
                '--dump-sections', '--game=sdoj'], cwd=ROOT, stdin=subprocess.DEVNULL,
                stdout=stdout, stderr=stderr, timeout=90)
        if result.returncode:
            raise RuntimeError(f'Static census failed for {name}: {result.returncode}; preserve {output}')
        report = json.loads(analysis_child(target, 'census.json').read_text(encoding='utf-8'))
        summaries.append(validate_census(report, item, target, output))
    if before != program_inputs(directory):
        raise ValueError('Selected SDOJ programs changed during static analysis')
    if tool_hash != digest(executable) or runtime_hash != digest(runtime):
        raise ValueError('Static analysis binaries changed during the census')
    verify_sdk()
    write_json_new(output / 'analysis.json', dict(format=1, game='sdoj', variant=before['variant'],
        scope='Known base programs only; no applied TU or resource/playability verification',
        toolSha256=tool_hash, runtimeSha256=runtime_hash, guestExecution=False,
        selectedProgramsUnchanged=True, readyToRun=False, programs=before['programs'], modules=summaries))
    return dict(output=str(output), modules=len(summaries), guestExecution=False, readyToRun=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    # Compatibility executable name: implementation is shared by the 360 backend.
    parser.add_argument('--inspector', type=Path)
    args = parser.parse_args()
    candidates = [ROOT / 'out' / build / 'bin/espgaluda2_inspect.exe'
                  for build in ('espgaluda2-native', 'xbox360-native', 'espgaluda2', 'xbox360')]
    executable = args.inspector or next((path for path in candidates if path.is_file()), None)
    if executable is None:
        raise ValueError('Build the native inspector: Build.ps1 -Games espgaluda2 -NativeOnly')
    stamp = dt.datetime.now(dt.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    output = args.output or ROOT / '.local/analysis' / ('sdoj-census-' + stamp)
    print(json.dumps(analyze(args.data_dir, output, executable), indent=2))


if __name__ == '__main__':
    main()
