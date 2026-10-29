"""Revalidate one inspected Yusuke Stage1 ordinary/NEO first-response pair."""
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
    label = 'v082-esprade-neo-pixel-r5' if neo else 'v082-esprade-base-pixel-r4'
    launch_path, trace_path = [base / (label+s) for s in ('-launch.json', '-trace.csv')]
    launch = json.loads(launch_path.read_text())
    trace = read_trace(trace_path.read_text())
    capture = read_capture(base / (label+'-pixels'), trace)
    if trace['pid'] != launch['pid'] or trace['overflow']:
        raise ValueError('Capture identity differs')
    fixture, = [Path(a.split('=', 1)[1]) for a in launch['command']
                if a.startswith('--collection_root=')]
    run, = [p for p in (fixture/'logs/esprade').glob('run-*')
            if p.name.endswith('-'+str(launch['pid']))]
    config, stage = run/'config.snapshot.ini', fixture/'probe-stage.json'
    if digest(config) != launch['config_sha256'] or digest(stage) != launch['stage_sha256']:
        raise ValueError('Frozen binary/configuration identity differs')
    configuration, = [json.loads(l) for l in (run/'stdout.jsonl').read_text().splitlines()
                      if '"event":"collection_configuration"' in l]
    if configuration['neo'] != neo or configuration['vsync']:
        raise ValueError('Unexpected runtime mode')
    size = (1920, 1080) if neo else (1280, 720)
    if (capture['header']['width'], capture['header']['height']) != size:
        raise ValueError('Unexpected guest composition size')
    boxes = ((378,276,12,18), (370,319,20,21)) if neo else (
        (290,181,10,14), (285,213,14,15))
    images = {i: np.asarray(rgb(capture,i)) for i in range(152,156)}
    templates = [[images[i][y:y+h,x:x+w] for i in (152,153,154)]
                 for x,y,w,h in boxes]
    tracks = []
    for index, image in images.items():
        features = [max((locate(image,t,box[1],240,440) for t in group),
                        key=lambda p:p['score']) for box,group in zip(boxes,templates)]
        if any(p['score'] < .60 or p['score']-p['distant_score'] < .20 for p in features):
            raise ValueError('Ambiguous inspected head/leg feature')
        tracks.append(dict(frame=index,features=features))
    expected = [(378,370)]*3+[(369,361)] if neo else [(290,285)]*3+[(284,279)]
    if [tuple(p['x'] for p in t['features']) for t in tracks] != expected:
        raise ValueError('Stationary neighborhood/first left response changed')
    if tracks[-1]['features'][0]['score'] < .90:
        raise ValueError('Weak first-response head evidence')
    sample, = [s for s in summarize(trace,101,112)['input_samples'] if s['command']==111]
    records = {r['seq']:r for r in trace['records']}
    source, returned = records[sample['source']], records[sample['api_end']]
    previous, response = capture['frames'][154], capture['frames'][155]
    prepared, presented = records[response['prepare']],records[response['present_begin']]
    if not (source['qpc'] <= returned['qpc'] <= prepared['qpc'] <= presented['qpc'] and
            records[previous['present_begin']]['qpc'] < returned['qpc']):
        raise ValueError('Input/stationary-image/first-response order differs')
    files = [launch_path,trace_path,config,stage,
             base/(label+'-pixels.csv'),base/(label+'-pixels.bin')]
    return dict(label=label,neo=neo,accepted_responses=1,command=111,first_response=155,
        composition_size=size,tracks=tracks,executable_sha256=launch['executable_sha256'],
        stage_sha256=launch['stage_sha256'],
        pad_return_to_response_present_ms=(presented['qpc']-returned['qpc'])*1000/trace['frequency'],
        sources=[dict(path=str(p),sha256=digest(p)) for p in files])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    base,neo=review(False),review(True)
    if any(base[k]!=neo[k] for k in ('executable_sha256','stage_sha256')):
        raise ValueError('Pair uses different binaries')
    result=dict(ordinary=base,neo=neo,reviewer_sha256=digest(Path(__file__)),
        scope='One inspected first-response pair, same ordinary slot01 in both modes. Ordinary already uses ahead2. Not a latency distribution or physical input measurement.')
    write_json_new(args.output,result)
    print(json.dumps({k:result[k]['pad_return_to_response_present_ms'] for k in ('ordinary','neo')}))


if __name__=='__main__':
    main()
