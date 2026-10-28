"""Revalidate the G2 Xenia controls and join the native comparison receipt."""
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from g2_latency import analyze
from latency_probe import read_trace
from pixel_probe import read_capture
from project import digest, write_json_new


def checked_report(path):
    report = json.loads(path.read_text(encoding='utf-8'))
    for source in report.get('sources', []):
        if digest(Path(source['path'])) != source['sha256']:
            raise ValueError(f'Capture identity changed: {source["path"]}')
    return report


def main():
    base = ROOT / '.local/input-latency-v080-r1'
    stage_path = base / 'xenia-g2-r1/probe-stage.json'
    stage = json.loads(stage_path.read_text())
    if digest(Path(stage['executable'])) != stage['exe_sha256']:
        raise ValueError('Pinned Xenia measurement executable changed')
    for module, expected in stage['game_programs'].items():
        if digest(ROOT / 'game-data/espgaluda2' / (module + '.xex')) != expected:
            raise ValueError('Original G2 input changed')
    runs = {}
    for run, name, label in ((3, 'xenia-g2-cycle-r2.json', 'pixels-on'),
                              (4, 'xenia-g2-pixel-off-r1.json', 'pixels-off')):
        report_path = base / name
        prior = checked_report(report_path)
        trace = read_trace((base / f'xenia-g2-probe-run-r{run}.csv').read_text())
        current = analyze(trace)
        if json.loads(json.dumps(current['samples'])) != prior['samples']:
            raise ValueError('Earliest-core revalidation changes selected samples')
        if current['paired_sources'] != 60 or current['modes'] != {2: 60}:
            raise ValueError('Expected sixty original 2V internal chains')
        runs[label] = dict(pid=trace['pid'], records=trace['committed'],
            complete=trace['complete'], overflow=trace['overflow'],
            incomplete=trace['incomplete'], paired=current['paired_sources'],
            statistics=current['statistics'],
            report=dict(path=str(report_path), sha256=digest(report_path)))
    capture = read_capture(base / 'xenia-g2-pixels-r3')
    if capture['footer']['overflow']:
        raise ValueError('Pixel capture overflow')
    pixels = []
    for name in ('xenia-g2-first-response-r1.json', 'xenia-g2-first-response-r2.json'):
        path = base / name
        report = checked_report(path)
        offsets = sorted({item['frame_offset'] for item in report['accepted']})
        if not report['accepted'] or offsets != [0]:
            raise ValueError('First response does not match owned first output')
        pixels.append(dict(path=str(path), sha256=digest(path),
            accepted=len(report['accepted']), excluded=len(report['excluded']),
            offsets=offsets, statistics=report['statistics']))
    native_path = base / 'g2-native-comparison-r1.json'
    native = json.loads(native_path.read_text())
    for case in native['native'].values():
        checked_report(Path(case['report']['path']))
        if digest(Path(case['report']['path'])) != case['report']['sha256']:
            raise ValueError('Native comparison report changed')
    result = dict(scene=native['scene'], input=native['input'],
        native_receipt=dict(path=str(native_path), sha256=digest(native_path)),
        xenia=dict(source_commit='291f0c9be398a3e0a0e29f378cbd564fa28c55a9',
            stage=dict(path=str(stage_path), sha256=digest(stage_path)),
            executable_sha256=stage['exe_sha256'], native_frame_delay_patch_applied=False,
            runs=runs, pixel=dict(header=capture['header'], footer=capture['footer'],
                                  checks=pixels)),
        analyzer_sha256=digest(ROOT / 'scripts/g2_latency.py'),
        rejected=dict(run_r1='Hidden startup: no usable window or flushed capture',
            run_r2='Pixel inspection shows the character had died before sampling'),
        conclusion='Native default and Xenia both retain the original 2V internal policy; '
                   'native experimental 1V removes approximately one game cycle.',
        controls='Same Xenia binary for pixel ON/OFF. Different early-stage offsets; '
                 'supports no extra whole cycle, not zero observer cost. Pixel-verified '
                 'subsets are not sixty individually verified responses.',
        still_required=['physical_or_Windows_frontend', 'current_same_response_display',
                        'panel_processing_and_pixel_response'],
        display_reference=dict(refresh_hz=144, position='screen center', type='theory',
            origin='Image ready and adoptable, not CPU Present entry',
            immediate_mean_ms=1000 / 144 / 2, empty_fifo_mean_ms=1000 / 144,
            extra='Unknown panel processing and pixel response D; extra queues excluded'))
    output = base / 'g2-comparison-r1.json'
    write_json_new(output, result)
    print(json.dumps(dict(output=str(output), sha256=digest(output),
        core_to_present={k:v['statistics']['game_input_to_present_begin_us']
                         for k,v in runs.items()}), indent=2))


if __name__ == '__main__':
    main()
