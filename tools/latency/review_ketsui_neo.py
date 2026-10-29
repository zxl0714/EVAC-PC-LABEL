"""Revalidate the inspected Ketsui Type-A Stage1 ordinary/NEO response pair.

One same-command pair establishes that both paths respond in the current
update. It is not a distribution of physical button-to-photon latency.
"""
import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from futari_pixel_response import locate
from latency_probe import read_trace
from project import digest, write_json_new
from ps4_latency import summarize
from vulkan_pixel_probe import read_capture, rgb


def review(neo):
    import numpy as np
    base = ROOT / '.local/input-latency-v080-r1'
    label = 'v082-ketsui-neo-pixel-r3' if neo else 'v082-ketsui-base-pixel-r2'
    launch_path, trace_path = (base / (label + suffix) for suffix in ('-launch.json', '-trace.csv'))
    launch = json.loads(launch_path.read_text())
    trace = read_trace(trace_path.read_text())
    capture = read_capture(base / (label + '-pixels'), trace)
    if trace['pid'] != launch['pid'] or trace['overflow']:
        raise ValueError('Invalid capture identity')
    root, = [Path(a.split('=', 1)[1]) for a in launch['command'] if a.startswith('--collection_root=')]
    run, = [p for p in (root / 'logs/ketsui').glob('run-*') if p.name.endswith('-' + str(launch['pid']))]
    config_path = run / 'config.snapshot.ini'
    if digest(config_path) != launch['config_sha256']:
        raise ValueError('Configuration snapshot changed')
    stage_path = root / 'probe-stage.json'
    if digest(stage_path) != launch['stage_sha256']:
        raise ValueError('Frozen binary manifest changed')
    configuration, = [json.loads(l) for l in (run / 'stdout.jsonl').read_text(errors='replace').splitlines()
                      if '"event":"collection_configuration"' in l]
    if configuration['neo'] != neo or configuration['vsync']:
        raise ValueError('Unexpected runtime mode')
    expected = (1920, 1080) if neo else (1280, 720)
    if (capture['header']['width'], capture['header']['height']) != expected:
        raise ValueError('Unexpected guest composition size')
    sample, = [s for s in summarize(trace, 101, 112)['input_samples'] if s['command'] == 103]
    if sample['buttons'] != 0x8080:
        raise ValueError('Not the inspected Left plus Square source')
    first = 49 if neo else 51
    boxes = ((212, 216, 10, 16), (207, 261, 18, 13)) if neo else (
        (187, 140, 8, 12), (184, 158, 12, 10))
    references = (17, 18, 19) if neo else tuple(range(6))
    images = {i: np.asarray(rgb(capture, i)) for i in {*references, *range(first-3, first+2)}}
    templates = [[images[i][y:y+h, x:x+w] for i in references] for x, y, w, h in boxes]
    tracks = []
    for index in range(first-3, first+2):
        features = [max((locate(images[index], template, box[1], 60, 380)
                         for template in group), key=lambda p: p['score'])
                    for box, group in zip(boxes, templates)]
        # The invulnerability overlay changes colour. Retain a strong unique
        # peak and exact coherent positions before/after, then require both
        # actual first-response features to have at least .90 correlation.
        if any(p['score'] < .60 or p['score']-p['distant_score'] < .10 for p in features):
            raise ValueError('Ambiguous inspected ship feature')
        tracks.append(dict(frame=index, features=features))
    expected_positions = ([(212,207)]*3+[(203,198),(197,192)] if neo else
                          [(181,178)]*3+[(177,174),(173,170)])
    if [tuple(p['x'] for p in t['features']) for t in tracks] != expected_positions:
        raise ValueError('Inspected stationary neighborhood/left response changed')
    if any(p['score'] < .90 for p in tracks[3]['features']):
        raise ValueError('Weak first-response evidence')
    records = {r['seq']: r for r in trace['records']}
    source, returned = records[sample['source']], records[sample['api_end']]
    previous, response = capture['frames'][first-1], capture['frames'][first]
    prepared, presented = records[response['prepare']], records[response['present_begin']]
    if not (source['qpc'] <= returned['qpc'] <= prepared['qpc'] <= presented['qpc'] and
            records[previous['present_begin']]['qpc'] < returned['qpc']):
        raise ValueError('Input/stationary-image/response order differs')
    files = [launch_path, trace_path, config_path, stage_path,
             base/(label+'-pixels.csv'), base/(label+'-pixels.bin')]
    return dict(label=label, neo=neo, command=103, accepted_responses=1,
        executable_sha256=launch['executable_sha256'], stage_sha256=launch['stage_sha256'],
        composition_size=expected, source=source['seq'], pad_return=returned['seq'],
        first_response=first, response_present=presented['seq'], tracks=tracks,
        pad_return_to_response_present_ms=(presented['qpc']-returned['qpc'])*1000/trace['frequency'],
        sources=[dict(path=str(p), sha256=digest(p)) for p in files])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to(ROOT / '.local/input-latency-v080-r1') or output.exists():
        raise ValueError('Fresh private report required')
    base, neo = review(False), review(True)
    for key in ('executable_sha256', 'stage_sha256'):
        if base[key] != neo[key]:
            raise ValueError('The pair uses different binaries')
    result = dict(base=base, neo=neo, reviewer_sha256=digest(Path(__file__)),
        scope='One inspected same-command first-response pair. Neither path adds a whole game update; no physical device or panel measurement.')
    write_json_new(output, result)
    print(json.dumps({k: result[k]['pad_return_to_response_present_ms'] for k in ('base','neo')}))


if __name__ == '__main__':
    main()
