"""Summarize original Guwange call targets; discovery is not response timing."""
import argparse
from collections import Counter
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from latency_probe import read_trace, summarize_trace
from project import digest, write_json_new


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--command', type=int, help='Inspect at most 80 boundaries near one test publication')
    args = parser.parse_args()
    trace = read_trace(args.trace.read_text())
    if not trace['complete']:
        raise ValueError('Overflow/incomplete capture is not accepted')
    report = summarize_trace(trace)
    observations = [r for r in trace['records'] if r['kind'] == 10]
    targets = {}
    for identifier in (3, 7, 9, 23, 25):
        targets[identifier] = dict(Counter(f"{r['c'] & 0xffffffff:08X}"
            for r in observations if r['a'] == identifier))
    result = dict(scope='Original control-flow target discovery; not core consumption or first responsive image',
        capture={k:trace[k] for k in ('pid', 'committed', 'complete', 'overflow', 'incomplete')},
        original_call_targets=targets,
        original_site_counts=dict(Counter(r['a'] for r in observations)),
        input_api_calls=len(report['input_calls']),
        input_sources=report['synthetic_guest_responses'],
        owned_present_count=len(report['output_responses']),
        invalid_links=report['invalid_links'],
        source=dict(path=str(args.trace.resolve()), sha256=digest(args.trace)),
        unmeasured=['core_input_consumption', 'first_response_image', 'input_latency', 'panel_light'])
    write_json_new(args.output, result)
    print(json.dumps({k:v for k,v in result.items() if k != 'input_sources'}, indent=2))
    if args.command is not None:
        source, = [r for r in trace['records'] if r['kind'] == 14 and r['c'] == 1 and r['a'] == args.command]
        selected = [r for r in trace['records'] if source['qpc'] - trace['frequency'] * .002 <= r['qpc'] <= source['qpc'] + trace['frequency'] * .040]
        for r in sorted(selected, key=lambda r:(r['qpc'],r['seq']))[:80]:
            print(f"{(r['qpc']-source['qpc'])*1000/trace['frequency']:9.4f}ms tid={r['thread']} seq={r['seq']} kind={r['kind']} " +
                  ' '.join(f"{k}={r[k] & 0xffffffffffffffff:X}" for k in ('a','b','c','d','e','f')))


if __name__ == '__main__':
    main()
