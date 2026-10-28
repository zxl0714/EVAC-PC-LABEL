"""Revalidate Guwange's internal ON/OFF controls and explicit first responses."""
import argparse
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from evac_futari_latency import merge_output
from latency_probe import read_trace
from pixel_probe import read_capture
from project import digest, write_json_new
from review_guwange_pixels import checked, reviewed_response


def check_pair(on,off,on_launch,off_launch):
    for key in ('executable_sha256','config_sha256','stage_sha256'):
        if on_launch[key]!=off_launch[key]:
            raise ValueError('ON/OFF binary, configuration or stage differs')
    for report,launch,pixels in ((on,on_launch,True),(off,off_launch,False)):
        if (not report['capture']['complete'] or report['capture']['overflow'] or
                report['capture']['pid']!=launch['pid'] or
                report['paired_sources']!=report['requested_sources'] or
                report['unpaired_commands'] or not report['paired_sources']):
            raise ValueError('Incomplete or unrelated internal control')
        if ('EVAC_LATENCY_PIXELS' in launch['environment'])!=pixels:
            raise ValueError('Pixel control state differs from the launch receipt')
    return dict(on_count=on['paired_sources'],off_count=off['paired_sources'],
        on_statistics=on['statistics'],off_statistics=off['statistics'],
        scope='Core-port-owned output only; not first-responsive-image latency',
        interpretation='No added whole output cycle in this internal ON/OFF check; not zero observer cost or a submillisecond ranking')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    base=ROOT/'.local/input-latency-v080-r1'
    specs=(dict(runtime='native',on='evac-guwange-cycle-r2.json',off='evac-guwange-cycle-r3.json',
        on_prefix='evac-guwange-r3',off_prefix='evac-guwange-r4',owned=17,
        trackers=['evac-guwange-first-response-r3.json','evac-guwange-first-response-r4.json']),
        dict(runtime='xenia',on='xenia-guwange-cycle-r3.json',off='xenia-guwange-cycle-r4.json',
        on_prefix='xenia-guwange-r6',off_prefix='xenia-guwange-r7',owned=16,
        trackers=['xenia-guwange-first-response-r1.json','xenia-guwange-first-response-r2.json']))
    results={};sources=[]
    for spec in specs:
        paths=[base/spec['on'],base/spec['off'],
               base/(spec['on_prefix']+'-launch.json'),base/(spec['off_prefix']+'-launch.json')]
        on,off=[checked(p) for p in paths[:2]]
        on_launch,off_launch=[json.loads(p.read_text()) for p in paths[2:]]
        result=check_pair(on,off,on_launch,off_launch)
        manual_path=base/(spec['runtime']+'-guwange-manual-first-response-r1.json')
        manual=checked(manual_path)
        trace_path=base/(spec['on_prefix']+'-trace.csv')
        trace=read_trace(trace_path.read_text())
        xenia=spec['runtime']=='xenia'
        if not xenia:
            inputs=[Path(s['path']) for s in on['sources']]
            logs=[p for p in inputs if p.suffix=='.log']
            clock=base/'clock-check-r1.json'
            trace=merge_output(trace,[p.read_text(encoding='utf-8') for p in logs],json.loads(clock.read_text()))
        response=reviewed_response(trace,on,read_capture(base/(spec['on_prefix']+'-pixels')),
            [checked(base/p) for p in spec['trackers']],xenia=xenia,owned_index=spec['owned'])
        if any(manual[key]!=value for key,value in response.items()):
            raise ValueError('Previously inspected first-response decision no longer revalidates')
        result['first_response']=response
        results[spec['runtime']]=result
        sources += [dict(path=str(p),sha256=digest(p)) for p in [*paths,manual_path]]
    report=dict(scene='XBLA0.0.0.4 / Arcade / Shishin / Stage1 / direction-only pulses',
        internal_controls=results,
        conclusion='Each explicitly inspected first response appears two consecutive outputs after its input-core-owned output. API return to first response Present is about50ms in both implementations; a one-cycle internal output chain is not a one-cycle responsive image.',
        response_sample_count=dict(native=1,xenia=1),response_p95_ms=None,
        display_reference='144Hz / screen center / theory only; unknown panel processing/response D',
        unmeasured=['physical_or_Windows_frontend','same_response_OS_display','panel_photons'],
        scope_limits=['Single explicitly inspected response per implementation, not all modes or a first-response distribution',
            'Native and Xenia stage start offsets differ; no submillisecond winner inferred',
            'Earlier R1 overflow, R4 paused inputs, R5 wrong ROI and native R1 configuration failure remain excluded'],
        sources=sources)
    write_json_new(args.output,report)
    print(json.dumps(dict(output=str(args.output),sha256=digest(args.output),
        first_responses={k:v['first_response']['input_api_return_to_response_present_ms'] for k,v in results.items()},
        off_api_to_owned_present={k:v['off_statistics']['input_api_return_to_present_begin_us'] for k,v in results.items()}),indent=2))


if __name__=='__main__':
    main()
