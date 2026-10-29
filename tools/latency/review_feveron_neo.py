"""Review inspected Feveron Stage1 NEO/base response captures.

The private fixture must start at the same inspected quickslot and fire before
the direction pulses. Two disjoint ship features establish movement; neither
the next Present nor a scrolling background is accepted as a response.
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


def review(label, neo, first=101, last=112):
    import numpy as np
    base = ROOT / '.local/input-latency-v080-r1'
    paths = {name: base / (label + suffix) for name, suffix in (
        ('launch', '-launch.json'), ('trace', '-trace.csv'),
        ('pixels_csv', '-pixels.csv'), ('pixels_bin', '-pixels.bin'))}
    launch = json.loads(paths['launch'].read_text())
    trace = read_trace(paths['trace'].read_text())
    if trace['pid'] != launch['pid'] or trace['overflow']:
        raise ValueError('Capture identity or completeness differs')
    capture = read_capture(base / (label + '-pixels'), trace)
    header = capture['header']
    expected = (1920, 1080) if neo else (1280, 720)
    if (header['width'], header['height']) != expected:
        raise ValueError('Unexpected virtual display size')
    root, = [Path(arg.split('=', 1)[1]) for arg in launch['command']
             if arg.startswith('--collection_root=')]
    run, = [p for p in (root / 'logs/feveron').glob('run-*')
            if p.name.endswith('-' + str(launch['pid']))]
    paths['config'] = run / 'config.snapshot.ini'
    paths['stdout'] = run / 'stdout.jsonl'
    if digest(paths['config']) != launch['config_sha256']:
        raise ValueError('Launched configuration identity differs')
    events = []
    for line in paths['stdout'].read_text(errors='replace').splitlines():
        if line.startswith('{') and line.endswith('}'):
            try:
                events.append(json.loads(line))
            except json.JSONDecodeError:
                pass
    config, = [e for e in events if e.get('event') == 'collection_configuration']
    if config['neo'] != neo or config['vsync'] or config['fullscreen']:
        raise ValueError('This comparison requires the inspected windowed Immediate setup')
    module, = [e for e in events if e.get('event') == 'native_module_start'
               and e['image_name'].startswith('dfeveron_')]
    summary = summarize(trace, first, last)
    samples = summary['input_samples']
    if [s['command'] for s in samples] != list(range(first, last + 1)):
        raise ValueError('Missing, repeated or unordered input sources')
    by_id = {r['seq']: r for r in trace['records']}
    images = {}

    def image(index):
        if index not in images:
            images[index] = np.asarray(rgb(capture, index))
        return images[index]

    # Cockpit and lower hull, inspected on the same fixed Type-A quickslot.
    # Scale is the game's 1080p/720p output, not a Windows screenshot resize.
    boxes = ((113, 282, 18, 27), (105, 315, 36, 15)) if neo else (
        (128, 188, 12, 18), (122, 210, 24, 10))
    templates = [image(0)[y:y+h, x:x+w] for x, y, w, h in boxes]
    rows = []
    for sample in samples:
        returned = by_id[sample['api_end']]
        begin = next(f['index'] for f in capture['frames'] if f['copy_qpc'] >= returned['qpc'])
        if begin < 3 or begin + 6 >= len(capture['frames']):
            raise ValueError('Response neighborhood is incomplete')
        direction = -1 if sample['command'] % 2 else 1
        tracks = []
        for index in range(begin - 3, begin + 7):
            features = [locate(image(index), template, box[1], 0, 210)
                        for box, template in zip(boxes, templates)]
            tracks.append(dict(frame=index, features=features))
        before = tracks[:3]
        if any(f['score'] < .85 for t in before for f in t['features']):
            raise ValueError('Ambiguous stationary ship before command ' + str(sample['command']))
        origins = [before[-1]['features'][n]['x'] for n in range(2)]
        if any(len({t['features'][n]['x'] for t in before}) != 1 for n in range(2)):
            raise ValueError('Ship was still moving before command ' + str(sample['command']))
        response = next((t for t in tracks[3:] if all(
            f['score'] >= .85 and direction * (f['x'] - origin) >= 3
            for f, origin in zip(t['features'], origins))), None)
        if response is None:
            raise ValueError('No two-feature response to command ' + str(sample['command']))
        frame = capture['frames'][response['frame']]
        present = by_id[frame['present_begin']]
        if present['qpc'] < returned['qpc']:
            raise ValueError('Response predates its input')
        rows.append(dict(command=sample['command'], response_frame=response['frame'],
            pad_return=returned['seq'], present=present['seq'],
            read_to_response_present_ms=(present['qpc']-returned['qpc'])*1000/trace['frequency'],
            source_to_response_present_ms=(present['qpc']-by_id[sample['source']]['qpc'])*1000/trace['frequency'],
            origins=origins, tracks=tracks))
    return dict(label=label, neo=neo, module=module['image_name'],
        executable_sha256=launch['executable_sha256'], display=expected,
        window=[config['window_width'], config['window_height']], samples=rows,
        mean_read_to_response_present_ms=sum(r['read_to_response_present_ms'] for r in rows)/len(rows),
        sources=[dict(path=str(p), sha256=digest(p)) for p in paths.values()])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', required=True)
    parser.add_argument('--neo', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if any(not label.replace('-', '').isalnum() for label in (args.base, args.neo)):
        raise ValueError('Capture labels must be simple names')
    output = args.output.resolve()
    if not output.is_relative_to(ROOT / '.local/input-latency-v080-r1') or output.exists():
        raise ValueError('Fresh private evidence output required')
    base, neo = review(args.base, False), review(args.neo, True)
    for key in ('executable_sha256', 'window', 'module'):
        if base[key] != neo[key]:
            raise ValueError('Comparison identity differs: ' + key)
    report = dict(base=base, neo=neo,
        mean_saving_ms=base['mean_read_to_response_present_ms']-neo['mean_read_to_response_present_ms'],
        scope='Synthetic guest-input API return to inspected response-image Present. No physical device, OS display scanout or panel measurement.',
        reviewer_sha256=digest(Path(__file__)))
    write_json_new(output, report)
    print(json.dumps({name: report[name]['mean_read_to_response_present_ms']
                      for name in ('base', 'neo')} | {'saving_ms': report['mean_saving_ms']}, indent=2))


if __name__ == '__main__':
    main()
