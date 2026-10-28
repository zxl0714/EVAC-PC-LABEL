"""Record the explicitly inspected SDOJ first response without lowering NCC gates."""
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from latency_probe import read_trace
from pixel_probe import read_capture
from project import digest, write_json_new


def main():
    base=ROOT/'.local/input-latency-v080-r1'
    trace_path=base/'xenia-sdoj-r3-trace.csv'
    chain_path=base/'xenia-sdoj-cycle-r3.json'
    chain=json.loads(chain_path.read_text())
    trace=read_trace(trace_path.read_text())
    if chain['source']['sha256']!=digest(trace_path):
        raise ValueError('Chain input changed')
    capture=read_capture(base/'xenia-sdoj-r3-pixels')
    if capture['header']['pid']!=trace['pid'] or capture['footer']['overflow']:
        raise ValueError('Pixel identity/coverage changed')
    sample,=[s for s in chain['samples'] if s['command']==101]
    records={r['seq']:r for r in trace['records']}
    present=records[sample['points']['present_begin']]
    frame=capture['frames'][18]
    if ((frame['source_event'],frame['source_packet'])!=(present['a'],present['c']) or
        abs(frame['present_begin']-present['qpc'])>trace['frequency']*3e-6):
        raise ValueError('Reviewed image is not this input batch owned Present')
    tracker_paths=[base/f'xenia-sdoj-first-response-r{n}.json' for n in (1,2)]
    trackers=[json.loads(p.read_text()) for p in tracker_paths]
    for r in trackers:
        for source in r['sources']:
            if digest(Path(source['path']))!=source['sha256']:
                raise ValueError('Tracker source changed')
    checks=[next(e for e in r['examined'] if e['command']==101) for r in trackers]
    if any(c['frame_indexes']!=list(range(15,22)) for c in checks):
        raise ValueError('Reviewed first-response neighborhood differs')
    offsets=[[x-c['x'][0] for x in c['x']] for c in checks]
    if (any(max(o[:3])-min(o[:3])>1 for o in offsets) or
        any(not 2<=o[3]<=12 or o[4]-o[3]<2 for o in offsets) or
        any(abs(a-b)>1 for a,b in zip(*offsets))):
        raise ValueError('Independent feature positions no longer support the inspection')
    panel=base/'xenia-sdoj-first101-review-r1.png'
    output=base/'xenia-sdoj-manual-first-response-r1.json'
    result=dict(method='Explicit visual inspection of frames15..20 plus two independent body-template position checks; not an automated NCC pass',
        accepted_responses=1,command=101,baseline_frames=[15,16,17],first_response_frame=18,
        next_sustained_frame=19,owned_frame_offset=0,relative_body_positions=offsets,
        observation='The same live golden player body is stationary in15..17, shifts right in18, and continues right in19..20. Animated colors/fire make the unmodified automated confidence gate reject the neighborhood.',
        game_input_to_response_present_ms=sample['times']['game_input_to_present_begin_us']/1000,
        source_to_response_present_ms=sample['times']['source_to_present_begin_us']/1000,
        p95_ms=None,other_responses_not_visually_accepted=True,
        sources=[dict(path=str(p),sha256=digest(p)) for p in [trace_path,chain_path,*tracker_paths,panel,
            base/'xenia-sdoj-r3-pixels.csv',base/'xenia-sdoj-r3-pixels.bin']],
        unmeasured=['physical_input_frontend','OS_display','panel_light'],
        display_reference='144Hz / screen center / theory only')
    write_json_new(output,result)
    print(json.dumps(dict(output=str(output),sha256=digest(output),
                          accepted=1,core_to_present_ms=result['game_input_to_response_present_ms']),indent=2))


if __name__=='__main__':
    main()
