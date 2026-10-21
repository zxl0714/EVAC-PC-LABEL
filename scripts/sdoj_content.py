"""Assemble bounded command snapshots; candidates are not first-pixel measurements."""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import re

from project import digest, write_json_new
from sdoj_latency import fields_from, run_logs


def snapshots(lines):
    grouped = defaultdict(list)
    for number, line in enumerate(lines, 1):
        if 'SDOJ_CONTENT ' not in line:
            continue
        row = fields_from(line)
        payload = re.search(r'\bwords=([^\s]+)', line)
        if not payload:
            raise ValueError(f'Missing command chunk on line {number}')
        row['words'] = payload[1]
        # These are explicitly hexadecimal, including values with digits only.
        for name in ('buffer', 'cursor', 'mask'):
            value = re.search(r'\b' + name + r'=([0-9A-Fa-f]{8})\b', line)
            if not value:
                raise ValueError(f'Missing hexadecimal {name} on line {number}')
            row[name] = int(value[1], 16)
        row['line'] = number
        grouped[(row['module'], row['generation'], row['copy'], row['batch'], row['buffer'])].append(row)
    result = []
    for key, rows in grouped.items():
        rows.sort(key=lambda row: row['offset'])
        first = rows[0]
        identity = ('t_us', 'host_iteration', 'cursor', 'mask', 'baseline', 'valid', 'total')
        if any(any(row[name] != first[name] for name in identity) for row in rows):
            raise ValueError(f'Inconsistent snapshot metadata: {key}')
        if not first['valid']:
            result.append(dict(first, complete=False, issue='runtime_rejected', packets=[]))
            continue
        total = first['total']
        if not 0 < total <= 16384 or first['cursor'] - first['buffer'] != total * 4:
            raise ValueError(f'Invalid snapshot size: {key}')
        words = []
        for row in rows:
            payload = str(row['words'])
            if row['offset'] != len(words) or not re.fullmatch(r'(?:[0-9A-Fa-f]{8}){1,64}', payload):
                raise ValueError(f'Missing, overlapping or malformed chunk: {key}')
            words.extend(int(payload[index:index + 8], 16) for index in range(0, len(payload), 8))
        if len(words) != total or words[-1] != 0xffffffff:
            raise ValueError(f'Incomplete snapshot or missing sentinel: {key}')
        # The reviewed sprite emission callbacks write nine words. Reject the
        # whole interpretation if its alignment/type invariants do not hold.
        if (len(words) - 1) % 9:
            raise ValueError(f'Unknown command packet alignment: {key}')
        packets = []
        for index in range(0, len(words) - 1, 9):
            packet = words[index:index + 9]
            if packet[0] >> 24 not in range(0x11, 0x20, 2):
                raise ValueError(f'Unknown packet header: {key}, word {index}')
            packets.append(dict(index=index // 9, header=packet[0], uv=packet[1],
                                x=packet[2] >> 16, y=packet[2] & 65535,
                                attributes=packet[3:]))
        result.append(dict(first, complete=True, packets=packets))
    return sorted(result, key=lambda row: (row['t_us'], row['module']))


def signature(packet, animation=False):
    return (packet['header'], None if animation else packet['uv'], *packet['attributes'])


def movement(before, after):
    if not before['complete'] or not after['complete'] or any(
            before[name] != after[name] for name in ('module', 'generation')):
        return None
    if after['copy'] != before['copy'] + 1:
        return None
    groups = []
    for snapshot in (before, after):
        group = defaultdict(list)
        for packet in snapshot['packets']:
            group[signature(packet)].append(packet)
        groups.append(group)
    # Infer the common render-target translation from unambiguous exact sprite
    # attributes. Never mistake the alternating atlas origin for player motion.
    translations = Counter()
    for key in groups[0].keys() & groups[1].keys():
        left, right = groups[0][key], groups[1][key]
        if len(left) == len(right) == 1:
            translations[(right[0]['x'] - left[0]['x'], right[0]['y'] - left[0]['y'])] += 1
    if not translations:
        return None
    (origin_x, origin_y), votes = translations.most_common(1)[0]
    if votes < 3:
        return None
    animated = []
    for snapshot in (before, after):
        group = defaultdict(list)
        for packet in snapshot['packets']:
            group[signature(packet, animation=True)].append(packet)
        animated.append(group)
    candidates = []
    for key in animated[0].keys() & animated[1].keys():
        left, right = animated[0][key], animated[1][key]
        if len(left) != 1 or len(right) != 1:
            continue
        left, right = left[0], right[0]
        dx, dy = right['x'] - left['x'] - origin_x, right['y'] - left['y'] - origin_y
        if (dx or dy) and abs(dx) <= 16 and abs(dy) <= 16:
            candidates.append(dict(before=left, after=right, relative_dx=dx, relative_dy=dy))
    return dict(before_copy=before['copy'], after_copy=after['copy'], mask=after['mask'],
                common_translation=[origin_x, origin_y], translation_votes=votes,
                candidates=candidates, semantic_player_identity_requires_review=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    paths = run_logs(args.run)
    lines = [line for path in paths for line in path.read_text(encoding='utf-8').splitlines()]
    captures = snapshots(lines)
    comparisons = [comparison for left, right in zip(captures, captures[1:])
                   if (comparison := movement(left, right))]
    metadata = [{key: value for key, value in row.items() if key not in ('words', 'packets')}
                for row in captures]
    report = dict(scope='Command content and relative motion candidates, not pixels, Present or physical scanout',
                  snapshots=metadata, comparisons=comparisons,
                  sources=[dict(path=str(path), sha256=digest(path)) for path in paths])
    write_json_new(args.output, report)
    print(json.dumps(dict(snapshots=len(captures), complete=sum(row['complete'] for row in captures),
                         comparisons=len(comparisons), candidates=sum(len(row['candidates']) for row in comparisons))))


if __name__ == '__main__':
    main()
