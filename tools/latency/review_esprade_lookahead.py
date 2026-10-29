"""Recheck the ESP Ra.De. original 1 -> 2 -> 1 look-ahead experiment.

Requires the retained private captures; never launches a game or changes data.
Response indices are independently inspected raw GPU images, not next-Present
assumptions. The identical-binary 2 -> 1 reversal is the causal control.
"""
import argparse
from functools import lru_cache
import json
from pathlib import Path
from statistics import mean
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from esprade_core_latency import analyze
from futari_pixel_response import locate
from latency_probe import read_trace
from project import digest, write_json_new
from vulkan_pixel_probe import read_capture, rgb

BASE = ROOT / '.local/input-latency-v080-r1'
CASES = (
    ('v081-esprade-core-latency-r1', 301, 1, 76, (88, 102, 116, 130)),
    ('v081-esprade-ahead2-r1', 201, 2, 6, (12, 26, 40, 54)),
    ('v081-esprade-ahead1-control-r1', 201, 1, 6, (14, 28, 42, 57)),
)


def review_case(label, first, ahead, reference, responses,
                template_rect=(228,56,24,22), search_y=(45,90)):
    import numpy as np
    prefix = BASE / label
    trace_path = Path(str(prefix) + '-trace.csv')
    trace = read_trace(trace_path.read_text())
    core = analyze(trace, first, first + 3)
    capture = read_capture(Path(str(prefix) + '-pixels'), trace)
    launch_path = Path(str(prefix) + '-launch.json')
    launch = json.loads(launch_path.read_text())
    if launch['pid'] != trace['pid']:
        raise ValueError('Launch/trace process differs')
    executable = Path(launch['command'][0])
    fixture = executable.parents[2]
    stage_path = fixture / 'probe-stage.json'
    config_path = fixture / 'user-data/config.ini'
    for path, expected in ((executable, launch['executable_sha256']),
                           (stage_path, launch['stage_sha256']),
                           (config_path, launch['config_sha256'])):
        if digest(path) != expected:
            raise ValueError('Private fixture identity changed: ' + str(path))
    stage = json.loads(stage_path.read_text())
    for binary in stage['binaries']:
        if digest(fixture / binary['path']) != binary['sha256']:
            raise ValueError('Staged measurement binary changed')
    by = {r['seq']: r for r in trace['records']}
    hz = trace['frequency']
    x,y,width,height=template_rect
    template = np.asarray(rgb(capture, reference))[y:y+height,x:x+width]

    @lru_cache(None)
    def track(index):
        image = np.asarray(rgb(capture, index))
        return max((dict(locate(image, template, y, 180, 340), y=y)
                    for y in range(*search_y)), key=lambda r: r['score'])

    samples = []
    for sample, response in zip(core['samples'], responses, strict=True):
        if sample['look_ahead_frames'] != ahead:
            raise ValueError('Original run-ahead setting differs')
        frame = capture['frames'][response]
        if frame['result'] or not frame['present_begin']:
            raise ValueError('Response image was not successfully presented')
        pad = by[sample['points']['pad_return']]
        begin = by[sample['points']['frame_begin']]
        if by[frame['prepare']]['qpc'] <= begin['qpc']:
            raise ValueError('Response image predates the delivering callback')
        neighborhood = []
        # Include every prepared image from the first input-bearing callback
        # through two images beyond the inspected first response.
        for candidate in capture['frames']:
            qpc = by[candidate['prepare']]['qpc']
            if begin['qpc'] <= qpc <= by[capture['frames'][response + 2]['prepare']]['qpc']:
                neighborhood.append(dict(index=candidate['index'], **track(candidate['index'])))
        before = [dict(index=i, **track(i)) for i in range(response - 3, response)]
        direction = 1 if sample['source_buttons'] == 8 else -1
        origin = before[-1]['x']
        positions = before + neighborhood
        if min(p['score'] for p in positions) < .58:
            raise ValueError('Ambiguous character feature')
        if max(p['x'] for p in before) - min(p['x'] for p in before) > 1:
            raise ValueError('Character was not stationary before the response')
        moved = [p for p in neighborhood if direction * (p['x'] - origin) >= 4]
        if not moved or moved[0]['index'] != response:
            raise ValueError('Inspected response is not the first observed displacement')
        if direction * (track(response + 1)['x'] - track(response)['x']) < 2:
            raise ValueError('First response lacks continuing displacement')
        subsequent = [r for r in trace['records'] if r['kind'] == 10 and r['a'] == 100
                      and begin['qpc'] < r['qpc'] < by[frame['prepare']]['qpc']]
        if len(subsequent) != (1 if ahead == 1 else 0):
            raise ValueError('Original callback count does not support the causal claim')
        samples.append(dict(command=sample['command'], look_ahead=ahead,
            pad_to_delivery_ms=sample['pad_to_delivery_ms'],
            callback_wall_ms=sample['callback_wall_ms'],
            pad_to_response_present_ms=(by[frame['present_begin']]['qpc'] - pad['qpc']) * 1000 / hz,
            response_image=response, response_prepare=frame['prepare'],
            response_present=frame['present_begin'], additional_core_callbacks=len(subsequent),
            feature_positions=positions))

    # Prediction clocks must be rolled back: the next callback starts from the
    # previous NORMAL step's end, rather than the last speculative step's end.
    groups = {}
    clocks = {}
    for row in trace['records']:
        if row['kind'] == 10 and row['a'] == 112:
            groups.setdefault(row['c'], []).append(row)
        elif row['kind'] == 10 and row['a'] in (114, 115):
            clocks[row['c'], row['a']] = tuple(row[k] for k in ('d', 'e', 'f'))
    normal = [v for _, v in sorted(groups.items()) if len(v) == 1 + ahead]
    restored = distinct = 0
    for previous, following in zip(normal, normal[1:]):
        expected = clocks[previous[0]['seq'], 115]
        prediction = clocks[previous[-1]['seq'], 115]
        actual = clocks[following[0]['seq'], 114]
        restored += actual == expected
        distinct += actual == expected and expected != prediction
    paths = [trace_path, launch_path, executable, stage_path, config_path, Path(str(prefix) + '-core.json'),
             Path(str(prefix) + '-pixels.csv'), Path(str(prefix) + '-pixels.bin')]
    return dict(label=label, launch=launch, capture=core['capture'], pixels=capture['footer'],
        samples=samples, read_to_present_mean_ms=mean(s['pad_to_response_present_ms'] for s in samples),
        pad_to_delivery_mean_ms=mean(s['pad_to_delivery_ms'] for s in samples),
        callback_wall_mean_ms=mean(s['callback_wall_ms'] for s in samples),
        clock_restore=dict(adjacent_pairs=len(normal)-1, matching_normal_end=restored,
                           matching_normal_not_prediction=distinct),
        sources=[dict(path=str(p), sha256=digest(p)) for p in paths])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    cases = [review_case(*case) for case in CASES]
    for key in ('executable_sha256', 'config_sha256', 'stage_sha256'):
        if cases[1]['launch'][key] != cases[2]['launch'][key]:
            raise ValueError('Reversal control changed ' + key)
    for case, value in zip(cases[1:], ('2', '1'), strict=True):
        if case['launch']['environment']['EVAC_ESPRADE_TEST_AHEAD'] != value:
            raise ValueError('Reversal control setting differs')
    audit = BASE / 'esprade-core-audit-r2/report.json'
    audited = json.loads(audit.read_text())
    for identity in audited['sources'] + audited['profile_sources']:
        if digest(Path(identity['path'])) != identity['sha256']:
            raise ValueError('Audited original program/profile changed')
    report = dict(game='esprade', cases=cases,
        reversal_saving_ms=cases[2]['read_to_present_mean_ms'] - cases[1]['read_to_present_mean_ms'],
        original_selector=audited['look_ahead'],
        sources=[dict(path=str(p), sha256=digest(p)) for p in (audit, Path(__file__),
                     ROOT/'scripts/esprade_core_latency.py')],
        limitations=['Four inspected ARCADE/Yusuke movements per case, not all modes or physical buttons',
                     'Read-to-first-response Present excludes input sampling phase and presentation completion',
                     'Clock restoration only checks observed scheduler fields, not complete replay/audio equivalence',
                     'Diagnostic observer costs remain; no production runtime or frozen package change'])
    write_json_new(args.output, report)
    print(json.dumps({k: v for k, v in report.items() if k != 'cases'}, indent=2))
    for case in cases:
        print(case['label'], case['read_to_present_mean_ms'], case['clock_restore'])


if __name__ == '__main__':
    main()
