"""Launch an isolated PS2 input/image diagnostic with the existing test-pad API."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from project import digest, reject_link, write_json_new


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--disc', type=Path, required=True)
    parser.add_argument('--prepared', type=Path, required=True)
    parser.add_argument('--profile', type=Path, help='Reuse an existing private v090 profile for cold-save checks')
    parser.add_argument('--label', required=True)
    parser.add_argument('--pixels', help='Native scanout ROI: x,y,width,height,frame_count; F9 arms capture')
    parser.add_argument('--normal', action='store_true', help='Ordinary input; disable synthetic input and image tracing')
    parser.add_argument('--fullscreen',action='store_true',help='Use the same full desktop output for emulator comparisons')
    parser.add_argument('--present-feedback', action='store_true', help='Opt in to driver presentation-timing feedback')
    parser.add_argument('--capture-gs', action='store_true', help='Record a bounded graphics workload for offline replay')
    args = parser.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,80}', args.label):
        parser.error('Use an ASCII diagnostic label')
    if args.normal and (args.pixels or args.present_feedback or args.capture_gs):
        parser.error('Normal input measurements do not capture diagnostic pixels')
    binary, disc, prepared = (p.resolve(strict=True) for p in
                             (args.binary, args.disc, args.prepared))
    for path in (binary, disc, prepared):
        reject_link(path)
    if not binary.is_relative_to(ROOT) or not binary.is_file() or not disc.is_file():
        raise ValueError('A project-local test EXE and original disc are required')
    boot = prepared / 'boot.elf'
    if digest(boot) != 'ff8eaac60476e5bf3789aa5b3b972fa0f50a50b104b7cc8a1e78af042d510055':
        raise ValueError('Prepared ESPGaluda boot image differs')
    output = ROOT / '.local/v090/latency' / args.label
    reject_link(output)
    output.mkdir(parents=True, exist_ok=False)
    if args.profile:
        profile=args.profile.resolve(strict=True)
        reject_link(profile)
        if not profile.is_dir() or not profile.is_relative_to(ROOT/'.local/v090'):
            raise ValueError('Reuse only a private v090 test profile')
    else:
        profile = output / 'profile'
        profile.mkdir()
    environment = dict(EVAC_LATENCY_TRACE=str(output / 'synthetic-input.csv'),
                       EVAC_LATENCY_TEST_PAD='ps2-' + args.label,
                       EVAC_LATENCY_CAPACITY='262144')
    if args.pixels:
        environment.update(EVAC_PS2_PIXEL_TRACE=str(output / 'scanout'),
                           EVAC_PS2_PIXEL_REGION=args.pixels)
    if args.present_feedback:
        environment['EVAC_PS2_PRESENT_FEEDBACK'] = '1'
    command = [str(binary), str(boot), str(disc), str(prepared), str(profile),
               '--window-size', '1280', '720',
               '--input-trace', str(output / 'input.csv'),
               '--frame-times', str(output / 'frame-times.csv')]
    if args.capture_gs:
        command.extend(['--capture-gs',str(output/'graphics.evacgs')])
    if args.fullscreen:command.append('--fullscreen')
    if args.normal:
        environment = {}
        start = command.index('--input-trace')
        del command[start:start+2]
    inherited = {k:v for k,v in os.environ.items()
                 if not k.startswith(('EVAC_LATENCY_', 'EVAC_PS2_PIXEL_', 'EVAC_PS2_PRESENT_FEEDBACK'))}
    with (output / 'stdout.log').open('xb') as stdout, (output / 'stderr.log').open('xb') as stderr:
        process = subprocess.Popen(command, cwd=ROOT, env=dict(inherited, **environment),
                                   stdout=stdout, stderr=stderr)
    record = dict(pid=process.pid, binary=str(binary), binary_sha256=digest(binary),
                  command=command, environment=environment,
                  tool_sha256=digest(Path(__file__)), diagnostic_only=not args.normal,
                  physical_input_measured=False)
    write_json_new(output / 'launch.json', record)
    print(json.dumps(dict(pid=process.pid, output=str(output), pipe=environment.get('EVAC_LATENCY_TEST_PAD'))))


if __name__ == '__main__':
    main()
