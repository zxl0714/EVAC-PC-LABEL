"""Join captured response images to their own DXGI ETW Present call.

Both ETW entry and return must lie inside the instrumented call interval. This
is deliberately not a nearest-Present association. Display feedback is optional:
an unobserved display remains unavailable, never a zero-latency sample.
"""
import argparse
from bisect import bisect_left, bisect_right
from collections import Counter
import csv
import json
from pathlib import Path

from pixel_probe import read_capture
from present_latency import number, distribution
from project import digest, write_json_new


BASE = {'ProcessID', 'SwapChainAddress', 'PresentRuntime', 'CPUStartQPC',
        'CPUBusy', 'CPUWait', 'FrameTime', 'SyncInterval', 'PresentFlags'}


def join(capture, rows):
    header, footer, frames = (capture[k] for k in ('header', 'footer', 'frames'))
    if footer['overflow'] or not frames:
        raise ValueError('Complete, nonempty, non-overflowed pixel capture required')
    hz, pid = header['frequency'], header['pid']
    # The CLI rounds durations to 0.1 us. Permit only that rounding at the
    # instrumentation boundaries, not an arbitrary nearest-frame tolerance.
    rounding = max(1.0, hz * .00000021)
    events, identities, counts = [], set(), Counter()
    for line, row in enumerate(rows, 2):
        if not BASE <= row.keys():
            raise ValueError('Missing DXGI/QPC PresentMon columns')
        if int(row['ProcessID']) != pid:
            counts['other_process_rows'] += 1
            continue
        if row['PresentRuntime'] != 'DXGI':
            raise ValueError('DXGI instrumentation required, not Vulkan/Other')
        if row.get('FrameType', 'Application') != 'Application':
            raise ValueError('Generated/repeated ETW frames require separate analysis')
        start = int(row['CPUStartQPC'])
        busy, wait, duration = (number(row[k], k) for k in ('CPUBusy', 'CPUWait', 'FrameTime'))
        if abs(duration - busy - wait) > .00031:
            raise ValueError('Inconsistent ETW Present duration')
        entry, end = start + busy * hz / 1000, start + duration * hz / 1000
        if events and entry <= events[-1]['entry']:
            raise ValueError('Non-increasing/duplicate ETW Present calls')
        identity = row['SwapChainAddress']
        identities.add(identity)
        if len(identities) > 1:
            raise ValueError('Select one swapchain per capture')
        events.append(dict(line=line, entry=entry, end=end, row=row))
    if not events:
        raise ValueError('No ETW calls for the pixel-capture PID')
    entries = [e['entry'] for e in events]
    used, joined, missing = set(), [], []
    for frame in frames:
        if frame['hr']:
            raise ValueError('Failed pixel Present cannot prove an output')
        lo = bisect_left(entries, frame['present_begin'] - rounding)
        hi = bisect_right(entries, frame['present_end'] + rounding)
        candidates = [i for i in range(lo, hi)
                      if events[i]['end'] <= frame['present_end'] + rounding
                      and events[i]['end'] >= events[i]['entry']]
        if len(candidates) > 1:
            raise ValueError('Ambiguous ETW calls inside one instrumented Present')
        if not candidates:
            missing.append(dict(index=frame['index'], reason='no_contained_etw_present'))
            continue
        index = candidates[0]
        if index in used:
            raise ValueError('One ETW call cannot own two captured images')
        used.add(index)
        event, row = events[index], events[index]['row']
        item = dict(index=frame['index'], source_frame=frame['source_frame'],
                    source_packet=frame['source_packet'], source_event=frame['source_event'],
                    present_begin_qpc=frame['present_begin'],
                    present_end_qpc=frame['present_end'], etw_line=event['line'],
                    etw_entry_qpc=event['entry'], etw_return_qpc=event['end'],
                    entry_bracket_us=(event['entry']-frame['present_begin'])*1e6/hz,
                    return_bracket_us=(frame['present_end']-event['end'])*1e6/hz,
                    swapchain=row['SwapChainAddress'], sync_interval=int(row['SyncInterval']),
                    present_flags=int(row['PresentFlags']), display=None)
        if 'DisplayLatency' not in row or 'DisplayedTime' not in row:
            item['display_unavailable_reason'] = 'display_tracking_not_in_csv'
        elif row['DisplayLatency'] == 'NA' or row['DisplayedTime'] == 'NA':
            if row['DisplayLatency'] != row['DisplayedTime']:
                raise ValueError('Incomplete display timing pair')
            item['display_unavailable_reason'] = 'not_displayed_or_not_observed'
        else:
            if not {'PresentMode', 'AllowsTearing'} <= row.keys():
                raise ValueError('Display feedback lacks its presentation policy')
            delay, shown = (number(row[k], k) for k in ('DisplayLatency', 'DisplayedTime'))
            display_qpc = int(row['CPUStartQPC']) + delay * hz / 1000
            if display_qpc < event['entry'] - rounding:
                raise ValueError('OS display precedes its own Present')
            if not shown:
                item['display_unavailable_reason'] = 'zero_display_duration'
            else:
                item['display'] = dict(qpc=display_qpc, duration_ms=shown,
                    present_mode=row['PresentMode'], allows_tearing=int(row['AllowsTearing']),
                    present_entry_to_os_display_ms=(display_qpc-frame['present_begin'])*1000/hz)
        joined.append(item)
    counts.update(captured_images=len(frames), matched_presents=len(joined),
                  missing_presents=len(missing),
                  observed_displays=sum(item['display'] is not None for item in joined))
    return dict(process_id=pid, qpc_frequency=hz, counts=dict(counts), images=joined,
                missing=missing, swapchain=next(iter(identities)),
                etw_entry_bracket_ms=distribution([x['entry_bracket_us']/1000 for x in joined]),
                etw_return_bracket_ms=distribution([x['return_bracket_us']/1000 for x in joined]))


def responses(result, accepted):
    by_index = {f['index']: f for f in result['images']}
    output, unavailable, commands = [], [], set()
    for response in accepted:
        command = response['command']
        if command in commands:
            raise ValueError('Duplicate response command')
        commands.add(command)
        image = by_index.get(response['first_response_index'])
        if image is None or image['display'] is None:
            unavailable.append(dict(command=command, reason=(
                image['display_unavailable_reason'] if image else 'no_contained_etw_present')))
            continue
        delay = image['display']['present_entry_to_os_display_ms']
        source_ms = number(response['source_to_response_present_us'], 'source interval') / 1000
        core_ms = number(response['core_to_response_present_us'], 'core interval') / 1000
        if core_ms > source_ms:
            raise ValueError('Response core precedes its source')
        output.append(dict(command=command, index=image['index'], etw_line=image['etw_line'],
            source_to_response_os_display_ms=source_ms+delay,
            core_to_response_os_display_ms=core_ms+delay,
            present_to_os_display_ms=delay, present_mode=image['display']['present_mode']))
    def statistics(key):
        stats = distribution([sample[key] for sample in output])
        if stats is not None:
            stats['tail_sample_sufficient'] = len(output) >= 30
            if not stats['tail_sample_sufficient']:
                stats['p95_ms'] = stats['p99_ms'] = None
        return stats
    return dict(samples=output, unavailable=unavailable,
        statistics={k: statistics(k) for k in
                    ('source_to_response_os_display_ms', 'core_to_response_os_display_ms',
                     'present_to_os_display_ms')})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pixels', required=True, type=Path)
    parser.add_argument('--presentmon', required=True, type=Path)
    parser.add_argument('--response', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    capture = read_capture(args.pixels)
    with args.presentmon.open(encoding='utf-8-sig', newline='') as stream:
        report = join(capture, csv.DictReader(stream))
    sources = [args.presentmon, Path(str(args.pixels)+'.csv'), capture['binary']]
    if args.response:
        response = json.loads(args.response.read_text(encoding='utf8'))
        recorded = {str(Path(s['path']).resolve()): s['sha256'] for s in response['sources']}
        for source in sources[1:]:
            if recorded.get(str(source.resolve())) != digest(source):
                raise ValueError('Response report does not own these exact pixel files')
        # Verify the response artifact's complete input evidence, not only its
        # filename or PID; this also prevents using a stale response selection.
        for path, expected in recorded.items():
            if digest(Path(path)) != expected:
                raise ValueError('Response source evidence changed: ' + path)
        report['responses'] = responses(report, response['accepted'])
        sources.append(args.response)
    report.update(schema_version=1,
        scope='Same-image instrumented Present -> ETW Present -> OS display, where observed',
        sources=[dict(path=str(p.resolve()), sha256=digest(p)) for p in sources],
        limitations=['OS display is not a screen-specific scanline or panel photons.',
                     'Missing display events are unavailable, not zero latency.',
                     'Response totals use each input and its own image, not summed medians.'])
    write_json_new(args.output, report)
    print(json.dumps(report['counts'], indent=2))


if __name__ == '__main__':
    main()
