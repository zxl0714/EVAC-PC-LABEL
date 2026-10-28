"""Match Futari's original single-update handshake and render-list ownership.

Uses the opcode-pinned reco_abnormal observation sites, not the next Present.
Reports command-batch provenance; visible movement/pixel timing is separate.
"""
import argparse
from collections import Counter
import json
from pathlib import Path

from input_latency_report import distribution
from latency_probe import read_trace, summarize_trace
from project import digest, write_json_new

SITE_ADDRESSES = {1: 0x8216B22C, 4: 0x821C5664, 5: 0x821C56F8,
    6: 0x820A56AC, 11: 0x8808FCDC, 12: 0x88168F78, 13: 0x88168F7C,
    14: 0x8808FC24, 15: 0x8824E8A0, 16: 0x8808D4A4, 17: 0x8808E66C,
    18: 0x8808FA90, 19: 0x8808FAA0, 20: 0x8808FC98, 21: 0x8808FCB4}
ORDER = (18, 21, 4, 5, 11, 12, 13, 16, 14, 20, 19, 15, 17, 1, 6)
LOW32 = 0xffffffff


def match_cycle(rows, inputs, outputs, by_id, frequency):
    def need(ok, message):
        if not ok:
            raise ValueError(message)

    observed = [r for r in rows if r['kind'] == 10 and r['a'] in SITE_ADDRESSES]
    need([r['a'] for r in observed] == list(ORDER), 'incomplete_or_multi_update_cycle')
    sites = {r['a']: r for r in observed}
    need(all(r['b'] == SITE_ADDRESSES[r['a']] for r in observed), 'wrong_site_address')
    main, core = sites[18]['thread'], sites[21]['thread']
    need(main != core, 'missing_two_thread_handshake')
    need(all(sites[k]['thread'] == core for k in (21, 4, 5, 11, 12, 13, 16, 14, 20)),
         'wrong_worker_thread')
    need(all(sites[k]['thread'] == main for k in (18, 19, 15, 17, 1, 6)), 'wrong_render_thread')
    need(sites[21]['c'] == 0 and sites[21]['e'] == 0 and sites[20]['e'] == 0,
         'non_single_update_or_exit_wake')
    need(sites[19]['c'] == 0, 'completion_wait_failed')
    owner = sites[18]['d'] & LOW32
    need(owner == (sites[19]['d'] & LOW32) == ((sites[20]['d'] - 764) & LOW32),
         'wrong_completion_owner')
    need((sites[5]['d'] & LOW32) == (sites[11]['c'] & LOW32) and sites[5]['e'] == 0,
         'wrong_game_input_destination')
    published, consumed = sites[16], sites[17]
    start, end, count = consumed['c'] & LOW32, consumed['d'] & LOW32, published['c'] & LOW32
    need(end >= start and end - start == count * 4 and count > 0, 'wrong_render_list_length')
    need(start == (published['d'] & LOW32) and
         (published['e'] & LOW32) == (consumed['e'] & LOW32) and
         (published['f'] & LOW32) == (consumed['f'] & LOW32), 'wrong_render_list_owner')
    calls = [r for r in inputs if r['result'] == 0 and r['user'] == 0 and
             by_id[r['end_seq']]['thread'] == core and
             sites[21]['qpc'] <= by_id[r['begin_seq']]['qpc'] <=
             by_id[r['end_seq']]['qpc'] <= sites[4]['qpc']]
    need(len(calls) == 1, 'ambiguous_core_input')
    call = calls[0]
    sampled = [r for r in rows if r['kind'] == 15 and r['thread'] == core and
               by_id[call['begin_seq']]['qpc'] <= r['qpc'] <= by_id[call['end_seq']]['qpc'] and
               (r['b'], r['c'], r['d']) == (call['user'], call['packet'], call['buttons'])]
    need(len(sampled) == 1, 'missing_exact_input_sample')
    source = by_id.get(sampled[0]['a'])
    need(source is not None and source['kind'] == 14 and source['b'] == call['buttons'] and
         source['qpc'] <= sampled[0]['qpc'], 'wrong_input_source')
    submits = [r for r in rows if r['kind'] == 13 and r['thread'] == main and
               r['qpc'] >= sites[6]['qpc']]
    need(len(submits) == 1, 'ambiguous_guest_swap')
    submit = submits[0]
    output = [r for r in outputs if r['guest_submit_seq'] == submit['seq'] and r['first_present']]
    need(len(output) == 1, 'missing_unique_successful_output')
    output = output[0]
    points = dict(source=source, game_input=sites[11], logic_end=sites[13],
                  list_published=published, list_consumed=consumed, guest_submit=submit,
                  frame_delay_branch=sites[1], guest_swap_call=sites[6],
                  present_begin=by_id[output['present_begin_seq']],
                  present_return=by_id[output['present_end_seq']])
    pairs = [('source', 'game_input'), ('game_input', 'logic_end'),
             ('logic_end', 'list_published'), ('list_published', 'list_consumed'),
             ('list_consumed', 'guest_submit'), ('guest_submit', 'present_begin'),
             ('list_consumed', 'frame_delay_branch'), ('frame_delay_branch', 'guest_swap_call'),
             ('game_input', 'present_begin'), ('source', 'present_begin'),
             ('source', 'present_return')]
    times = {}
    for a, b in pairs:
        need(points[b]['qpc'] >= points[a]['qpc'], 'reversed_link_clock')
        times[f'{a}_to_{b}_us'] = (points[b]['qpc'] - points[a]['qpc']) * 1e6 / frequency
    return dict(source_seq=source['seq'], command=source['a'], source_pressed=source['c'] == 1,
                sample_seq=sampled[0]['seq'],
                buttons=source['b'], mapped_buttons=sites[5]['c'] & LOW32,
                frame_delay=sites[1]['c'], render_list=dict(start=start, words=count,
                    owner=published['e'] & LOW32, lock=published['f'] & LOW32),
                points={k: r['seq'] for k, r in points.items()},
                handshake={k: r['seq'] for k, r in sites.items()}, times=times)


def analyze(trace, first_command=101, last_command=160):
    if not trace['complete']:
        raise ValueError('Complete trace is required for handshake provenance')
    summary = summarize_trace(trace)
    if summary['guest_observations']['invalid_sites'] or summary['invalid_links'] or summary['guest_submit_pairing_errors']:
        raise ValueError('Probe identity validation failed')
    rows = sorted(trace['records'], key=lambda r: (r['qpc'], r['seq']))
    by_id = {r['seq']: r for r in rows}
    cuts = [i for i, r in enumerate(rows) if r['kind'] == 10 and r['a'] == 18]
    # Only API calls from a cycle's own records are scanned (linear total cost).
    calls_by_end = {c['end_seq']: c for c in summary['input_calls']}
    outputs_by_submit = {}
    for output in summary['output_responses']:
        outputs_by_submit.setdefault(output['guest_submit_seq'], []).append(output)
    cycles, rejected = [], []
    core_threads = {r['thread'] for r in rows if r['kind'] == 10 and r['a'] == 11}
    if len(core_threads) != 1:
        raise ValueError('Exactly one observed Futari core thread is required')
    first_core_samples = {}
    for r in rows:
        if r['kind'] == 15 and r['thread'] in core_threads:
            first_core_samples.setdefault(r['a'], r['seq'])
    for lo, hi in zip(cuts, cuts[1:]):
        part = rows[lo:hi]
        calls = [calls_by_end[r['seq']] for r in part if r['seq'] in calls_by_end]
        outputs = [o for r in part if r['kind'] == 13 for o in outputs_by_submit.get(r['seq'], [])]
        try:
            cycles.append(match_cycle(part, calls, outputs, by_id, trace['frequency']))
        except ValueError as exc:
            rejected.append(dict(wake_seq=part[0]['seq'], reason=str(exc)))
    selected = {}
    for c in cycles:
        if (c['source_pressed'] and first_command <= c['command'] <= last_command and
                c['sample_seq'] == first_core_samples.get(c['source_seq'])):
            selected.setdefault(c['source_seq'], c)
    sources = [r for r in rows if r['kind'] == 14 and r['c'] == 1 and first_command <= r['a'] <= last_command]
    samples = list(selected.values())
    return dict(capture={k: summary['capture'][k] for k in ('complete', 'records', 'overflow', 'incomplete')},
                candidate_cycles=len(cuts) - 1, matched_cycles=len(cycles),
                rejected_cycles=dict(Counter(r['reason'] for r in rejected)),
                requested_sources=len(sources), paired_sources=len(samples),
                unpaired_commands=[r['a'] for r in sources if r['seq'] not in selected],
                modes=dict(Counter(s['frame_delay'] for s in samples)), samples=samples,
                statistics={k: distribution([s['times'][k] for s in samples], 60)
                            for k in (samples[0]['times'] if samples else [])},
                scope='Synthetic controller -> actual game input -> original completed render list -> owned Present',
                unmeasured=['physical_or_Windows_input_frontend', 'first_changed_pixel',
                            'display_scanout', 'panel_photons', 'probe_off_control'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--first-command', type=int, default=101)
    parser.add_argument('--last-command', type=int, default=160)
    args = parser.parse_args()
    report = analyze(read_trace(args.trace.read_text(encoding='utf-8')), args.first_command, args.last_command)
    report['source'] = dict(path=str(args.trace.resolve()), sha256=digest(args.trace))
    write_json_new(args.output, report)
    print(json.dumps({k: v for k, v in report.items() if k != 'samples'}, indent=2))


if __name__ == '__main__':
    main()
