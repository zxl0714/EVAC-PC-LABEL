"""Match G2 Xbox360's synchronous input/update/draw cycle to its owned output."""
import argparse
from collections import Counter
import json
from pathlib import Path

from evac_futari_latency import merge_output
from input_latency_report import distribution
from latency_probe import read_trace, summarize_trace
from project import digest, write_json_new

ADDRESSES = {1: 0x820ED8BC, 2: 0x820ED8C4, 3: 0x820CFDA8,
    4: 0x820BB274, 5: 0x820BB278, 6: 0x82170F84, 7: 0x821712CC,
    8: 0x821713A0, 9: 0x821713A8, 11: 0x8814AC90, 12: 0x88022C88,
    13: 0x88022C8C, 14: 0x88022CB0, 15: 0x820F3E40,
    16: 0x820912B8, 17: 0x820912BC, 18: 0x820F418C, 19: 0x820F43D0}
ORDER = (1, 2, 4, 11, 12, 13, 14, 5, 16, 17, 15, 8, 9, 6, 7)
LOW32 = 0xffffffff


def analyze(trace, first=101, last=160):
    if not trace['complete']:
        raise ValueError('Complete trace required')
    summary = summarize_trace(trace)
    if (summary['invalid_links'] or summary['guest_submit_pairing_errors'] or
            summary['guest_observations']['invalid_sites']):
        raise ValueError('Invalid probe identity/output links')
    rows = sorted(trace['records'], key=lambda r: (r['qpc'], r['seq']))
    by_id = {r['seq']: r for r in rows}
    cut = [i for i, r in enumerate(rows) if r['kind'] == 10 and r['a'] == 1]
    call_by_end = {c['end_seq']: c for c in summary['input_calls']}
    out_by_submit = {}
    for out in summary['output_responses']:
        if out['first_present']:
            out_by_submit.setdefault(out['guest_submit_seq'], []).append(out)
    sources = [r for r in rows if r['kind'] == 14 and r['c'] == 1 and first <= r['a'] <= last]
    # Preserve earliest observed core consumption even when its enclosing
    # cycle is rejected; never silently substitute a later complete cycle.
    latest_sample, first_core = {}, {}
    for r in rows:
        if r['kind'] == 15 and r['b'] == 0:
            latest_sample[r['thread']] = r
        elif (r['kind'] == 10 and r['a'] == 11 and r['b'] == ADDRESSES[11]
              and r['d'] == 0 and (r['f'] & LOW32) == 0x82091690):
            observed = latest_sample.get(r['thread'])
            source = by_id.get(observed['a']) if observed else None
            if source and source['kind'] == 14 and (r['c'] & 15) == (source['b'] & 15):
                first_core.setdefault(source['seq'], r['seq'])
    selected, rejected, cycles = {}, Counter(), 0
    frequency = trace['frequency']
    for lo, hi in zip(cut, cut[1:]):
        part = rows[lo:hi]
        def need(ok, why):
            if not ok:
                raise ValueError(why)
        try:
            observations = [r for r in part if r['kind'] == 10 and r['a'] in ORDER
                            and (r['a'] != 11 or r['d'] == 0)]
            need([r['a'] for r in observations] == list(ORDER), 'missing_or_multiple_core_update')
            s = {r['a']: r for r in observations}
            need(all(r['b'] == ADDRESSES[r['a']] for r in observations), 'wrong_original_site')
            thread = s[1]['thread']
            need(all(r['thread'] == thread for r in observations), 'not_synchronous_original_cycle')
            need((s[4]['f'] & LOW32) == 0x880202A8, 'wrong_core_entry')
            need((s[11]['f'] & LOW32) == 0x82091690 and s[11]['e'] == 0, 'wrong_input_callback')
            need((s[4]['d'] & LOW32) == (s[5]['d'] & LOW32), 'wrong_core_owner')
            calls = [call_by_end[r['seq']] for r in part if r['seq'] in call_by_end
                     and s[1]['qpc'] <= r['qpc'] <= s[2]['qpc']
                     and call_by_end[r['seq']]['user'] == 0 and call_by_end[r['seq']]['result'] == 0]
            need(len(calls) == 1, 'ambiguous_original_input_refresh')
            call = calls[0]
            samples = [r for r in part if r['kind'] == 15 and r['thread'] == thread
                and by_id[call['begin_seq']]['qpc'] <= r['qpc'] <= by_id[call['end_seq']]['qpc']
                and (r['b'], r['c'], r['d']) == (0, call['packet'], call['buttons'])]
            need(len(samples) == 1, 'missing_exact_synthetic_sample')
            sample = samples[0]
            source = by_id.get(sample['a'])
            need(source and source['kind'] == 14 and source['b'] == call['buttons']
                 and source['qpc'] <= sample['qpc'], 'wrong_input_source')
            need((s[11]['c'] & 15) == (source['b'] & 15), 'direction_not_consumed_by_core')
            submits = [r for r in part if r['kind'] == 13 and r['thread'] == thread
                       and s[6]['qpc'] <= r['qpc'] <= s[7]['qpc']]
            need(len(submits) == 1, 'missing_unique_guest_swap')
            submit = submits[0]
            outputs = out_by_submit.get(submit['seq'], [])
            need(len(outputs) == 1, 'missing_unique_owned_present')
            output = outputs[0]
            points = dict(source=source, input_api_return=by_id[call['end_seq']], game_input=s[11],
                logic_start=s[12], logic_end=s[13], core_return=s[5], draw_start=s[16],
                draw_end=s[17], wait_start=s[8], wait_end=s[9], guest_submit=submit,
                present_begin=by_id[output['present_begin_seq']], present_return=by_id[output['present_end_seq']])
            pairs = (('source', 'game_input'), ('input_api_return', 'game_input'),
                ('game_input', 'logic_end'), ('logic_end', 'core_return'),
                ('core_return', 'draw_start'), ('draw_start', 'draw_end'),
                ('draw_end', 'wait_start'), ('wait_start', 'wait_end'),
                ('wait_end', 'guest_submit'), ('guest_submit', 'present_begin'),
                ('game_input', 'present_begin'), ('source', 'present_begin'))
            need(all(points[b]['qpc'] >= points[a]['qpc'] for a, b in pairs), 'reversed_chain')
            result = dict(command=source['a'], source_seq=source['seq'], sample_seq=sample['seq'],
                buttons=source['b'], mapped_buttons=s[11]['c'] & LOW32,
                actual_device_flags=s[7]['c'] & LOW32,
                frame_delay=1 if s[7]['c'] & 5 == 4 else 2 if s[7]['c'] & 5 == 0 else None,
                points={k: r['seq'] for k, r in points.items()},
                times={f'{a}_to_{b}_us': (points[b]['qpc']-points[a]['qpc'])*1e6/frequency for a,b in pairs},
                original_sites={k: r['seq'] for k, r in s.items()}, output=output)
            cycles += 1
            if source['c'] == 1 and first <= source['a'] <= last:
                if first_core.get(source['seq']) == s[11]['seq']:
                    selected.setdefault(source['seq'], result)
        except ValueError as exc:
            rejected[str(exc)] += 1
    samples = list(selected.values())
    return dict(capture=summary['capture'], candidate_cycles=len(cut)-1, matched_cycles=cycles,
        rejected_cycles=dict(rejected), requested_sources=len(sources), paired_sources=len(samples),
        unpaired_commands=[r['a'] for r in sources if r['seq'] not in selected],
        actual_device_flags=dict(Counter(s['actual_device_flags'] for s in samples)),
        modes=dict(Counter(s['frame_delay'] for s in samples)),
        samples=samples, statistics={k: distribution([s['times'][k] for s in samples],60)
                                    for k in (samples[0]['times'] if samples else [])},
        scope='Synthetic controller -> original Xbox360 core input -> synchronous update/draw -> packet-owned Present',
        unmeasured=['physical_or_Windows_input_frontend','first_changed_pixel','display_scanout','panel_photons','probe_off_control'])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('trace', type=Path)
    p.add_argument('--logs', type=Path, nargs='+')
    p.add_argument('--clock', type=Path)
    p.add_argument('--first-command', type=int, default=101)
    p.add_argument('--last-command', type=int, default=160)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    trace = read_trace(a.trace.read_text(encoding='utf-8'))
    original_trace = trace
    paths = [a.trace]
    if a.logs:
        if not a.clock:
            p.error('--logs requires --clock')
        trace = merge_output(trace, [f.read_text(encoding='utf-8') for f in a.logs],
                             json.loads(a.clock.read_text(encoding='utf-8')))
        paths += [a.clock, *a.logs]
    report = analyze(trace, a.first_command, a.last_command)
    if a.logs:
        report['native_capture'] = {k:original_trace[k] for k in ('pid','committed','complete','overflow','incomplete')}
    else:
        report['source'] = dict(path=str(a.trace.resolve()),sha256=digest(a.trace))
    report['sources'] = [dict(path=str(f.resolve()),sha256=digest(f)) for f in paths]
    write_json_new(a.output, report)
    print(json.dumps({k:v for k,v in report.items() if k!='samples'},indent=2))


if __name__ == '__main__':
    main()
