"""Compare exact ready-to-Present intervals, not first-response input latency."""
import argparse
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from input_latency_report import distribution
from latency_probe import read_trace
from project import digest,write_json_new


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--on',required=True)
    parser.add_argument('--off',required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    base=ROOT/'.local/input-latency-v080-r1'
    output=args.output.resolve()
    if not output.is_relative_to(base): raise ValueError('Private output required')
    reports=[];receipts=[];sources=[Path(__file__)]
    for label,pixels_on in ((args.on,True),(args.off,False)):
        if Path(label).name!=label or not label.replace('-','').isalnum(): raise ValueError('ASCII capture label required')
        trace_path,receipt_path=(base/(label+suffix) for suffix in ('-trace.csv','-launch.json'))
        receipt=json.loads(receipt_path.read_text());receipts.append(receipt)
        trace=read_trace(trace_path.read_text())
        if (not trace['complete'] or trace['pid']!=receipt['pid'] or
            bool(receipt['environment'].get('EVAC_LATENCY_PIXELS'))!=pixels_on):
            raise ValueError('Complete matching process and explicit pixel state required')
        rows=trace['records'];by_id={r['seq']:r for r in rows}
        inputs=[r for r in rows if r['kind']==14 and r['c']==1 and 101<=r['a']<=112]
        if [r['a'] for r in inputs]!=list(range(101,113)):
            raise ValueError('The twelve requested source events differ')
        presents=[r for r in rows if r['kind']==22 and inputs[0]['qpc']<=r['qpc']<=inputs[-1]['qpc']]
        tails=[]
        for present in presents:
            ready=by_id.get(present['a']);prepare=by_id.get(ready['a']) if ready else None
            if (not ready or not prepare or ready['kind']!=21 or prepare['kind']!=26 or
                ready['c']!=prepare['a'] or not prepare['qpc']<=ready['qpc']<=present['qpc']):
                raise ValueError('Not the exact owned image readiness chain')
            tails.append((present['qpc']-ready['qpc'])*1e6/trace['frequency'])
        intervals=[(b['qpc']-a['qpc'])*1e6/trace['frequency'] for a,b in zip(presents,presents[1:])]
        reports.append(dict(label=label,pid=trace['pid'],capture_complete=True,records=trace['committed'],
            pixels_on=pixels_on,present_modes=sorted(set(r['d'] for r in presents)),
            ready_to_present=distribution(tails,60),present_intervals=distribution(intervals,60)))
        sources += [trace_path,receipt_path]
    for key in ('command','executable_sha256','config_sha256','stage_sha256'):
        if receipts[0][key]!=receipts[1][key]: raise ValueError('Build/config/launch contract differs: '+key)
    write_json_new(output,dict(scope='Exact image ready-handoff -> CPU Present begin, during requested source interval; not first-response latency',
        same_build_config_contract=True,runs=reports,
        conclusion='Similar presentation queue timing with and without ROI; does not establish zero observer cost',
        live_first_response_off_control_verified=False,
        off_final_observation='GAME OVER at later CU observation; live state throughout the sampled interval not independently verified',
        sources=[dict(path=str(p),sha256=digest(p)) for p in sources]))
    print(json.dumps(dict(runs=reports,sha256=digest(output)),indent=2))


if __name__=='__main__': main()
