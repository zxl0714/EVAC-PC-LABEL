"""Join original SDOJ input/cmd-buffer/queue ownership to Xenia Present."""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path

from input_latency_report import distribution
from latency_probe import read_trace, summarize_trace
from project import digest, write_json_new

SITES = {1:0x82178B78, 2:0x82178BFC, 3:0x880344B4, 23:0x880344CC,
    4:0x88051D88, 5:0x880340D8, 6:0x8803417C, 7:0x88033178,
    8:0x88031288, 9:0x880313C0, 10:0x8219DCFC, 11:0x8219DDE8,
    12:0x8219DEA4, 13:0x8219DEC8, 14:0x8219D148, 15:0x8219D9B0,
    16:0x8219D9C8, 17:0x821DFECC, 18:0x821DFED0, 19:0x88051350,
    20:0x880513D0, 21:0x88051E14}
MASK = 0xffffffff
BLOCK = 0x888752F0


def analyze(trace, first=101, last=160):
    if not trace['complete']:
        raise ValueError('Complete capture required')
    summary = summarize_trace(trace)
    if summary['invalid_links'] or summary['guest_submit_pairing_errors'] or summary['guest_observations']['invalid_sites']:
        raise ValueError('Invalid API/output/site identity')
    rows = sorted(trace['records'], key=lambda r:(r['qpc'],r['seq']))
    by_id = {r['seq']:r for r in rows}
    inputs = {c['end_seq']:c for c in summary['input_calls']}
    outputs = {o['guest_submit_seq']:o for o in reversed(summary['output_responses'])
               if o['first_present'] and o['guest_submit_seq'] is not None}
    iteration, last_sample, last_input, callbacks = {}, {}, {}, {}
    latest_build = latest_api = None
    copies, writing, complete, publications, active_render = {}, {}, {}, {}, {}
    slots, pop_begin, drawings, frames, swap_calls = {}, {}, defaultdict(list), {}, {}
    first_core, chains, issues = {}, [], Counter()
    for r in rows:
        thread, kind = r['thread'], r['kind']
        if kind == 15 and r['b'] == 0:
            last_sample[thread] = r
        elif kind == 4 and r['seq'] in inputs:
            call = inputs[r['seq']]
            sample = last_sample.get(thread)
            if call['user'] == call['result'] == 0 and sample:
                source = by_id.get(sample['a'])
                if (source and source['kind'] == 14 and source['b'] == call['buttons'] and
                    sample['c'] == call['packet'] and sample['d'] == call['buttons'] and
                    by_id[call['begin_seq']]['qpc'] <= sample['qpc'] <= r['qpc']):
                    last_input[thread] = dict(source=source, input_api_return=r,
                                             iteration=iteration.get(thread))
                    latest_api = last_input[thread]
        elif kind == 13:
            pending = swap_calls.get(thread)
            if pending:
                pending['submits'].append(r)
        elif kind != 10:
            continue
        if kind != 10:
            continue
        site = r['a']
        if site not in SITES or r['b'] != SITES[site]:
            raise ValueError('Wrong original SDOJ instruction site')
        c,d,e,f = (r[k] & MASK for k in ('c','d','e','f'))
        if site == 1:
            iteration[thread] = r['seq']
            active_render.pop(thread, None)
        elif site == 19:
            candidate = last_input.get(thread)
            callbacks[thread] = (r, candidate if candidate and candidate['iteration'] == iteration.get(thread) else None)
        elif site in (3,23):
            callback = callbacks.get(thread)
            candidate = callback[1] if callback else None
            if e == BLOCK and candidate and c & 15 == candidate['source']['b'] & 15:
                latest_build = dict(candidate, built=r, callback=callback[0], mapped_buttons=c)
            else:
                latest_build = None
        elif site == 20:
            callbacks.pop(thread, None)
        elif site == 4 and e == BLOCK and f == BLOCK:
            # An incomplete early build/cycle must not make a later complete
            # consumption look like the first one for the same source.
            if latest_api and c & 15 == latest_api['source']['b'] & 15:
                first_core.setdefault(latest_api['source']['seq'],r['seq'])
            stamp = latest_build
            if stamp and stamp['mapped_buttons'] == c and stamp['built']['qpc'] <= r['qpc']:
                source = stamp['source']
                first_core.setdefault(source['seq'],r['seq'])
                copies[thread] = dict(stamp, game_input=r)
            else:
                copies.pop(thread,None)
        elif site == 5:
            complete.pop(c,None)
            publications = {key:val for key,val in publications.items() if key[1] != c}
            if thread in writing:
                issues['nested_or_missing_command_end'] += 1
            if c not in (0,MASK) and thread in copies:
                writing[thread] = dict(copies[thread], command_begin=r, buffer=c)
            else:
                writing.pop(thread,None)
        elif site == 6:
            stamp = writing.pop(thread,None)
            if stamp and 0 < c - stamp['buffer'] <= 0x120000 and (c-stamp['buffer']) % 4 == 0:
                complete[stamp['buffer']] = dict(stamp, command_end=r)
            else:
                issues['unpaired_or_invalid_command_end'] += 1
        elif site == 7:
            stamp = complete.get(d)
            if stamp:
                publications.setdefault((c,d),dict(stamp, publication=r, serial=c))
        elif site == 8:
            matches = [p for (serial,pointer),p in publications.items() if pointer == c]
            if len(matches) == 1 and not any(w['buffer'] == c for w in writing.values()):
                active_render[thread] = dict(matches[0], render_source=r, iteration=iteration.get(thread))
            else:
                active_render.pop(thread,None)
                issues['unproven_publication_or_active_writer'] += 1
        elif site == 9:
            if thread in active_render:
                active_render[thread]['render_return'] = r
        elif site in (10,11):
            slot = (d+e) & MASK
            message = dict(event=r, queue=c, slot=slot, type=(r['f']>>32)&MASK, argument=f)
            if message['type'] != (0 if site == 10 else 1):
                raise ValueError('Native queue entry kind differs from original instruction path')
            stamp = active_render.get(thread)
            if stamp and stamp['iteration'] == iteration.get(thread):
                message['stamp'] = stamp
            if (c,slot) in slots:
                issues['queue_slot_reused_without_pop'] += 1
            slots[c,slot] = message
        elif site == 12:
            pop_begin[thread] = dict(queue=c, slot=d, output=e, event=r)
        elif site == 13:
            pop = pop_begin.pop(thread,None)
            producer = slots.pop((c,pop['slot']),None) if pop and pop['queue'] == c else None
            if not producer or (producer['type'],producer['argument']) != (d,e) or pop['output'] != f:
                drawings[thread].clear(); frames.pop(thread,None)
                issues['unpaired_or_changed_native_queue_entry'] += 1
                continue
            message = dict(producer=producer, dequeue=r, pop=pop['event'])
            if d == 0:
                drawings[thread].append(message)
            elif d == 1:
                stamp = producer.get('stamp')
                matches = [draw for draw in drawings[thread] if stamp and
                    draw['producer'].get('stamp',{}).get('render_source',{}).get('seq') == stamp['render_source']['seq']]
                frames.pop(thread,None)
                if matches:
                    frames[thread] = dict(stamp, draw_messages=matches, frame_end=message)
                else:
                    issues['no_owned_draw_before_frame_end'] += 1
                drawings[thread].clear()
        elif site in (15,16):
            if thread in frames:
                frames[thread]['wait_begin' if site == 15 else 'wait_end'] = r
        elif site == 17:
            frame = frames.get(thread)
            if frame and 'wait_begin' in frame and 'wait_end' in frame:
                swap_calls[thread] = dict(frame, swap_begin=r, submits=[])
        elif site == 18:
            stamp = swap_calls.pop(thread,None)
            if not stamp or len(stamp['submits']) != 1:
                continue
            submit = stamp['submits'][0]
            output = outputs.get(submit['seq'])
            if output:
                stamp.update(guest_submit=submit, present_begin=by_id[output['present_begin_seq']],
                             present_end=by_id[output['present_end_seq']], swap_end=r)
                chains.append(stamp)
    sources = [r for r in rows if r['kind'] == 14 and r['c'] == 1 and first <= r['a'] <= last]
    selected = {}
    for chain in chains:
        source, core = chain['source'], chain['game_input']
        if (source not in sources or first_core.get(source['seq']) != core['seq'] or
            source['seq'] in selected):
            continue
        points = {k:chain[k] for k in ('source','input_api_return','built','game_input','command_begin',
            'command_end','publication','render_source','render_return','wait_begin','wait_end',
            'swap_begin','guest_submit','present_begin','present_end') if k in chain}
        order = ('source','input_api_return','built','game_input','command_begin','command_end',
                 'publication','render_source','render_return','wait_begin','wait_end',
                 'swap_begin','guest_submit','present_begin','present_end')
        if any(key not in points for key in order) or any(points[a]['qpc'] > points[b]['qpc'] for a,b in zip(order,order[1:])):
            issues['reversed_or_missing_selected_chain'] += 1
            continue
        pairs = [('source','game_input'),('input_api_return','game_input'),
                 ('game_input','command_begin'),('command_begin','command_end'),
                 ('command_end','publication'),('publication','render_source'),
                 ('render_source','wait_begin'),('wait_begin','wait_end'),
                 ('wait_end','guest_submit'),('guest_submit','present_begin'),
                 ('game_input','present_begin'),('game_input','present_end'),('source','present_begin')]
        times = {a+'_to_'+b+'_us':(points[b]['qpc']-points[a]['qpc'])*1e6/trace['frequency'] for a,b in pairs}
        selected[source['seq']] = dict(command=source['a'],buttons=source['b'],
            mapped_buttons=chain['mapped_buttons'], points={k:v['seq'] for k,v in points.items()},
            buffer=chain['buffer'], serial=chain['serial'], times=times,
            draw_entries=[dict(enqueue=m['producer']['event']['seq'],dequeue=m['dequeue']['seq'],
                               slot=m['producer']['slot'], argument=m['producer']['argument']) for m in chain['draw_messages']],
            frame_end_entry=dict(enqueue=chain['frame_end']['producer']['event']['seq'],
                                 dequeue=chain['frame_end']['dequeue']['seq']))
    samples = list(selected.values())
    return dict(scope='Original-disc SDOJ core input load -> original completed/publication buffer -> exact native queue slots -> packet-owned Present',
        capture=summary['capture'], requested_sources=len(sources), paired_sources=len(samples),
        unpaired_commands=[s['a'] for s in sources if s['seq'] not in selected],
        issues=dict(issues), samples=samples,
        statistics={name:distribution([s['times'][name] for s in samples],60)
                    for name in (samples[0]['times'] if samples else [])},
        unmeasured=['physical_or_Windows_frontend','first_response_pixel','same_response_OS_display','panel_photons'])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    report=analyze(read_trace(args.trace.read_text()))
    report['source']=dict(path=str(args.trace.resolve()),sha256=digest(args.trace))
    report['sources']=[report['source']]
    write_json_new(args.output,report)
    print(json.dumps({k:v for k,v in report.items() if k not in ('samples','sources')},indent=2))


if __name__=='__main__':
    main()
