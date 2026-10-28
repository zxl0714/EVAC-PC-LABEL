"""Collect independently checked representative responses and ROI controls."""
import argparse
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from input_latency_report import distribution
from latency_probe import read_trace
from project import digest,write_json_new


def cadence(trace):
    sources=[r for r in trace['records'] if r['kind']==14 and r['c']==1 and 101<=r['a']<=112]
    if [r['a'] for r in sources]!=list(range(101,113)):raise ValueError('Different requested source interval')
    def intervals(kind,site=None):
        rows=[r for r in trace['records'] if r['kind']==kind and (site is None or r['a']==site)
              and sources[0]['qpc']<=r['qpc']<=sources[-1]['qpc']]
        return distribution([(b['qpc']-a['qpc'])*1e6/trace['frequency'] for a,b in zip(rows,rows[1:])],60)
    return dict(original_callback_intervals=intervals(10,100),present_begin_intervals=intervals(22))


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();base=ROOT/'.local/input-latency-v080-r1';records={};sources=[Path(__file__)]
    if not args.output.resolve().is_relative_to(base):raise ValueError('Private output required')
    names=dict(native='native-daioujou-manual-first-response-r1.json',shad='shad-daioujou-manual-first-response-r1.json',
        native_control='daioujou-native-pixel-control-r1.json',shad_control='daioujou-shad-pixel-control-r1.json')
    for key,name in names.items():
        path=base/name;record=json.loads(path.read_text())
        for item in record['sources']:
            if digest(Path(item['path']))!=item['sha256']:raise ValueError('Original response/control evidence changed')
        records[key]=record;sources.append(path)
    audit_path=base/'daioujou-core-flow-r3/report.json';audit=json.loads(audit_path.read_text())
    for item in audit['sources']:
        path=Path(item['path'])
        if digest(path)!=item['sha256']:raise ValueError('Original game identity changed')
        sources.append(path)
    if any(records[k]['accepted_responses']!=1 or records[k]['command']!=101 for k in ('native','shad')):
        raise ValueError('Expected exactly one manually reviewed response per backend')
    native,shad=records['native'],records['shad']
    result=dict(game='daioujou',scope='ARCADE / Type-A / Shotia / Stage1A, original 01.04 data; one response per backend, differing progression and sampling phase',
        endpoints='Synthetic published state / original PadRead return / original core input delivery -> first visibly responsive image CPU Present begin',
        response_ms={key:{k:records[key][k] for k in ('core_delivery_to_response_present_ms',
            'pad_return_to_response_present_ms','synthetic_source_to_response_present_ms')} for key in ('native','shad')},
        difference_of_these_examples_ms={k:native[k]-shad[k] for k in ('core_delivery_to_response_present_ms',
            'pad_return_to_response_present_ms','synthetic_source_to_response_present_ms')},
        controls={key:records[key+'_control']['runs'] for key in ('native','shad')},cadence={},
        presentation_difference='Native first responsive snapshot replaced before its next response was presented; shad first response submitted directly. Native handoff follows copy completion/pacing; shad ready follows submission, so ready phases are not interchangeable.',
        defaults='Native VSync true; shad FIFO/vblank60, Japanese console language and Circle confirm aligned in isolated config; no gameplay timing code changed',
        statistical_scope='No population first-response median/P95 or fixed V advantage from two separate examples; ON/OFF control covers delivery/presentation timing, not OFF response pixels',
        display_reference='144 Hz / screen CENTER / THEORY: empty FIFO ideal mean6.94ms plus unknown panelD, only from an adoptable GPU-ready image; not a measured Present-to-light interval',
        unmeasured=['physical_switch_USB','Windows_SDL_frontend','actual_emulated_CPU_port_load','OS_display','panel_light'])
    for key,label in (('native','evac-daioujou-pixels-r3'),('shad','shad-daioujou-pixels-r3')):
        path=base/(label+'-trace.csv');trace=read_trace(path.read_text())
        if not trace['complete']:raise ValueError('Complete trace required')
        result['cadence'][key]=cadence(trace);sources.append(path)
    result['sources']=[dict(path=str(p.resolve()),sha256=digest(p)) for p in dict.fromkeys(sources)]
    write_json_new(args.output,result)
    print(json.dumps({k:result[k] for k in ('response_ms','difference_of_these_examples_ms','cadence')},indent=2))
    print('sha256',digest(args.output))


if __name__=='__main__':main()
