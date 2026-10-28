"""Pair Guwange's original arcade-port read with its actual queued draw/Present."""
import argparse
from collections import Counter
import json
from pathlib import Path
import sys

from input_latency_report import distribution
from latency_probe import read_trace, summarize_trace
from project import digest, write_json_new

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/latency'))
from prepare_guwange_xenia import SITES

ADDRESSES = {identifier: pc for specs in SITES.values() for identifier, pc, _ in specs}
LOW32 = 0xffffffff


def direction(value):
    # Original IN0 low-byte read at D00011: active-low bits1..4.
    # Verify the actual port value against the independently published input.
    return ((~value) >> 1) & 15


def queues(rows):
    """Join actual producer/dequeue slots, not time-near repeated list pointers."""
    publications, pending, consumed, last, draws, draw_pending, groups = {}, {}, {}, {}, {}, {}, {}
    group = {}
    for r in rows:
        if r['kind'] != 10:
            if r['kind'] == 13 and r['thread'] in group:
                group[r['thread']]['events'].append(r)
            continue
        site, thread = r['a'], r['thread']
        if site in (21, 22):
            key = (r['d'] & LOW32, ((r['e'] + r['f']) & LOW32))
            publications[key] = r
        elif site == 11:
            pending[thread] = r
        elif site == 12:
            before = pending.pop(thread, None)
            if not before:
                continue
            key = (before['c'] & LOW32, (before['d'] + before['e']) & LOW32)
            producer = publications.get(key)
            valid = bool(producer and r['c'] == producer['c'] and r['d'] & LOW32 == key[0]
                         and producer['qpc'] <= before['qpc'] <= r['qpc'])
            entry = dict(before=before, after=r, producer=producer, valid=valid)
            last[thread] = entry
            if valid and producer['seq'] not in consumed:
                consumed[producer['seq']] = entry
        elif site == 13:
            draw_pending[thread] = (r, last.get(thread))
        elif site == 14:
            entry = draw_pending.pop(thread, None)
            if entry and entry[1] and entry[1]['valid']:
                draws[entry[1]['after']['seq']] = dict(begin=entry[0], end=r)
        if site == 15:
            entry = last.get(thread)
            group[thread] = dict(entry=entry, events=[r])
            if entry and entry['valid']:
                groups[entry['after']['seq']] = group[thread]
        elif thread in group and site in (16, 17, 18, 19, 20):
            group[thread]['events'].append(r)
    return consumed, draws, groups


def analyze(trace, first=101, last=160):
    if not trace['complete']:
        raise ValueError('Complete trace required')
    rows = sorted(trace['records'], key=lambda r:(r['qpc'], r['seq']))
    by_id = {r['seq']: r for r in rows}
    observations = [r for r in rows if r['kind'] == 10]
    if any(ADDRESSES.get(r['a']) != r['b'] for r in observations):
        raise ValueError('Not the pinned original Guwange instruction')
    summary = summarize_trace(trace)
    if summary['invalid_links'] or summary['guest_submit_pairing_errors']:
        raise ValueError('Invalid original API/output ownership')
    calls = {c['end_seq']: c for c in summary['input_calls']}
    outputs = {}
    for item in summary['output_responses']:
        if item['first_present']:
            outputs.setdefault(item['guest_submit_seq'], []).append(item)
    dequeues, draws, render_groups = queues(rows)
    # Keep the earliest actual core read even when its surrounding graphics
    # evidence is incomplete. A later complete frame must not replace it.
    latest, first_core = {}, {}
    for r in rows:
        if r['kind'] == 15 and r['b'] == 0:
            latest[r['thread']] = r
        elif r['kind'] == 10 and r['a'] == 45:
            sample = latest.get(r['thread'])
            source = by_id.get(sample['a']) if sample else None
            if source and source['kind'] == 14 and source['c'] == 1 and direction(r['c']) == source['b'] & 15:
                first_core.setdefault(source['seq'], r['seq'])
    cuts = [i for i, r in enumerate(rows) if r['kind'] == 10 and r['a'] == 1]
    accepted, issues = {}, Counter()
    for start, stop in zip(cuts, cuts[1:]):
        part = rows[start:stop]
        own = [r for r in part if r['kind'] == 10 and r['thread'] == rows[start]['thread']]
        def need(condition, why):
            if not condition:
                raise ValueError(why)
        def one(identifier):
            found = [r for r in own if r['a'] == identifier]
            need(len(found) == 1, 'missing_or_multiple_main_core_boundary')
            return found[0]
        try:
            s = {identifier:one(identifier) for identifier in (1,2,5,6,7,8,9,10,21,22,30,31,44,45)}
            need(s[1]['qpc'] <= s[2]['qpc'] <= s[5]['qpc'] <= s[30]['qpc'] <= s[44]['qpc'] <=
                 s[45]['qpc'] <= s[31]['qpc'] <= s[6]['qpc'] <= s[7]['qpc'] <= s[21]['qpc'] <=
                 s[8]['qpc'] <= s[9]['qpc'] <= s[22]['qpc'] <= s[10]['qpc'], 'original_cycle_order')
            need(s[30]['f'] & LOW32 == 0x821CCA7C, 'wrong_original_arcade_cpu_caller')
            need((s[44]['c'] - 0xD00011) & LOW32 == s[31]['e'] & LOW32 and
                 s[44]['d'] == s[45]['d'] and s[44]['f'] == s[45]['f'] and s[45]['c'] <= 255,
                 'wrong_arcade_port_context_or_load_pair')
            candidates = [calls[r['seq']] for r in part if r['seq'] in calls
                for c in [calls[r['seq']]] if c['user'] == c['result'] == 0 and
                by_id[c['end_seq']]['thread'] == s[1]['thread'] and
                s[1]['qpc'] <= by_id[c['begin_seq']]['qpc'] <= by_id[c['end_seq']]['qpc'] <= s[2]['qpc']]
            need(len(candidates) == 1, 'missing_unique_original_input_read')
            call = candidates[0]
            sampled = [r for r in part if r['kind'] == 15 and r['thread'] == s[1]['thread'] and
                by_id[call['begin_seq']]['qpc'] <= r['qpc'] <= by_id[call['end_seq']]['qpc'] and
                (r['b'],r['c'],r['d']) == (0,call['packet'],call['buttons'])]
            need(len(sampled) == 1, 'missing_exact_synthetic_state')
            source = by_id[sampled[0]['a']]
            need(source['kind'] == 14 and source['c'] == 1 and first <= source['a'] <= last,
                 'outside_requested_input')
            need(direction(s[45]['c']) == source['b'] & 15, 'direction_not_read_by_arcade_core')
            need(first_core.get(source['seq']) == s[45]['seq'], 'later_core_cannot_replace_first')
            buffer = s[7]['e'] & LOW32
            need((s[7]['c'] & LOW32,s[9]['c'] & LOW32) == (0x821F7620,0x821F7720) and
                 s[21]['c'] == buffer and s[22]['c'] == (1 << 32) | buffer and
                 s[21]['d'] == s[22]['d'], 'wrong_original_list_publication')
            consumed = [dequeues.get(s[identifier]['seq']) for identifier in (21,22)]
            need(all(consumed), 'original_queue_slot_not_consumed')
            draw = draws.get(consumed[0]['after']['seq'])
            group = render_groups.get(consumed[1]['after']['seq'])
            need(draw is not None and group is not None, 'missing_owned_draw_or_frame_task')
            need((draw['begin']['c'] & LOW32,draw['begin']['e'] & LOW32,draw['end']['d'] & LOW32) ==
                 (buffer,buffer,buffer) and draw['begin']['f'] == s[21]['d'] and
                 draw['end']['qpc'] <= consumed[1]['after']['qpc'], 'wrong_rendered_list_owner')
            events = group['events']
            need([(r['kind'],r['a'] if r['kind'] == 10 else 0) for r in events] ==
                 [(10,15),(10,16),(10,17),(10,18),(10,19),(13,0),(10,20)], 'incomplete_original_frame_swap')
            need(events[0]['c'] == 1 and events[0]['e'] == s[21]['d'] and
                 events[0]['d'] & LOW32 == s[7]['d'] & LOW32, 'wrong_frame_task_context')
            submit = events[5]
            owned = outputs.get(submit['seq'], [])
            need(len(owned) == 1, 'missing_unique_packet_owned_present')
            output = owned[0]
            points = dict(source=source, input_api_return=by_id[call['end_seq']],
                core_begin=s[30], game_input=s[45], core_end=s[31],
                enqueue_draw=s[21], draw_begin=draw['begin'], draw_end=draw['end'],
                enqueue_frame=s[22], wait_begin=events[1], wait_end=events[2],
                guest_submit=submit, present_begin=by_id[output['present_begin_seq']],
                present_return=by_id[output['present_end_seq']])
            pairs = (('source','game_input'),('input_api_return','game_input'),
                ('core_begin','game_input'),('game_input','core_end'),('core_end','enqueue_draw'),
                ('enqueue_draw','draw_begin'),('draw_begin','draw_end'),('wait_begin','wait_end'),
                ('guest_submit','present_begin'),('game_input','present_begin'),
                ('input_api_return','present_begin'),('source','present_begin'))
            times = {}
            for left,right in pairs:
                need(points[left]['qpc'] <= points[right]['qpc'], 'reversed_paired_boundary')
                times[left+'_to_'+right+'_us'] = (points[right]['qpc']-points[left]['qpc'])*1e6/trace['frequency']
            accepted[source['seq']] = dict(command=source['a'],buttons=source['b'],
                arcade_port_value=s[45]['c'],arcade_context=s[31]['e'] & LOW32,buffer=buffer,
                points={k:r['seq'] for k,r in points.items()},times=times,
                publications=[s[i]['seq'] for i in (21,22)],
                dequeued_slots=[dict(before=c['before']['seq'],after=c['after']['seq']) for c in consumed])
        except ValueError as error:
            issues[str(error)] += 1
    sources = [r for r in rows if r['kind'] == 14 and r['c'] == 1 and first <= r['a'] <= last]
    samples = [accepted[r['seq']] for r in sources if r['seq'] in accepted]
    return dict(scope='Synthetic state -> original API -> actual arcade port read D00011 -> same CPU frame and exact queue slots -> owned Present CPU begin',
        requested_sources=len(sources),paired_sources=len(samples),samples=samples,
        rejected_cycles=dict(issues),unpaired_commands=[r['a'] for r in sources if r['seq'] not in accepted],
        statistics={k:distribution([s['times'][k] for s in samples],60) for k in (samples[0]['times'] if samples else [])},
        input_boundary='Actual original byte load882CE5E0 observed after it at882CE5E4; active-low IN0 bits1..4',
        unmeasured=['physical_or_Windows_frontend','first_response_pixel','same_response_OS_display','panel_photons'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--logs', type=Path, nargs='+', default=[])
    parser.add_argument('--clock', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    trace = read_trace(args.trace.read_text())
    if bool(args.logs) != bool(args.clock):
        parser.error('Native output logs and measured clock are required together')
    combined = trace
    if args.logs:
        from evac_futari_latency import merge_output
        combined = merge_output(trace, [p.read_text(encoding='utf-8') for p in args.logs],
                                json.loads(args.clock.read_text()))
    report = analyze(combined)
    if args.logs:
        report['native_capture'] = {k:trace[k] for k in ('pid','complete','committed','overflow','incomplete')}
        report['sources'] = [dict(path=str(p.resolve()),sha256=digest(p))
                            for p in (args.trace,args.clock,*args.logs)]
        report['clock_precision_us'] = 1
        report['guest_submit_boundary'] = 'entry to VdSwap; Xenia records VdSwap return'
        report['unmeasured'].append('native_async_output_log_observer_cost')
    else:
        report['source'] = dict(path=str(args.trace.resolve()),sha256=digest(args.trace))
    report['capture'] = {k:trace[k] for k in ('pid','complete','committed','overflow','incomplete')}
    write_json_new(args.output, report)
    print(json.dumps({k:v for k,v in report.items() if k != 'samples'}, indent=2))


if __name__ == '__main__':
    main()
