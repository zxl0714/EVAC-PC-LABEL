"""Read-only command-buffer lineage analysis; never measures physical scanout."""
import argparse
from collections import Counter
import json
from pathlib import Path
import re
import statistics

from project import digest, write_json_new

HEX_FIELDS = {'argument', 'writer', 'published', 'status', 'cursor',
              'current0', 'current1', 'consumed0', 'consumed1', 'before', 'after', 'buttons',
              'draw_list', 'queue', 'slot', 'frame_arg'}


def fields_from(line):
    fields = dict(re.findall(r'(\w+)=([^\s]+)', line))
    return {key: (int(value, 16) if key in HEX_FIELDS else
                  value == 'true' if value in ('true', 'false') else
                  int(value) if value.isdecimal() else value)
            for key, value in fields.items()}


def records(lines):
    result = []
    for line_number, line in enumerate(lines, 1):
        if 'SDOJ_LATENCY ' not in line:
            continue
        fields = fields_from(line)
        if fields.get('phase') == 'guest_submit':
            packet = re.search(r'\bpacket=([0-9a-fA-F]{8})\b', line)
            if not packet:
                raise ValueError(f'Missing physical swap packet on line {line_number}')
            fields['packet'] = int(packet[1], 16)
        if not {'seq', 't_us', 'phase', 'host_iteration', 'thread'} <= fields.keys():
            raise ValueError(f'Incomplete latency record on line {line_number}')
        event = {'line': line_number}
        event.update(fields)
        result.append(event)
    return sorted(result, key=lambda event: (event['t_us'], event['seq']))


def input_records(lines):
    result = []
    for line_number, line in enumerate(lines, 1):
        if 'INPUT_DIAG key=' not in line and 'SDOJ_INPUT ' not in line:
            continue
        fields = fields_from(line)
        if 't_us' not in fields:
            continue
        result.append(dict(fields, line=line_number))
    return sorted(result, key=lambda event: (event['t_us'], event['line']))


def direction_edges(inputs, report, events):
    presses = [event for event in inputs if event.get('down') is True and
               event.get('repeat') is False and
               event.get('after', 0) & ~event.get('before', 0) in (1, 2, 4, 8)]
    result = []
    for press in presses:
        bit = press['after'] & ~press['before']
        deadline = min([press['t_us'] + 500000] + [other['t_us'] for other in presses
                       if other['t_us'] > press['t_us'] and other['after'] & ~other['before'] & bit])
        raw = next((row for row in inputs if row.get('phase') == 'button_copy' and
                    row.get('user') == 0 and press['t_us'] <= row['t_us'] < deadline and row['buttons'] & bit), None)
        built = next((row for row in inputs if raw and row.get('phase') == 'core_mask_built' and
                      raw['t_us'] <= row['t_us'] < deadline and row['current0'] & bit), None)
        copied = next((row for row in events if built and row.get('phase') == 'logic_copy' and
                       built['t_us'] <= row['t_us'] < deadline and
                       (row['module'], row['generation']) == (built['module'], built['generation']) and
                       row['current0'] == built['current0'] and row['current1'] == built['current1']), None)
        lineage = next((chain for chain in report['lineages'] if copied and
                        chain['copy']['seq'] == copied['seq']), None)
        intervals = {}
        if raw:
            intervals['key_to_raw_copy'] = raw['t_us'] - press['t_us']
        if built:
            intervals['raw_copy_to_mask_build'] = built['t_us'] - raw['t_us']
        if copied:
            intervals['mask_build_to_logic_copy'] = copied['t_us'] - built['t_us']
        if lineage:
            intervals.update(lineage['intervals_us'])
            intervals['key_to_renderer_source'] = lineage['source']['t_us'] - press['t_us']
        result.append(dict(bit=bit, key=press['key'], press=press, raw=raw, built=built,
                           copied=copied, lineage=lineage, intervals_us=intervals,
                           live_player_requires_external_observation=True))
    return result


def run_logs(path):
    if path.is_file():
        return [path]
    if not path.is_dir():
        raise ValueError('Expected a game.log or run directory')
    rotated = []
    for candidate in path.iterdir():
        match = re.fullmatch(r'game\.(\d+)\.log', candidate.name)
        if match and candidate.is_file():
            rotated.append((int(match[1]), candidate))
    files = [candidate for _, candidate in sorted(rotated, reverse=True)]
    current = path / 'game.log'
    if current.is_file():
        files.append(current)
    if not files:
        raise ValueError('No runtime game logs')
    return files


def stats(values):
    values = sorted(values)
    if not values:
        return {'count': 0}
    return {'count': len(values), 'min': values[0], 'median': statistics.median(values),
            'p95': values[(len(values) * 95 + 99) // 100 - 1], 'max': values[-1]}


def analyze(events):
    copies, writing, completed, publications = {}, {}, {}, {}
    renders, paired, issues = {}, [], Counter()
    for event in events:
        if 'module' not in event:
            continue
        module = (event['module'], event['generation'])
        thread = (*module, event['thread'])
        phase = event['phase']
        if not event.get('valid') or not event.get('swap_stable'):
            issues['invalid_snapshot'] += 1
            # Lost observations must not leave a producer available for pairing.
            writing.pop(thread, None)
            completed = {key: value for key, value in completed.items() if key[:2] != module}
            publications = {key: value for key, value in publications.items() if key[:2] != module}
            continue
        if phase == 'logic_copy':
            copies[(*module, event['copies'])] = event
        elif phase == 'commands_begin':
            if thread in writing:
                issues['nested_or_missing_command_end'] += 1
            pointer = event['argument']
            completed.pop((*module, pointer), None)  # Reuse invalidates an old stamp.
            publications = {key: value for key, value in publications.items()
                            if key[:2] != module or key[3] != pointer}
            for reader, stamp in list(renders.items()):
                if reader[:2] == module and stamp['source']['argument'] == pointer:
                    issues['source_reused_during_read'] += 1
                    del renders[reader]
            if pointer not in (0, 0xffffffff):
                writing[thread] = event
            else:
                issues['invalid_writer'] += 1
                writing.pop(thread, None)
        elif phase == 'commands_end':
            began = writing.pop(thread, None)
            if (not began or began['batches'] != event['batches'] or
                    began['copies'] != event['copies'] or
                    not 0 < event['cursor'] - began['argument'] <= 0x120000):
                issues['unpaired_command_end'] += 1
                continue
            copied = copies.get((*module, began['copies']))
            if not copied or copied['t_us'] > began['t_us']:
                issues['unpaired_logic_copy'] += 1
                continue
            completed[(*module, began['argument'])] = dict(copy=copied, began=began, end=event)
        elif phase == 'swap_return':
            stamp = completed.get((*module, event['published']))
            key = (*module, event['swap_serial'], event['published'])
            if key not in publications and stamp:
                publications[key] = dict(stamp, swap=event)
        elif phase == 'render_source':
            key = (*module, event['swap_serial'], event['argument'])
            stamp = publications.get(key)
            if event['published'] != event['argument'] or not stamp:
                issues['unpaired_render_source'] += 1
                renders.pop(thread, None)
                continue
            # A producer may already be writing the same address in a later
            # generation. Pointer equality alone must never establish lineage.
            if any(key[:2] == module and value['argument'] == event['argument']
                   for key, value in writing.items()):
                issues['source_reused_during_read'] += 1
                renders.pop(thread, None)
                continue
            renders[thread] = dict(stamp, source=event)
        elif phase == 'render_return':
            stamp = renders.pop(thread, None)
            if not stamp or event['t_us'] < stamp['source']['t_us']:
                issues['unpaired_render_return'] += 1
                continue
            stamp['return'] = event
            paired.append(stamp)
    seq = sorted(event['seq'] for event in events)
    if len(seq) != len(set(seq)):
        raise ValueError('Duplicate observation sequence; do not merge repeated logs')
    gaps = [(left + 1, right - 1) for left, right in zip([0, *seq], seq)
            if right > left + 1]
    kept = []
    for stamp in paired:
        begin = min(stamp[key]['seq'] for key in ('copy', 'began', 'end', 'swap', 'source', 'return'))
        end = max(stamp[key]['seq'] for key in ('copy', 'began', 'end', 'swap', 'source', 'return'))
        if any(left <= end and right >= begin for left, right in gaps):
            issues['lineage_crosses_lost_observation'] += 1
        else:
            kept.append(stamp)
    paired = kept
    for stamp in paired:
        copy, began, end, swap, source = (stamp[key] for key in ('copy', 'began', 'end', 'swap', 'source'))
        stamp['intervals_us'] = {
            'copy_to_commands_begin': began['t_us'] - copy['t_us'],
            'commands_build': end['t_us'] - began['t_us'],
            'commands_end_to_publication': swap['t_us'] - end['t_us'],
            'publication_to_source': source['t_us'] - swap['t_us'],
            'copy_to_source': source['t_us'] - copy['t_us'],
            'source_to_render_return': stamp['return']['t_us'] - source['t_us'],
        }
        stamp['copy_to_source_host_iterations'] = source['host_iteration'] - copy['host_iteration']
        stamp['copy_to_source_epochs'] = source['epoch'] - copy['epoch']
    # Pause/slowdown can legitimately draw the same immutable result many
    # times. Keep every causal association, but measure the first use of each
    # completed batch separately from the age of a deliberately held picture.
    seen_results, first_results = set(), []
    for stamp in paired:
        key = (stamp['began']['module'], stamp['began']['generation'],
               stamp['began']['seq'], stamp['swap']['swap_serial'])
        stamp['first_result_read'] = key not in seen_results
        seen_results.add(key)
        if stamp['first_result_read']:
            first_results.append(stamp)
    rejected = max((event.get('rejected', 0) for event in events), default=0)
    missing = sum(right - left + 1 for left, right in gaps)
    bounded = bool(events) and max(seq) < min(event.get('limit', 65536) for event in events)
    return {
        'scope': 'Input-copy to this exact guest command buffer supplied to the renderer; not physical scanout or proven first changed pixel',
        'records': len(events), 'phases': dict(Counter(event['phase'] for event in events)),
        'observer_rejected': rejected, 'missing_sequence_records': missing,
        'within_record_budget': bounded, 'continuous_trace': bool(events) and not rejected and not missing and bounded,
        'issues': dict(issues), 'paired_command_buffers': len(paired),
        'first_result_reads': len(first_results), 'held_result_reads': len(paired)-len(first_results),
        'first_result_intervals_us': {key: stats([stamp['intervals_us'][key] for stamp in first_results])
                                      for key in (first_results[0]['intervals_us'] if first_results else ())},
        'first_result_copy_to_source_epochs': dict(Counter(stamp['copy_to_source_epochs'] for stamp in first_results)),
        'first_result_copy_to_source_iterations': dict(Counter(stamp['copy_to_source_host_iterations'] for stamp in first_results)),
        'intervals_us': {key: stats([stamp['intervals_us'][key] for stamp in paired])
                         for key in (paired[0]['intervals_us'] if paired else ())},
        'copy_to_source_host_iterations': dict(Counter(stamp['copy_to_source_host_iterations'] for stamp in paired)),
        'copy_to_source_epochs': dict(Counter(stamp['copy_to_source_epochs'] for stamp in paired)),
        'lineages': paired,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    files = run_logs(args.log)
    events, inputs = [], []
    for path in files:
        lines = path.read_text(encoding='utf-8').splitlines()
        for target, parse in ((events, records), (inputs, input_records)):
            target.extend(dict(row, log=path.name) for row in parse(lines))
    events.sort(key=lambda event: (event['t_us'], event['seq']))
    inputs.sort(key=lambda event: (event['t_us'], event['line']))
    report = analyze(events)
    report['input_edges'] = direction_edges(inputs, report, events)
    report['sources'] = [dict(path=str(path), sha256=digest(path)) for path in files]
    write_json_new(args.output, report)
    print(json.dumps({key: value for key, value in report.items()
                      if key not in ('lineages', 'input_edges')}, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
