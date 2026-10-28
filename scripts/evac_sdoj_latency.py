"""Join SDOJ's existing native lineage observer with the private input QPC trace."""
import argparse
from collections import Counter
import json
from pathlib import Path
import re

from evac_futari_latency import merge_output
from input_latency_report import distribution
from latency_probe import read_trace, summarize_trace
from project import digest, write_json_new
from sdoj_latency import input_records, records

BLOCK=0x888752F0


def merge_sdoj_points(trace,texts,clock):
    """Add only actual log observations, with the independently checked clock."""
    merged=merge_output(trace,texts,clock)
    sequence=max(r['seq'] for r in merged['records'])
    lo,hi=min(r['qpc'] for r in trace['records']),max(r['qpc'] for r in trace['records'])
    rows=list(merged['records'])
    native_points={}
    for text in texts:
        for event in records(text.splitlines()):
            if event['phase'] not in ('logic_copy','guest_submit'):
                continue
            qpc=event['t_us']*trace['frequency']//1000000
            if not lo<=qpc<=hi:
                continue
            if event['phase']=='logic_copy':
                if event.get('module')!=1 or event.get('argument')!=BLOCK or not event.get('valid') or not event.get('swap_stable'):
                    continue
                kind,values=10,(4,0x88051D74,event['current0'],event['current1'],event['seq'],BLOCK)
            else:
                if not event.get('valid'):
                    continue
                kind,values=13,(event['packet'],0,0,0,event['seq'],0)
            if event['seq'] in native_points:
                raise ValueError('Duplicate native boundary identity')
            sequence+=1
            native_points[event['seq']]=sequence
            rows.append(dict(seq=sequence,qpc=qpc,thread=event['thread'],kind=kind,
                             **dict(zip(('a','b','c','d','e','f'),values))))
    rows.sort(key=lambda r:(r['qpc'],r['seq']))
    return dict(merged,records=rows,committed=len(rows),reserved=len(rows),
                capacity=max(merged['capacity'],len(rows)),native_point_ids=native_points)


def analyze(trace,texts,clock,lineage,first=101,last=160):
    if not lineage['lineage_summary']['continuous_trace']:
        raise ValueError('Continuous original native lineage required')
    merged=merge_sdoj_points(trace,texts,clock)
    by_id={r['seq']:r for r in merged['records']}
    summary=summarize_trace(merged)
    if summary['invalid_links']:
        raise ValueError('Native output/API identity is invalid')
    # SDOJ's original text observer deliberately starts only after a game
    # module is seen. Startup Presents therefore have no guest-submit log.
    # Require the packet join for every selected response, not for title video.
    outputs_by_begin={r['present_begin_seq']:r for r in summary['output_responses']}
    inputs,events=[],[]
    for text in texts:
        lines=text.splitlines()
        observed=input_records(lines)
        for item in observed:
            match=re.search(r'\[t(\d+)\]',lines[item['line']-1])
            if match:
                item['thread']=int(match[1])
        inputs.extend(observed)
        events.extend(records(lines))
    inputs.sort(key=lambda r:r['t_us']);events.sort(key=lambda r:(r['t_us'],r['seq']))
    calls=summary['input_calls']
    copies=[r for r in events if r['phase']=='logic_copy']
    frames={}
    for frame in lineage['frames']:
        if frame['first_result_output']:
            frames.setdefault(frame['lineage']['copy']['seq'],frame)
    present_by_paint={r['e']:r for r in merged['records'] if r['kind']==8}
    sources=[r for r in trace['records'] if r['kind']==14 and r['c']==1 and first<=r['a']<=last]
    samples=[];issues=Counter()
    for source in sources:
        # InputSystem intentionally replaces the driver's packet number with
        # its own per-user change counter. Follow the original pass scope and
        # exact synthetic sample ID, not equality of the two packet counters.
        candidates=[]
        for poll in inputs:
            if not (poll.get('phase')=='poll_result' and poll.get('user')==0 and
                    poll.get('status')==0 and poll.get('valid') and poll.get('buttons')==source['b']):
                continue
            passes=[r for r in inputs if r.get('phase')=='engine_input_pass' and r['frame']==poll['frame'] and
                r.get('thread')==poll.get('thread') and r['began_us']<=poll['t_us']<=r['t_us']]
            if len(passes)!=1:
                continue
            began=passes[0]['began_us']
            api=[c for c in calls if c['user']==c['result']==0 and c['packet']==source['seq'] and
                 c['buttons']==source['b'] and by_id[c['end_seq']]['thread']==poll.get('thread') and
                 began<=by_id[c['end_seq']]['qpc']*1000000//trace['frequency']<=poll['t_us']]
            if len(api)==1:
                candidates.append((poll,api[0]))
        if len(candidates)!=1:
            issues['missing_or_ambiguous_original_pass_sample']+=1;continue
        poll,call=candidates[0]
        synthetic=[r for r in trace['records'] if r['kind']==15 and r['a']==source['seq'] and r['b']==0 and
            r['c']==call['packet'] and r['d']==call['buttons'] and
            by_id[call['begin_seq']]['qpc']<=r['qpc']<=by_id[call['end_seq']]['qpc']]
        if len(synthetic)!=1:
            issues['missing_exact_published_state_sample']+=1;continue
        deadline=min([poll['t_us']+500000]+[r['t_us'] for r in inputs if
            r.get('phase')=='poll_result' and r.get('user')==0 and r['t_us']>poll['t_us']])
        built=next((r for r in inputs if r.get('phase')=='core_mask_built' and r.get('module')==1 and
            poll['t_us']<=r['t_us']<deadline and r.get('current0',0)&15==source['b']&15),None)
        copied=next((r for r in copies if built and (r.get('module'),r.get('generation'))==(1,built['generation']) and
            built['t_us']<=r['t_us']<deadline and (r['current0'],r['current1'])==(built['current0'],built['current1'])),None)
        if not copied:
            issues['no_first_core_consumption']+=1;continue
        frame=frames.get(copied['seq'])
        if not frame:
            issues['first_core_has_no_owned_output']+=1;continue
        present=present_by_paint.get(frame['present']['paint'])
        core_id=merged['native_point_ids'].get(copied['seq'])
        submit_id=merged['native_point_ids'].get(frame['submit']['seq'])
        if not present or not core_id or not submit_id:
            issues['native_boundary_outside_QPC_capture']+=1;continue
        owned=outputs_by_begin.get(present['seq'])
        if not owned or owned['guest_submit_seq']!=submit_id or not owned['first_present']:
            issues['selected_output_packet_not_owned']+=1;continue
        points=dict(source=source['seq'],input_api_return=call['end_seq'],game_input=core_id,
                    guest_submit=submit_id,present_begin=present['seq'])
        if (frame['present']['begin_us']!=present['qpc']*1000000//trace['frequency'] or
            not source['qpc']<=by_id[call['end_seq']]['qpc']<=by_id[core_id]['qpc']<=present['qpc']):
            raise ValueError('Exact paired event clocks disagree')
        intervals=dict(frame['intervals_us'])
        intervals.update(source_to_game_input_us=(by_id[core_id]['qpc']-source['qpc'])*1e6/trace['frequency'],
            game_input_to_present_begin_us=frame['present']['begin_us']-copied['t_us'],
            game_input_to_present_end_us=frame['present']['t_us']-copied['t_us'],
            source_to_present_begin_us=(present['qpc']-source['qpc'])*1e6/trace['frequency'])
        samples.append(dict(command=source['a'],buttons=source['b'],mapped_buttons=copied['current0'],
            points=points,times=intervals,native_copy_seq=copied['seq'],
            native_buffer=frame['lineage']['began']['argument'],native_serial=frame['lineage']['swap']['swap_serial'],
            native_poll=poll,core_mask_build=built))
    return dict(scope='Synthetic controller state -> original SDOJ poll/build/first core copy -> existing buffer/queue/swap lineage -> Present CPU begin',
        native_capture={k:trace[k] for k in ('pid','committed','complete','overflow','incomplete')},
        requested_sources=len(sources),paired_sources=len(samples),issues=dict(issues),samples=samples,
        packet_boundary='Configured driver publication ID -> original InputSystem change counter; joined by original pass/thread/sample identity',
        nonselected_output_packet_issues=len(summary['guest_submit_pairing_errors']),
        unpaired_commands=[r['a'] for r in sources if r['a'] not in {s['command'] for s in samples}],
        statistics={key:distribution([s['times'][key] for s in samples if key in s['times']],60)
                    for key in (samples[0]['times'] if samples else [])},
        unmeasured=['physical_or_Windows_frontend','first_response_pixel','same_response_OS_display','panel_photons'])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace',type=Path)
    parser.add_argument('--logs',type=Path,nargs='+',required=True)
    parser.add_argument('--clock',type=Path,required=True)
    parser.add_argument('--lineage',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    lineage=json.loads(args.lineage.read_text())
    for item in lineage['sources']:
        if digest(Path(item['path']))!=item['sha256']:
            raise ValueError('Native lineage log identity changed')
    report=analyze(read_trace(args.trace.read_text()),[p.read_text(encoding='utf-8') for p in args.logs],
                   json.loads(args.clock.read_text()),lineage)
    report['sources']=[dict(path=str(p.resolve()),sha256=digest(p))
                       for p in (args.trace,*args.logs,args.clock,args.lineage)]
    write_json_new(args.output,report)
    print(json.dumps({k:v for k,v in report.items() if k not in ('samples','sources')},indent=2))


if __name__=='__main__':
    main()
